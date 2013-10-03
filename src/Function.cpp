#include "IR.h"
#include "IRMutator.h"
#include "Function.h"
#include "Scope.h"
#include "IRPrinter.h"
#include "Debug.h"
#include "CSE.h"
#include <set>

namespace Halide {
namespace Internal {

static void assertf(bool cond, const char* msg, const std::string& name) {
  if (!cond) {
    std::cerr << msg << " (Func: " << name << ")" << std::endl;
    assert(false);
  }
}

using std::vector;
using std::string;
using std::set;

template<>
EXPORT RefCount &ref_count<FunctionContents>(const FunctionContents *f) {return f->ref_count;}

template<>
EXPORT void destroy<FunctionContents>(const FunctionContents *f) {delete f;}

// All variables present in any part of a function definition must
// either be pure args, elements of the reduction domain, parameters
// (i.e. attached to some Parameter object), or part of a let node
// internal to the expression
struct CheckVars : public IRGraphVisitor {
    vector<string> pure_args;
    ReductionDomain reduction_domain;
    Scope<int> defined_internally;
    const std::string name;

    CheckVars(const std::string& n) : name(n) {}

    using IRVisitor::visit;

    void visit(const Let *let) {
        let->value.accept(this);
        defined_internally.push(let->name, 0);
        let->body.accept(this);
        defined_internally.pop(let->name);
    }

    void visit(const Variable *var) {
        // Is it a parameter?
        if (var->param.defined()) return;

        // Was it defined internally by a let expression?
        if (defined_internally.contains(var->name)) return;

        // Is it a pure argument?
        for (size_t i = 0; i < pure_args.size(); i++) {
            if (var->name == pure_args[i]) return;
        }

        // Is it in a reduction domain?
        if (var->reduction_domain.defined()) {
            if (!reduction_domain.defined()) {
                reduction_domain = var->reduction_domain;
                return;
            } else if (var->reduction_domain.same_as(reduction_domain)) {
                // It's in a reduction domain we already know about
                return;
            } else {
                assertf(false, "Multiple reduction domains found in function definition", name);
            }
        }

        std::cerr << "Undefined variable in function definition: " << var->name
            << " (Func: " << name << ")"
            << std::endl;
        assert(false);
    }
};

struct CountSelfReferences : public IRGraphVisitor {
    set<const Call *> calls;
    const Function *func;

    using IRVisitor::visit;

    void visit(const Call *c) {
        if (c->func.same_as(*func)) {
            calls.insert(c);
        }
    }
};

void Function::define(const vector<string> &args, vector<Expr> values) {
    assertf(!has_extern_definition(), "Function with extern definition cannot be given a pure definition", name());
    assertf(!name().empty(), "A function needs a name", name());
    for (size_t i = 0; i < values.size(); i++) {
        assertf(values[i].defined(), "Undefined expression in right-hand-side of function definition", name());
    }

    // Make sure all the vars in the value are either args or are
    // attached to some parameter
    CheckVars check(name());
    check.pure_args = args;
    for (size_t i = 0; i < values.size(); i++) {
        values[i].accept(&check);
    }

    // Make sure all the vars in the args have unique non-empty names
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].empty()) {
            std::cerr << "In the left-hand-side of the definition of " << name()
                      << ", argument " << i << " has an empty name\n";
            assert(false);
        }
        for (size_t j = 0; j < i; j++) {
            if (args[i] == args[j]) {
                std::cerr << "In the left-hand-side of the definition of " << name()
                          << ", arguments " << j << " and " << i << " have the same name: " << args[i] << "\n";
                assert(false);
            }
        }
    }

    for (size_t i = 0; i < values.size(); i++) {
        values[i] = common_subexpression_elimination(values[i]);
    }

    assertf(!check.reduction_domain.defined(), "Reduction domain referenced in pure function definition", name());

    if (!contents.defined()) {
        contents = new FunctionContents;
        contents.ptr->name = unique_name('f');
    }

    assertf(contents.ptr->values.empty(), "Function is already defined", name());
    contents.ptr->values = values;
    contents.ptr->args = args;

    contents.ptr->output_types.resize(values.size());
    for (size_t i = 0; i < contents.ptr->output_types.size(); i++) {
        contents.ptr->output_types[i] = values[i].type();
    }

    for (size_t i = 0; i < args.size(); i++) {
        Schedule::Dim d = {args[i], For::Serial};
        contents.ptr->schedule.dims.push_back(d);
        contents.ptr->schedule.storage_dims.push_back(args[i]);
    }

    for (size_t i = 0; i < values.size(); i++) {
        string buffer_name = name();
        if (values.size() > 1) {
            buffer_name += '.' + int_to_string((int)i);
        }
        contents.ptr->output_buffers.push_back(Parameter(values[i].type(), true, buffer_name));
    }
}

