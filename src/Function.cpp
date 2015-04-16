#include <set>
#include <stdlib.h>

#include "IR.h"
#include "Function.h"
#include "Scope.h"
#include "CSE.h"
#include "Random.h"
#include "Introspection.h"
#include "IRPrinter.h"
#include "IRMutator.h"
#include "ParallelRVar.h"
#include "Var.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;
using std::set;

struct FunctionContents {
    mutable RefCount ref_count;
    std::string name;
    std::vector<std::string> args;
    std::vector<Expr> values;
    std::vector<Type> output_types;
    Schedule schedule;

    std::vector<UpdateDefinition> updates;

    std::string debug_file;

    std::vector<Parameter> output_buffers;

    std::vector<ExternFuncArgument> extern_arguments;
    std::string extern_function_name;

    bool trace_loads, trace_stores, trace_realizations;

    bool frozen;

    FunctionContents() : trace_loads(false), trace_stores(false), trace_realizations(false), frozen(false) {}
};

template<>
EXPORT RefCount &ref_count<FunctionContents>(const FunctionContents *f) {
    return f->ref_count;
}

template<>
EXPORT void destroy<FunctionContents>(const FunctionContents *f) {
    delete f;
}

// All variables present in any part of a function definition must
// either be pure args, elements of the reduction domain, parameters
// (i.e. attached to some Parameter object), or part of a let node
// internal to the expression
struct CheckVars : public IRGraphVisitor {
    vector<string> pure_args;
    ReductionDomain reduction_domain;
    Scope<int> defined_internally;
    const std::string name;

    CheckVars(const std::string &n) :
        name(n) {}

    using IRVisitor::visit;

    void visit(const Let *let) {
        let->value.accept(this);
        defined_internally.push(let->name, 0);
        let->body.accept(this);
        defined_internally.pop(let->name);
    }

    void visit(const Call *op) {
        IRGraphVisitor::visit(op);
        if (op->name == name && op->call_type == Call::Halide) {
            for (size_t i = 0; i < op->args.size(); i++) {
                const Variable *var = op->args[i].as<Variable>();
                if (!pure_args[i].empty()) {
                    user_assert(var && var->name == pure_args[i])
                        << "In definition of Func \"" << name << "\":\n"
                        << "All of a functions recursive references to itself"
                        << " must contain the same pure variables in the same"
                        << " places as on the left-hand-side.\n";
                }
            }
        }
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
                user_error << "Multiple reduction domains found in definition of Func \"" << name << "\"\n";
            }
        }

        user_error << "Undefined variable \"" << var->name << "\" in definition of Func \"" << name << "\"\n";
    }
};

struct CountSelfReferences : public IRMutator {
    int count;
    const Function *func;

    using IRMutator::visit;

    void visit(const Call *c) {
        IRMutator::visit(c);
        c = expr.as<Call>();
        internal_assert(c);
        if (c->func.same_as(*func)) {
            expr = Call::make(c->type, c->name, c->args, c->call_type,
                              c->func, c->value_index,
                              c->image, c->param);
            count++;
        }
    }
};

// Mark all functions found in an expr as frozen.
class FreezeFunctions : public IRGraphVisitor {
    using IRGraphVisitor::visit;

    const string &func;

    void visit(const Call *op) {
        IRGraphVisitor::visit(op);
        if (op->call_type == Call::Halide && op->name != func) {
            Function f = op->func;
            f.freeze();
        }
    }
public:
    FreezeFunctions(const string &f) : func(f) {}
};

// A counter to use in tagging random variables
namespace {
static int rand_counter = 0;
}

Function::Function() : contents(new FunctionContents) {
}

Function::Function(const std::string &n) : contents(new FunctionContents) {
    for (size_t i = 0; i < n.size(); i++) {
        user_assert(n[i] != '.')
            << "Func name \"" << n << "\" is invalid. "
            << "Func names may not contain the character '.', "
            << "as it is used internally by Halide as a separator\n";
    }
    contents.ptr->name = n;
}

void Function::define(const vector<string> &args, vector<Expr> values) {
    user_assert(!frozen())
        << "Func " << name() << " cannot be given a new pure definition, "
        << "because it has already been realized or used in the definition of another Func.\n";
    user_assert(!has_extern_definition())
        << "In pure definition of Func \"" << name() << "\":\n"
        << "Func with extern definition cannot be given a pure definition.\n";
    user_assert(!name().empty()) << "A Func may not have an empty name.\n";
    for (size_t i = 0; i < values.size(); i++) {
        user_assert(values[i].defined())
            << "In pure definition of Func \"" << name() << "\":\n"
            << "Undefined expression in right-hand-side of definition.\n";
    }

    // Make sure all the vars in the value are either args or are
    // attached to some parameter
    CheckVars check(name());
    check.pure_args = args;
    for (size_t i = 0; i < values.size(); i++) {
        values[i].accept(&check);
    }

    // Freeze all called functions
    FreezeFunctions freezer(name());
    for (size_t i = 0; i < values.size(); i++) {
        values[i].accept(&freezer);
    }

    // Make sure all the vars in the args have unique non-empty names
    for (size_t i = 0; i < args.size(); i++) {
        user_assert(!args[i].empty())
            << "In pure definition of Func \"" << name() << "\":\n"
            << "In left-hand-side of definition, argument "
            << i << " has an empty name.\n";
        for (size_t j = 0; j < i; j++) {
            user_assert(args[i] != args[j])
                << "In pure definition of Func \"" << name() << "\":\n"
                << "In left-hand-side of definition, arguments "
                << i << " and " << j
                << " both have the name \"" + args[i] + "\"\n";
        }
    }

    for (size_t i = 0; i < values.size(); i++) {
        values[i] = common_subexpression_elimination(values[i]);
    }

    // Tag calls to random() with the free vars
    int tag = rand_counter++;
    for (size_t i = 0; i < values.size(); i++) {
        values[i] = lower_random(values[i], args, tag);
    }

    user_assert(!check.reduction_domain.defined())
        << "In pure definition of Func \"" << name() << "\":\n"
        << "Reduction domain referenced in pure function definition.\n";

    if (!contents.defined()) {
        contents = new FunctionContents;
        contents.ptr->name = unique_name('f');
    }

    user_assert(contents.ptr->values.empty())
        << "In pure definition of Func \"" << name() << "\":\n"
        << "Func is already defined.\n";

    contents.ptr->values = values;
    contents.ptr->args = args;

    contents.ptr->output_types.resize(values.size());
    for (size_t i = 0; i < contents.ptr->output_types.size(); i++) {
        contents.ptr->output_types[i] = values[i].type();
    }

    for (size_t i = 0; i < args.size(); i++) {
        Dim d = {args[i], ForType::Serial, DeviceAPI::Parent, true};
        contents.ptr->schedule.dims().push_back(d);
        contents.ptr->schedule.storage_dims().push_back(args[i]);
    }

    // Add the dummy outermost dim
    {
        Dim d = {Var::outermost().name(), ForType::Serial, DeviceAPI::Parent, true};
        contents.ptr->schedule.dims().push_back(d);
    }

    for (size_t i = 0; i < values.size(); i++) {
        string buffer_name = name();
        if (values.size() > 1) {
            buffer_name += '.' + int_to_string((int)i);
        }
        Parameter output(values[i].type(), true, args.size(), buffer_name);
        contents.ptr->output_buffers.push_back(output);
    }
}