void Function::define_reduction(const vector<Expr> &_args, vector<Expr> values) {
    assertf(!name().empty(), "A function needs a name", name());
    assertf(has_pure_definition(), "Can't add a reduction definition without a regular definition first", name());
    assertf(!has_reduction_definition(), "Function already has a reduction definition", name());
    for (size_t i = 0; i < values.size(); i++) {
        assertf(values[i].defined(), "Undefined expression in right-hand-side of reduction", name());
    }

    // Check the dimensionality matches
    assertf((int)_args.size() == dimensions(),
           "Dimensionality of reduction definition must match dimensionality of pure definition", name());

    assertf(values.size() == contents.ptr->values.size(),
            "Number of tuple elements for reduction definition must "
            "match number of tuple elements for pure definition", name());

    for (size_t i = 0; i < values.size(); i++) {
        // Check that pure value and the reduction value have the same
        // type.  Without this check, allocations may be the wrong size
        // relative to what update code expects.
        assertf(contents.ptr->values[i].type() == values[i].type(),
                "Reduction definition does not match type of pure function definition.",
                name());
        values[i] = common_subexpression_elimination(values[i]);
    }

    vector<Expr> args(_args.size());
    for (size_t i = 0; i < args.size(); i++) {
        args[i] = common_subexpression_elimination(_args[i]);
    }

    // The pure args are those naked vars in the args that are not in
    // a reduction domain and are not parameters
    vector<string> pure_args;
    for (size_t i = 0; i < args.size(); i++) {
        assertf(args[i].defined(), "Undefined expression in left-hand-side of reduction", name());
        if (const Variable *var = args[i].as<Variable>()) {
            if (!var->param.defined() && !var->reduction_domain.defined()) {
                assertf(var->name == contents.ptr->args[i],
                       "Pure argument to update step must have the same name as pure argument to initialization step in the same dimension", name());
                pure_args.push_back(var->name);
            }
        }

    }

    // Make sure all the vars in the args and the value are either
    // pure args, in the reduction domain, or a parameter
    CheckVars check(name());
    check.pure_args = pure_args;
    for (size_t i = 0; i < values.size(); i++) {
        values[i].accept(&check);
    }
    for (size_t i = 0; i < args.size(); i++) {
        args[i].accept(&check);
    }

    assertf(check.reduction_domain.defined(), "No reduction domain referenced in reduction definition", name());

    contents.ptr->reduction_args = args;
    contents.ptr->reduction_values = values;
    contents.ptr->reduction_domain = check.reduction_domain;

    // The reduction value and args probably refer back to the
    // function itself, introducing circular references and hence
    // memory leaks. We need to count the number of unique call nodes
    // that point back to this function in order to break the cycles.
    CountSelfReferences counter;
    counter.func = this;
    for (size_t i = 0; i < args.size(); i++) {
        args[i].accept(&counter);
    }
    for (size_t i = 0; i < values.size(); i++) {
        values[i].accept(&counter);
    }

    for (size_t i = 0; i < counter.calls.size(); i++) {
        contents.ptr->ref_count.decrement();
        assertf(!contents.ptr->ref_count.is_zero(),
                "Bug: removed too many circular references when defining reduction", name());
    }

    // First add the pure args in order
    for (size_t i = 0; i < pure_args.size(); i++) {
        Schedule::Dim d = {pure_args[i], For::Serial};
        contents.ptr->reduction_schedule.dims.push_back(d);
    }

    // Then add the reduction domain outside of that
    for (size_t i = 0; i < check.reduction_domain.domain().size(); i++) {
        Schedule::Dim d = {check.reduction_domain.domain()[i].var, For::Serial};
        contents.ptr->reduction_schedule.dims.push_back(d);
    }
}

void Function::define_extern(const std::string &function_name,
                             const std::vector<ExternFuncArgument> &args,
                             const std::vector<Type> &types,
                             int dimensionality) {

    assertf(!has_pure_definition() && !has_reduction_definition(),
            "Function with a pure definition cannot have an extern definition",
            name());

    assertf(!has_extern_definition(),
            "Function already has an extern definition",
            name());

    contents.ptr->extern_function_name = function_name;
    contents.ptr->extern_arguments = args;
    contents.ptr->output_types = types;

    for (size_t i = 0; i < types.size(); i++) {
        string buffer_name = name();
        if (types.size() > 1) {
            buffer_name += '.' + int_to_string((int)i);
        }
        contents.ptr->output_buffers.push_back(Parameter(types[i], true, buffer_name));
    }

    // Make some synthetic var names for scheduling purposes (e.g. reorder_storage).
    contents.ptr->args.resize(dimensionality);
    for (int i = 0; i < dimensionality; i++) {
        string arg = unique_name('e');
        contents.ptr->args[i] = arg;
        contents.ptr->schedule.storage_dims.push_back(arg);
    }

}

namespace {
Expr compute_min_extent(string dim, const Schedule &sched) {
    Expr size = 1;
    const vector<Schedule::Split> &splits = sched.splits;
    for (size_t i = 0; i < splits.size(); i++) {
        if (splits[i].old_var == dim && !splits[i].is_fuse()) {
            if (splits[i].is_split()) {
                Expr factor = splits[i].factor;
                size = Mul::make(size, factor);
            }
            dim = splits[i].outer;
        }
    }
    return size;
}
}

Expr Function::min_extent_produced(const string &d) const {
    return compute_min_extent(d, schedule());
}

Expr Function::min_extent_updated(const string &d) const {
    if (!has_reduction_definition()) {
        return 1;
    } else {
        return compute_min_extent(d, reduction_schedule());
    }
}


}
}