void Function::define_update(const vector<Expr> &_args, vector<Expr> values) {
    user_assert(!name().empty())
        << "Func has an empty name.\n";
    user_assert(has_pure_definition())
        << "In update definition of Func \"" << name() << "\":\n"
        << "Can't add an update definition without a pure definition first.\n";
    user_assert(!frozen())
        << "Func " << name() << " cannot be given a new update definition, "
        << "because it has already been realized or used in the definition of another Func.\n";

    for (size_t i = 0; i < values.size(); i++) {
        user_assert(values[i].defined())
            << "In update definition of Func \"" << name() << "\":\n"
            << "Undefined expression in right-hand-side of update.\n";

    }

    // Check the dimensionality matches
    user_assert((int)_args.size() == dimensions())
        << "In update definition of Func \"" << name() << "\":\n"
        << "Dimensionality of update definition must match dimensionality of pure definition.\n";

    user_assert(values.size() == contents.ptr->values.size())
        << "In update definition of Func \"" << name() << "\":\n"
        << "Number of tuple elements for update definition must "
        << "match number of tuple elements for pure definition.\n";

    for (size_t i = 0; i < values.size(); i++) {
        // Check that pure value and the update value have the same
        // type.  Without this check, allocations may be the wrong size
        // relative to what update code expects.
        Type pure_type = contents.ptr->values[i].type();
        if (pure_type != values[i].type()) {
            std::ostringstream err;
            err << "In update definition of Func \"" << name() << "\":\n";
            if (values.size()) {
                err << "Tuple element " << i << " of update definition has type ";
            } else {
                err << "Update definition has type ";
            }
            err << values[i].type() << ", but pure definition has type " << pure_type;
            user_error << err.str() << "\n";
        }
        values[i] = common_subexpression_elimination(values[i]);
    }

    vector<Expr> args(_args.size());
    for (size_t i = 0; i < args.size(); i++) {
        args[i] = common_subexpression_elimination(_args[i]);
    }

    // The pure args are those naked vars in the args that are not in
    // a reduction domain and are not parameters and line up with the
    // pure args in the pure definition.
    bool pure = true;
    vector<string> pure_args(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        pure_args[i] = ""; // Will never match a var name
        user_assert(args[i].defined())
            << "In update definition of Func \"" << name() << "\":\n"
            << "Argument " << i
            << " in left-hand-side of update definition is undefined.\n";
        if (const Variable *var = args[i].as<Variable>()) {
            if (!var->param.defined() &&
                !var->reduction_domain.defined() &&
                var->name == contents.ptr->args[i]) {
                pure_args[i] = var->name;
            } else {
                pure = false;
            }
        } else {
            pure = false;
        }
    }

    // Make sure all the vars in the args and the value are either
    // pure args, in the reduction domain, or a parameter. Also checks
    // that recursive references to the function contain all the pure
    // vars in the LHS in the correct places.
    CheckVars check(name());
    check.pure_args = pure_args;
    for (size_t i = 0; i < args.size(); i++) {
        args[i].accept(&check);
    }
    for (size_t i = 0; i < values.size(); i++) {
        values[i].accept(&check);
    }

    // Freeze all called functions
    FreezeFunctions freezer(name());
    for (size_t i = 0; i < args.size(); i++) {
        args[i].accept(&freezer);
    }
    for (size_t i = 0; i < values.size(); i++) {
        values[i].accept(&freezer);
    }

    // Tag calls to random() with the free vars
    vector<string> free_vars;
    for (size_t i = 0; i < pure_args.size(); i++) {
        if (!pure_args[i].empty()) {
            free_vars.push_back(pure_args[i]);
        }
    }
    if (check.reduction_domain.defined()) {
        for (size_t i = 0; i < check.reduction_domain.domain().size(); i++) {
            string rvar = check.reduction_domain.domain()[i].var;
            free_vars.push_back(rvar);
        }
    }
    int tag = rand_counter++;
    for (size_t i = 0; i < args.size(); i++) {
        args[i] = lower_random(args[i], free_vars, tag);
    }
    for (size_t i = 0; i < values.size(); i++) {
        values[i] = lower_random(values[i], free_vars, tag);
    }

    UpdateDefinition r;
    r.args = args;
    r.values = values;
    r.domain = check.reduction_domain;
    r.schedule.set_reduction_domain(r.domain);

    // The update value and args probably refer back to the
    // function itself, introducing circular references and hence
    // memory leaks. We need to count the number of unique call nodes
    // that point back to this function in order to break the cycles.
    CountSelfReferences counter;
    counter.func = this;
    counter.count = 0;
    for (size_t i = 0; i < args.size(); i++) {
        r.args[i] = counter.mutate(r.args[i]);
    }
    for (size_t i = 0; i < values.size(); i++) {
        r.values[i] = counter.mutate(r.values[i]);
    }

    for (int i = 0; i < counter.count; i++) {
        contents.ptr->ref_count.decrement();
        internal_assert(!contents.ptr->ref_count.is_zero());
    }

    // First add any reduction domain
    if (r.domain.defined()) {
        for (size_t i = 0; i < r.domain.domain().size(); i++) {
            // Is this RVar actually pure (safe to parallelize and
            // reorder)? It's pure if one value of the RVar can never
            // access from the same memory that another RVar is
            // writing to.
            const string &v = r.domain.domain()[i].var;

            bool pure = can_parallelize_rvar(v, name(), r);

            Dim d = {v, ForType::Serial, DeviceAPI::Parent, pure};
            r.schedule.dims().push_back(d);
        }
    }

    // Then add the pure args outside of that
    for (size_t i = 0; i < pure_args.size(); i++) {
        if (!pure_args[i].empty()) {
            Dim d = {pure_args[i], ForType::Serial, DeviceAPI::Parent, true};
            r.schedule.dims().push_back(d);
        }
    }

    // Then the dummy outermost dim
    {
        Dim d = {Var::outermost().name(), ForType::Serial, DeviceAPI::Parent, true};
        r.schedule.dims().push_back(d);
    }

    // If there's no recursive reference, no reduction domain, and all
    // the args are pure, then this definition completely hides
    // earlier ones!
    if (!r.domain.defined() &&
        counter.count == 0 &&
        pure) {
        user_warning
            << "In update definition of Func \"" << name() << "\":\n"
            << "Update definition completely hides earlier definitions, "
            << " because all the arguments are pure, it contains no self-references, "
            << " and no reduction domain. This may be an accidental re-definition of "
            << " an already-defined function.\n";
    }

    contents.ptr->updates.push_back(r);

}

void Function::define_extern(const std::string &function_name,
                             const std::vector<ExternFuncArgument> &args,
                             const std::vector<Type> &types,
                             int dimensionality) {

    user_assert(!has_pure_definition() && !has_update_definition())
        << "In extern definition for Func \"" << name() << "\":\n"
        << "Func with a pure definition cannot have an extern definition.\n";

    user_assert(!has_extern_definition())
        << "In extern definition for Func \"" << name() << "\":\n"
        << "Func already has an extern definition.\n";

    contents.ptr->extern_function_name = function_name;
    contents.ptr->extern_arguments = args;
    contents.ptr->output_types = types;

    for (size_t i = 0; i < types.size(); i++) {
        string buffer_name = name();
        if (types.size() > 1) {
            buffer_name += '.' + int_to_string((int)i);
        }
        Parameter output(types[i], true, dimensionality, buffer_name);
        contents.ptr->output_buffers.push_back(output);
    }

    // Make some synthetic var names for scheduling purposes (e.g. reorder_storage).
    contents.ptr->args.resize(dimensionality);
    for (int i = 0; i < dimensionality; i++) {
        string arg = unique_name('e');
        contents.ptr->args[i] = arg;
        contents.ptr->schedule.storage_dims().push_back(arg);
    }

}

const std::string &Function::name() const {
    return contents.ptr->name;
}

const std::vector<std::string> &Function::args() const {
    return contents.ptr->args;
}

const std::vector<Type> &Function::output_types() const {
    return contents.ptr->output_types;
}

const std::vector<Expr> &Function::values() const {
    return contents.ptr->values;
}

Schedule &Function::schedule() {
    return contents.ptr->schedule;
}

const Schedule &Function::schedule() const {
    return contents.ptr->schedule;
}

const std::vector<Parameter> &Function::output_buffers() const {
    return contents.ptr->output_buffers;
}

Schedule &Function::update_schedule(int idx) {
    return contents.ptr->updates[idx].schedule;
}

const std::vector<UpdateDefinition> &Function::updates() const {
    return contents.ptr->updates;
}

bool Function::has_update_definition() const {
    return !contents.ptr->updates.empty();
}

bool Function::has_extern_definition() const {
    return !contents.ptr->extern_function_name.empty();
}

const std::vector<ExternFuncArgument> &Function::extern_arguments() const {
    return contents.ptr->extern_arguments;
}

const std::string &Function::extern_function_name() const {
    return contents.ptr->extern_function_name;
}

const std::string &Function::debug_file() const {
    return contents.ptr->debug_file;
}

std::string &Function::debug_file() {
    return contents.ptr->debug_file;
}

void Function::trace_loads() {
    contents.ptr->trace_loads = true;
}
void Function::trace_stores() {
    contents.ptr->trace_stores = true;
}
void Function::trace_realizations() {
    contents.ptr->trace_realizations = true;
}
bool Function::is_tracing_loads() const {
    return contents.ptr->trace_loads;
}
bool Function::is_tracing_stores() const {
    return contents.ptr->trace_stores;
}
bool Function::is_tracing_realizations() const {
    return contents.ptr->trace_realizations;
}

void Function::freeze() {
    contents.ptr->frozen = true;
}

bool Function::frozen() const {
    return contents.ptr->frozen;
}

}
}
