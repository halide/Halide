#include <atomic>
#include <cstdlib>
#include <memory>
#include <set>
#include <utility>

#include "CSE.h"
#include "Func.h"
#include "Function.h"
#include "IR.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "ParallelRVar.h"
#include "Random.h"
#include "Scope.h"
#include "Var.h"

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::string;
using std::vector;

typedef map<FunctionPtr, FunctionPtr> DeepCopyMap;

struct FunctionContents;

namespace {

// Weaken all the references to a particular Function to break
// reference cycles. Also count the number of references found.
class WeakenFunctionPtrs : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Call *c) override {
        Expr expr = IRMutator::visit(c);
        c = expr.as<Call>();
        internal_assert(c);
        if (c->func.defined() &&
            c->func.get() == func) {
            FunctionPtr ptr = c->func;
            ptr.weaken();
            expr = Call::make(c->type, c->name, c->args, c->call_type,
                              ptr, c->value_index,
                              c->image, c->param);
            count++;
        }
        return expr;
    }
    FunctionContents *func;

public:
    int count = 0;
    WeakenFunctionPtrs(FunctionContents *f)
        : func(f) {
    }
};

}  // namespace

struct FunctionContents {
    std::string name;
    std::string origin_name;
    std::vector<Type> output_types;

    /** Optional type constraints on the Function:
     * - If empty, there are no constraints.
     * - If size == 1, the Func is only allowed to have values of Expr with that type
     * - If size > 1, the Func is only allowed to have values of Tuple with those types
     *
     * Note that when this is nonempty, then output_types should match
     * required_types for all defined Functions.
     */
    std::vector<Type> required_types;

    /** Optional dimension constraints on the Function:
     * - If required_dims == AnyDims, there are no constraints.
     * - Otherwise, the Function's dimensionality must exactly match required_dims.
     */
    int required_dims = AnyDims;

    // The names of the dimensions of the Function. Corresponds to the
    // LHS of the pure definition if there is one. Is also the initial
    // stage of the dims and storage_dims. Used to identify dimensions
    // of the Function by name.
    std::vector<string> args;

    // Function-specific schedule. This schedule is applied to all stages
    // within the function.
    FuncSchedule func_schedule;

    Definition init_def;
    std::vector<Definition> updates;

    std::string debug_file;

    std::vector<Parameter> output_buffers;

    std::vector<ExternFuncArgument> extern_arguments;
    std::string extern_function_name;

    NameMangling extern_mangling = NameMangling::Default;
    DeviceAPI extern_function_device_api = DeviceAPI::Host;
    Expr extern_proxy_expr;

    bool trace_loads = false, trace_stores = false, trace_realizations = false;
    std::vector<string> trace_tags;

    bool frozen = false;

    void accept(IRVisitor *visitor) const {
        func_schedule.accept(visitor);

        if (init_def.defined()) {
            init_def.accept(visitor);
        }
        for (const Definition &def : updates) {
            def.accept(visitor);
        }

        if (!extern_function_name.empty()) {
            for (const ExternFuncArgument &i : extern_arguments) {
                if (i.is_func()) {
                    user_assert(i.func.get() != this)
                        << "Extern Func has itself as an argument";
                    i.func->accept(visitor);
                } else if (i.is_expr()) {
                    i.expr.accept(visitor);
                }
            }
            if (extern_proxy_expr.defined()) {
                extern_proxy_expr.accept(visitor);
            }
        }

        for (const Parameter &i : output_buffers) {
            for (size_t j = 0; j < args.size(); j++) {
                if (i.min_constraint(j).defined()) {
                    i.min_constraint(j).accept(visitor);
                }
                if (i.stride_constraint(j).defined()) {
                    i.stride_constraint(j).accept(visitor);
                }
                if (i.extent_constraint(j).defined()) {
                    i.extent_constraint(j).accept(visitor);
                }
            }
        }
    }

    // Pass an IRMutator through to all Exprs referenced in the FunctionContents
    void mutate(IRMutator *mutator) {
        func_schedule.mutate(mutator);

        if (init_def.defined()) {
            init_def.mutate(mutator);
        }
        for (Definition &def : updates) {
            def.mutate(mutator);
        }

        if (!extern_function_name.empty()) {
            for (ExternFuncArgument &i : extern_arguments) {
                if (i.is_expr()) {
                    i.expr = mutator->mutate(i.expr);
                }
            }
            extern_proxy_expr = mutator->mutate(extern_proxy_expr);
        }
    }
};

struct FunctionGroup {
    mutable RefCount ref_count;
    vector<FunctionContents> members;
};

FunctionContents *FunctionPtr::get() const {
    return &(group()->members[idx]);
}

template<>
RefCount &ref_count<FunctionGroup>(const FunctionGroup *f) noexcept {
    return f->ref_count;
}

template<>
void destroy<FunctionGroup>(const FunctionGroup *f) {
    delete f;
}

namespace {

// All variables present in any part of a function definition must
// either be pure args, elements of the reduction domain, parameters
// (i.e. attached to some Parameter object), or part of a let node
// internal to the expression
struct CheckVars : public IRGraphVisitor {
    vector<string> pure_args;
    ReductionDomain reduction_domain;
    Scope<> defined_internally;
    const std::string name;
    bool unbound_reduction_vars_ok = false;

    CheckVars(const std::string &n)
        : name(n) {
    }

    using IRVisitor::visit;

    void visit(const Let *let) override {
        let->value.accept(this);
        ScopedBinding<> bind(defined_internally, let->name);
        let->body.accept(this);
    }

    void visit(const Call *op) override {
        IRGraphVisitor::visit(op);
        if (op->name == name && op->call_type == Call::Halide) {
            for (size_t i = 0; i < op->args.size(); i++) {
                const Variable *var = op->args[i].as<Variable>();
                if (!pure_args[i].empty()) {
                    user_assert(var && var->name == pure_args[i])
                        << "In definition of Func \"" << name << "\":\n"
                        << "All of a function's recursive references to itself"
                        << " must contain the same pure variables in the same"
                        << " places as on the left-hand-side.\n";
                }
            }
        }
    }

    void visit(const Variable *var) override {
        // Is it a parameter?
        if (var->param.defined()) {
            return;
        }

        // Was it defined internally by a let expression?
        if (defined_internally.contains(var->name)) {
            return;
        }

        // Is it a pure argument?
        for (auto &pure_arg : pure_args) {
            if (var->name == pure_arg) {
                return;
            }
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
        } else if (reduction_domain.defined() && unbound_reduction_vars_ok) {
            // Is it one of the RVars from the reduction domain we already
            // know about (this can happen in the RDom predicate).
            for (const ReductionVariable &rv : reduction_domain.domain()) {
                if (rv.var == var->name) {
                    return;
                }
            }
        }

        user_error << "Undefined variable \"" << var->name << "\" in definition of Func \"" << name << "\"\n";
    }
};

// Mark all functions found in an expr as frozen.
class FreezeFunctions : public IRGraphVisitor {
    using IRGraphVisitor::visit;

    const string &func;

    void visit(const Call *op) override {
        IRGraphVisitor::visit(op);
        if (op->call_type == Call::Halide &&
            op->func.defined() &&
            op->name != func) {
            Function f(op->func);
            f.freeze();
        }
    }

public:
    FreezeFunctions(const string &f)
        : func(f) {
    }
};

// A counter to use in tagging random variables
std::atomic<int> rand_counter{0};

}  // namespace

Function::Function(const FunctionPtr &ptr)
    : contents(ptr) {
    contents.strengthen();
    internal_assert(ptr.defined())
        << "Can't construct Function from undefined FunctionContents ptr\n";
}

Function::Function(const std::string &n) {
    for (size_t i = 0; i < n.size(); i++) {
        user_assert(n[i] != '.')
            << "Func name \"" << n << "\" is invalid. "
            << "Func names may not contain the character '.', "
            << "as it is used internally by Halide as a separator\n";
    }
    contents.strong = new FunctionGroup;
    contents.strong->members.resize(1);
    contents->name = n;
    contents->origin_name = n;
}

Function::Function(const std::vector<Type> &required_types, int required_dims, const std::string &n)
    : Function(n) {
    user_assert(required_dims >= AnyDims);
    contents->required_types = required_types;
    contents->required_dims = required_dims;
}

namespace {

template<typename T>
struct PrintTypeList {
    const std::vector<T> &list_;

    explicit PrintTypeList(const std::vector<T> &list)
        : list_(list) {
    }

    friend std::ostream &operator<<(std::ostream &s, const PrintTypeList &self) {
        const size_t n = self.list_.size();
        if (n != 1) {
            s << "(";
        }
        const char *comma = "";
        for (const auto &t : self.list_) {
            if constexpr (std::is_same<Type, T>::value) {
                s << comma << t;
            } else {
                s << comma << t.type();
            }
            comma = ", ";
        }
        if (n != 1) {
            s << ")";
        }
        return s;
    }
};

bool types_match(const std::vector<Type> &types, const std::vector<Expr> &exprs) {
    size_t n = types.size();
    if (n != exprs.size()) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (types[i] != exprs[i].type()) {
            return false;
        }
    }
    return true;
}

}  // namespace

void Function::check_types(const Expr &e) const {
    check_types(std::vector<Expr>{e});
}

void Function::check_types(const Tuple &t) const {
    check_types(t.as_vector());
}

void Function::check_types(const Type &t) const {
    check_types(std::vector<Type>{t});
}

void Function::check_types(const std::vector<Expr> &exprs) const {
    if (!contents->required_types.empty()) {
        user_assert(types_match(contents->required_types, exprs))
            << "Func \"" << name() << "\" is constrained to only hold values of type " << PrintTypeList(contents->required_types)
            << " but is defined with values of type " << PrintTypeList(exprs) << ".\n";
    }
}

void Function::check_types(const std::vector<Type> &types) const {
    if (!contents->required_types.empty()) {
        user_assert(contents->required_types == types)
            << "Func \"" << name() << "\" is constrained to only hold values of type " << PrintTypeList(contents->required_types)
            << " but is defined with values of type " << PrintTypeList(types) << ".\n";
    }
}

void Function::check_dims(int dims) const {
    if (contents->required_dims != AnyDims) {
        user_assert(contents->required_dims == dims)
            << "Func \"" << name() << "\" is constrained to have exactly " << contents->required_dims
            << " dimensions, but is defined with " << dims << " dimensions.\n";
    }
}

namespace {

// Return deep-copy of ExternFuncArgument 'src'
ExternFuncArgument deep_copy_extern_func_argument_helper(const ExternFuncArgument &src,
                                                         DeepCopyMap &copied_map) {
    ExternFuncArgument copy;
    copy.arg_type = src.arg_type;
    copy.buffer = src.buffer;
    copy.expr = src.expr;
    copy.image_param = src.image_param;

    if (!src.func.defined()) {  // No need to deep-copy the func if it's undefined
        internal_assert(!src.is_func())
            << "ExternFuncArgument has type FuncArg but has no function definition\n";
        return copy;
    }

    // If the FunctionContents has already been deep-copied previously, i.e.
    // it's in the 'copied_map', use the deep-copied version from the map instead
    // of creating a new deep-copy
    FunctionPtr &copied_func = copied_map[src.func];
    internal_assert(copied_func.defined());
    copy.func = copied_func;
    return copy;
}

}  // namespace

void Function::deep_copy(const FunctionPtr &copy, DeepCopyMap &copied_map) const {
    internal_assert(copy.defined() && contents.defined())
        << "Cannot deep-copy undefined Function\n";

    // Add reference to this Function's deep-copy to the map in case of
    // self-reference, e.g. self-reference in an Definition.
    copied_map[contents] = copy;

    debug(4) << "Deep-copy function contents: \"" << contents->name << "\"\n";

    copy->name = contents->name;
    copy->origin_name = contents->origin_name;
    copy->args = contents->args;
    copy->output_types = contents->output_types;
    copy->debug_file = contents->debug_file;
    copy->extern_function_name = contents->extern_function_name;
    copy->extern_mangling = contents->extern_mangling;
    copy->extern_function_device_api = contents->extern_function_device_api;
    copy->extern_proxy_expr = contents->extern_proxy_expr;
    copy->trace_loads = contents->trace_loads;
    copy->trace_stores = contents->trace_stores;
    copy->trace_realizations = contents->trace_realizations;
    copy->trace_tags = contents->trace_tags;
    copy->frozen = contents->frozen;
    copy->output_buffers = contents->output_buffers;
    copy->func_schedule = contents->func_schedule.deep_copy(copied_map);

    // Copy the pure definition
    if (contents->init_def.defined()) {
        copy->init_def = contents->init_def.get_copy();
        internal_assert(copy->init_def.is_init());
        internal_assert(copy->init_def.schedule().rvars().empty())
            << "Init definition shouldn't have reduction domain\n";
    }

    for (const Definition &def : contents->updates) {
        internal_assert(!def.is_init());
        Definition def_copy = def.get_copy();
        internal_assert(!def_copy.is_init());
        copy->updates.push_back(std::move(def_copy));
    }

    for (const ExternFuncArgument &e : contents->extern_arguments) {
        ExternFuncArgument e_copy = deep_copy_extern_func_argument_helper(e, copied_map);
        copy->extern_arguments.push_back(std::move(e_copy));
    }
}

void Function::deep_copy(string name, const FunctionPtr &copy, DeepCopyMap &copied_map) const {
    deep_copy(copy, copied_map);
    copy->name = std::move(name);
}

void Function::define(const vector<string> &args, vector<Expr> values) {
    user_assert(!frozen())
        << "Func " << name() << " cannot be given a new pure definition, "
        << "because it has already been realized or used in the definition of another Func.\n";
    user_assert(!has_extern_definition())
        << "In pure definition of Func \"" << name() << "\":\n"
        << "Func with extern definition cannot be given a pure definition.\n";
    user_assert(!name().empty()) << "A Func may not have an empty name.\n";
    for (auto &value : values) {
        user_assert(value.defined())
            << "In pure definition of Func \"" << name() << "\":\n"
            << "Undefined expression in right-hand-side of definition.\n";
    }

    // Make sure all the vars in the value are either args or are
    // attached to some parameter
    CheckVars check(name());
    check.pure_args = args;
    for (const auto &value : values) {
        value.accept(&check);
    }

    // Freeze all called functions
    FreezeFunctions freezer(name());
    for (const auto &value : values) {
        value.accept(&freezer);
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

    for (auto &value : values) {
        value = common_subexpression_elimination(value);
    }

    // Tag calls to random() with the free vars
    int tag = rand_counter++;
    vector<VarOrRVar> free_vars;
    free_vars.reserve(args.size());
    for (const auto &arg : args) {
        free_vars.emplace_back(Var(arg));
    }
    for (auto &value : values) {
        value = lower_random(value, free_vars, tag);
    }

    user_assert(!check.reduction_domain.defined())
        << "In pure definition of Func \"" << name() << "\":\n"
        << "Reduction domain referenced in pure function definition.\n";

    if (!contents.defined()) {
        contents.strong = new FunctionGroup;
        contents.strong->members.resize(1);
        contents->name = unique_name('f');
        contents->origin_name = contents->name;
    }

    user_assert(!contents->init_def.defined())
        << "In pure definition of Func \"" << name() << "\":\n"
        << "Func is already defined.\n";

    check_types(values);
    check_dims((int)args.size());
    contents->args = args;

    std::vector<Expr> init_def_args;
    init_def_args.resize(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        init_def_args[i] = Var(args[i]);
    }

    ReductionDomain rdom;
    contents->init_def = Definition(init_def_args, values, rdom, true);

    for (const auto &arg : args) {
        Dim d = {arg, ForType::Serial, DeviceAPI::None, DimType::PureVar};
        contents->init_def.schedule().dims().push_back(d);
        StorageDim sd = {arg};
        contents->func_schedule.storage_dims().push_back(sd);
    }

    // Add the dummy outermost dim
    {
        Dim d = {Var::outermost().name(), ForType::Serial, DeviceAPI::None, DimType::PureVar};
        contents->init_def.schedule().dims().push_back(d);
    }

    contents->output_types.resize(values.size());
    for (size_t i = 0; i < contents->output_types.size(); i++) {
        contents->output_types[i] = values[i].type();
    }

    if (!contents->required_types.empty()) {
        // Just a reality check; mismatches here really should have been caught earlier
        internal_assert(contents->required_types == contents->output_types);
    }
    if (contents->required_dims != AnyDims) {
        // Just a reality check; mismatches here really should have been caught earlier
        internal_assert(contents->required_dims == (int)args.size());
    }

    if (contents->output_buffers.empty()) {
        create_output_buffers(contents->output_types, (int)args.size());
    }
}

void Function::create_output_buffers(const std::vector<Type> &types, int dims) const {
    internal_assert(contents->output_buffers.empty());
    internal_assert(!types.empty() && dims != AnyDims);

    for (size_t i = 0; i < types.size(); i++) {
        string buffer_name = name();
        if (types.size() > 1) {
            buffer_name += '.' + std::to_string((int)i);
        }
        Parameter output(types[i], true, dims, buffer_name);
        contents->output_buffers.push_back(output);
    }
}

void Function::define_update(const vector<Expr> &_args, vector<Expr> values) {
    int update_idx = static_cast<int>(contents->updates.size());

    user_assert(!name().empty())
        << "Func has an empty name.\n";
    user_assert(has_pure_definition())
        << "In update definition " << update_idx << " of Func \"" << name() << "\":\n"
        << "Can't add an update definition without a pure definition first.\n";
    user_assert(!frozen())
        << "Func " << name() << " cannot be given a new update definition, "
        << "because it has already been realized or used in the definition of another Func.\n";

    for (auto &value : values) {
        user_assert(value.defined())
            << "In update definition " << update_idx << " of Func \"" << name() << "\":\n"
            << "Undefined expression in right-hand-side of update.\n";
    }

    // Check the dimensionality matches
    user_assert((int)_args.size() == dimensions())
        << "In update definition " << update_idx << " of Func \"" << name() << "\":\n"
        << "Dimensionality of update definition must match dimensionality of pure definition.\n";

    user_assert(values.size() == contents->init_def.values().size())
        << "In update definition " << update_idx << " of Func \"" << name() << "\":\n"
        << "Number of tuple elements for update definition must "
        << "match number of tuple elements for pure definition.\n";

    const auto &pure_def_vals = contents->init_def.values();
    for (size_t i = 0; i < values.size(); i++) {
        // Check that pure value and the update value have the same
        // type.  Without this check, allocations may be the wrong size
        // relative to what update code expects.
        Type pure_type = pure_def_vals[i].type();
        if (pure_type != values[i].type()) {
            std::ostringstream err;
            err << "In update definition " << update_idx << " of Func \"" << name() << "\":\n";
            if (!values.empty()) {
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
        pure_args[i] = "";  // Will never match a var name
        user_assert(args[i].defined())
            << "In update definition " << update_idx << " of Func \"" << name() << "\":\n"
            << "Argument " << i
            << " in left-hand-side of update definition is undefined.\n";
        if (const Variable *var = args[i].as<Variable>()) {
            if (!var->param.defined() &&
                !var->reduction_domain.defined() &&
                var->name == contents->args[i]) {
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
    for (const auto &arg : args) {
        arg.accept(&check);
    }
    for (const auto &value : values) {
        value.accept(&check);
    }
    if (check.reduction_domain.defined()) {
        check.unbound_reduction_vars_ok = true;
        check.reduction_domain.predicate().accept(&check);
    }

    // Freeze all called functions
    FreezeFunctions freezer(name());
    for (const auto &arg : args) {
        arg.accept(&freezer);
    }
    for (const auto &value : values) {
        value.accept(&freezer);
    }

    // Freeze the reduction domain if defined
    if (check.reduction_domain.defined()) {
        check.reduction_domain.predicate().accept(&freezer);
        check.reduction_domain.freeze();
    }

    // Tag calls to random() with the free vars
    vector<VarOrRVar> free_vars;
    int num_free_vars = (int)pure_args.size();
    if (check.reduction_domain.defined()) {
        num_free_vars += (int)check.reduction_domain.domain().size();
    }
    free_vars.reserve(num_free_vars);
    for (const auto &pure_arg : pure_args) {
        if (!pure_arg.empty()) {
            free_vars.emplace_back(Var(pure_arg));
        }
    }
    if (check.reduction_domain.defined()) {
        for (size_t i = 0; i < check.reduction_domain.domain().size(); i++) {
            free_vars.emplace_back(RVar(check.reduction_domain, i));
        }
    }
    int tag = rand_counter++;
    for (auto &arg : args) {
        arg = lower_random(arg, free_vars, tag);
    }
    for (auto &value : values) {
        value = lower_random(value, free_vars, tag);
    }
    if (check.reduction_domain.defined()) {
        check.reduction_domain.set_predicate(lower_random(check.reduction_domain.predicate(), free_vars, tag));
    }

    // The update value and args probably refer back to the
    // function itself, introducing circular references and hence
    // memory leaks. We need to break these cycles.
    WeakenFunctionPtrs weakener(contents.get());
    for (auto &arg : args) {
        arg = weakener.mutate(arg);
    }
    for (auto &value : values) {
        value = weakener.mutate(value);
    }
    if (check.reduction_domain.defined()) {
        check.reduction_domain.set_predicate(
            weakener.mutate(check.reduction_domain.predicate()));
    }

    Definition r(args, values, check.reduction_domain, false);
    internal_assert(!r.is_init()) << "Should have been an update definition\n";

    // First add any reduction domain
    if (check.reduction_domain.defined()) {
        for (const auto &rvar : check.reduction_domain.domain()) {
            // Is this RVar actually pure (safe to parallelize and
            // reorder)? It's pure if one value of the RVar can never
            // access from the same memory that another RVar is
            // writing to.
            const string &v = rvar.var;

            bool pure = can_parallelize_rvar(v, name(), r);
            Dim d = {v, ForType::Serial, DeviceAPI::None,
                     pure ? DimType::PureRVar : DimType::ImpureRVar};
            r.schedule().dims().push_back(d);
        }
    }

    // Then add the pure args outside of that
    for (const auto &pure_arg : pure_args) {
        if (!pure_arg.empty()) {
            Dim d = {pure_arg, ForType::Serial, DeviceAPI::None, DimType::PureVar};
            r.schedule().dims().push_back(d);
        }
    }

    // Then the dummy outermost dim
    {
        Dim d = {Var::outermost().name(), ForType::Serial, DeviceAPI::None, DimType::PureVar};
        r.schedule().dims().push_back(d);
    }

    // If there's no recursive reference, no reduction domain, and all
    // the args are pure, then this definition completely hides
    // earlier ones!
    if (!check.reduction_domain.defined() &&
        weakener.count == 0 &&
        pure) {
        user_warning
            << "In update definition " << update_idx << " of Func \"" << name() << "\":\n"
            << "Update definition completely hides earlier definitions, "
            << " because all the arguments are pure, it contains no self-references, "
            << " and no reduction domain. This may be an accidental re-definition of "
            << " an already-defined function.\n";
    }

    contents->updates.push_back(r);
}

void Function::define_extern(const std::string &function_name,
                             const std::vector<ExternFuncArgument> &extern_args,
                             const std::vector<Type> &types,
                             const std::vector<Var> &args,
                             NameMangling mangling,
                             DeviceAPI device_api) {
    check_types(types);
    check_dims((int)args.size());

    user_assert(!has_pure_definition() && !has_update_definition())
        << "In extern definition for Func \"" << name() << "\":\n"
        << "Func with a pure definition cannot have an extern definition.\n";

    user_assert(!has_extern_definition())
        << "In extern definition for Func \"" << name() << "\":\n"
        << "Func already has an extern definition.\n";

    std::vector<string> arg_names;
    std::vector<Expr> arg_exprs;
    for (const auto &arg : args) {
        arg_names.push_back(arg.name());
        arg_exprs.push_back(arg);
    }
    contents->args = arg_names;
    contents->extern_function_name = function_name;
    contents->extern_arguments = extern_args;
    contents->output_types = types;
    contents->extern_mangling = mangling;
    contents->extern_function_device_api = device_api;

    std::vector<Expr> values;
    contents->output_buffers.clear();
    for (size_t i = 0; i < types.size(); i++) {
        string buffer_name = name();
        if (types.size() > 1) {
            buffer_name += '.' + std::to_string((int)i);
        }
        Parameter output(types[i], true, (int)args.size(), buffer_name);
        contents->output_buffers.push_back(output);

        values.push_back(undef(types[i]));
    }

    contents->init_def = Definition(arg_exprs, values, ReductionDomain(), true);

    // Reset the storage dims to match the pure args
    contents->func_schedule.storage_dims().clear();
    contents->init_def.schedule().dims().clear();
    for (size_t i = 0; i < args.size(); i++) {
        contents->func_schedule.storage_dims().push_back(StorageDim{arg_names[i]});
        contents->init_def.schedule().dims().push_back(
            Dim{arg_names[i], ForType::Extern, DeviceAPI::None, DimType::PureVar});
    }
    // Add the dummy outermost dim
    contents->init_def.schedule().dims().push_back(
        Dim{Var::outermost().name(), ForType::Serial, DeviceAPI::None, DimType::PureVar});
}

void Function::accept(IRVisitor *visitor) const {
    contents->accept(visitor);
}

void Function::mutate(IRMutator *mutator) {
    contents->mutate(mutator);
}

const std::string &Function::name() const {
    return contents->name;
}

const std::string &Function::origin_name() const {
    return contents->origin_name;
}

Definition &Function::definition() {
    internal_assert(contents->init_def.defined());
    return contents->init_def;
}

const Definition &Function::definition() const {
    internal_assert(contents->init_def.defined());
    return contents->init_def;
}

const std::vector<std::string> &Function::args() const {
    return contents->args;
}

bool Function::is_pure_arg(const std::string &name) const {
    return std::find(args().begin(), args().end(), name) != args().end();
}

int Function::dimensions() const {
    return (int)args().size();
}

int Function::outputs() const {
    return (int)output_types().size();
}

const std::vector<Type> &Function::output_types() const {
    return contents->output_types;
}

const std::vector<Type> &Function::required_types() const {
    return contents->required_types;
}

int Function::required_dimensions() const {
    return contents->required_dims;
}

const std::vector<Expr> &Function::values() const {
    static const std::vector<Expr> empty;
    if (has_pure_definition()) {
        return contents->init_def.values();
    } else {
        return empty;
    }
}

FuncSchedule &Function::schedule() {
    return contents->func_schedule;
}

const FuncSchedule &Function::schedule() const {
    return contents->func_schedule;
}

const std::vector<Parameter> &Function::output_buffers() const {
    if (!contents->output_buffers.empty()) {
        return contents->output_buffers;
    }

    // If types and dims are already specified, we can go ahead and create
    // the output buffer(s) even if the Function has no pure definition yet.
    if (!contents->required_types.empty() && contents->required_dims != AnyDims) {
        create_output_buffers(contents->required_types, contents->required_dims);
        return contents->output_buffers;
    }

    user_error << "Can't access output buffer(s) of undefined Func \"" << name() << "\".\n";
    return contents->output_buffers;
}

StageSchedule &Function::update_schedule(int idx) {
    internal_assert(idx < (int)contents->updates.size()) << "Invalid update definition index\n";
    return contents->updates[idx].schedule();
}

Definition &Function::update(int idx) {
    internal_assert(idx < (int)contents->updates.size()) << "Invalid update definition index\n";
    return contents->updates[idx];
}

const Definition &Function::update(int idx) const {
    internal_assert(idx < (int)contents->updates.size()) << "Invalid update definition index\n";
    return contents->updates[idx];
}

const std::vector<Definition> &Function::updates() const {
    return contents->updates;
}

bool Function::has_pure_definition() const {
    return contents->init_def.defined();
}

bool Function::can_be_inlined() const {
    return is_pure() && definition().specializations().empty();
}

bool Function::has_update_definition() const {
    return !contents->updates.empty();
}

bool Function::has_extern_definition() const {
    return !contents->extern_function_name.empty();
}

NameMangling Function::extern_definition_name_mangling() const {
    return contents->extern_mangling;
}

Expr Function::make_call_to_extern_definition(const std::vector<Expr> &args,
                                              const Target &target) const {
    internal_assert(has_extern_definition());

    Call::CallType call_type = Call::Extern;
    switch (contents->extern_mangling) {
    case NameMangling::Default:
        call_type = (target.has_feature(Target::CPlusPlusMangling) ? Call::ExternCPlusPlus : Call::Extern);
        break;
    case NameMangling::CPlusPlus:
        call_type = Call::ExternCPlusPlus;
        break;
    case NameMangling::C:
        call_type = Call::Extern;
        break;
    }
    return Call::make(Int(32), contents->extern_function_name, args, call_type, contents);
}

Expr Function::extern_definition_proxy_expr() const {
    return contents->extern_proxy_expr;
}

Expr &Function::extern_definition_proxy_expr() {
    return contents->extern_proxy_expr;
}

const std::vector<ExternFuncArgument> &Function::extern_arguments() const {
    return contents->extern_arguments;
}

std::vector<ExternFuncArgument> &Function::extern_arguments() {
    return contents->extern_arguments;
}

const std::string &Function::extern_function_name() const {
    return contents->extern_function_name;
}

DeviceAPI Function::extern_function_device_api() const {
    return contents->extern_function_device_api;
}

const std::string &Function::debug_file() const {
    return contents->debug_file;
}

std::string &Function::debug_file() {
    return contents->debug_file;
}

Function::operator ExternFuncArgument() const {
    return ExternFuncArgument(contents);
}

void Function::trace_loads() {
    contents->trace_loads = true;
}
void Function::trace_stores() {
    contents->trace_stores = true;
}
void Function::trace_realizations() {
    contents->trace_realizations = true;
}
void Function::add_trace_tag(const std::string &trace_tag) {
    contents->trace_tags.push_back(trace_tag);
}

bool Function::is_tracing_loads() const {
    return contents->trace_loads;
}
bool Function::is_tracing_stores() const {
    return contents->trace_stores;
}
bool Function::is_tracing_realizations() const {
    return contents->trace_realizations;
}
const std::vector<std::string> &Function::get_trace_tags() const {
    return contents->trace_tags;
}

void Function::freeze() {
    contents->frozen = true;
}

void Function::lock_loop_levels() {
    auto &schedule = contents->func_schedule;
    schedule.compute_level().lock();
    schedule.store_level().lock();
    // If store_level is inlined, use the compute_level instead.
    // (Note that we deliberately do *not* do the same if store_level
    // is undefined.)
    if (schedule.store_level().is_inlined()) {
        schedule.store_level() = schedule.compute_level();
    }
    if (contents->init_def.defined()) {
        contents->init_def.schedule().fuse_level().level.lock();
    }
    for (Definition &def : contents->updates) {
        internal_assert(def.defined());
        def.schedule().fuse_level().level.lock();
    }
}

bool Function::frozen() const {
    return contents->frozen;
}

const map<string, FunctionPtr> &Function::wrappers() const {
    return contents->func_schedule.wrappers();
}

Function Function::new_function_in_same_group(const std::string &f) {
    int group_size = (int)(contents.group()->members.size());
    contents.group()->members.resize(group_size + 1);
    contents.group()->members[group_size].name = f;
    FunctionPtr ptr;
    ptr.strong = contents.group();
    ptr.idx = group_size;
    return Function(ptr);
}

void Function::add_wrapper(const std::string &f, Function &wrapper) {
    wrapper.freeze();
    FunctionPtr ptr = wrapper.contents;

    // Weaken the pointer from the function to its wrapper
    ptr.weaken();
    contents->func_schedule.add_wrapper(f, ptr);

    // Weaken the pointer from the wrapper back to the function.
    WeakenFunctionPtrs weakener(contents.get());
    wrapper.mutate(&weakener);
}

const Call *Function::is_wrapper() const {
    const vector<Expr> &rhs = values();
    if (rhs.size() != 1) {
        return nullptr;
    }
    const Call *call = rhs[0].as<Call>();
    if (!call) {
        return nullptr;
    }
    vector<Expr> expected_args;
    for (const string &v : args()) {
        expected_args.push_back(Variable::make(Int(32), v));
    }
    Expr expected_rhs =
        Call::make(call->type, call->name, expected_args, call->call_type,
                   call->func, call->value_index, call->image, call->param);
    if (equal(rhs[0], expected_rhs)) {
        return call;
    } else {
        return nullptr;
    }
}

namespace {

// Replace all calls to functions listed in 'substitutions' with their wrappers.
class SubstituteCalls : public IRMutator {
    using IRMutator::visit;

    const map<FunctionPtr, FunctionPtr> &substitutions;

    Expr visit(const Call *c) override {
        Expr expr = IRMutator::visit(c);
        c = expr.as<Call>();
        internal_assert(c);

        if ((c->call_type == Call::Halide) &&
            c->func.defined() &&
            substitutions.count(c->func)) {
            auto it = substitutions.find(c->func);
            internal_assert(it != substitutions.end())
                << "Function not in environment: " << c->func->name << "\n";
            FunctionPtr subs = it->second;
            debug(4) << "...Replace call to Func \"" << c->name << "\" with "
                     << "\"" << subs->name << "\"\n";
            expr = Call::make(c->type, subs->name, c->args, c->call_type,
                              subs, c->value_index,
                              c->image, c->param);
        }
        return expr;
    }

public:
    SubstituteCalls(const map<FunctionPtr, FunctionPtr> &substitutions)
        : substitutions(substitutions) {
    }
};

}  // anonymous namespace

Function &Function::substitute_calls(const map<FunctionPtr, FunctionPtr> &substitutions) {
    debug(4) << "Substituting calls in " << name() << "\n";
    if (substitutions.empty()) {
        return *this;
    }
    SubstituteCalls subs_calls(substitutions);
    contents->mutate(&subs_calls);
    return *this;
}

Function &Function::substitute_calls(const Function &orig, const Function &substitute) {
    map<FunctionPtr, FunctionPtr> substitutions;
    substitutions.emplace(orig.get_contents(), substitute.get_contents());
    return substitute_calls(substitutions);
}

// Deep copy an entire Function DAG.
pair<vector<Function>, map<string, Function>> deep_copy(
    const vector<Function> &outputs, const map<string, Function> &env) {
    vector<Function> copy_outputs;
    map<string, Function> copy_env;

    // Create empty deep-copies of all Functions in 'env'
    DeepCopyMap copied_map;  // Original Function -> Deep-copy
    IntrusivePtr<FunctionGroup> group(new FunctionGroup);
    group->members.resize(env.size());
    int i = 0;
    for (const auto &iter : env) {
        // Make a weak pointer to the function to use for within-group references.
        FunctionPtr ptr;
        ptr.weak = group.get();
        ptr.idx = i;
        ptr->name = iter.second.name();
        copied_map[iter.second.get_contents()] = ptr;
        i++;
    }

    // Deep copy all Functions in 'env' into their corresponding empty copies
    for (const auto &iter : env) {
        iter.second.deep_copy(copied_map[iter.second.get_contents()], copied_map);
    }

    // Need to substitute-in all old Function references in all Exprs referenced
    // within the Function with the deep-copy versions
    for (auto &iter : copied_map) {
        Function(iter.second).substitute_calls(copied_map);
    }

    // Populate the env with the deep-copy version
    for (const auto &iter : copied_map) {
        FunctionPtr ptr = iter.second;
        copy_env.emplace(iter.first->name, Function(ptr));
    }

    for (const auto &func : outputs) {
        const auto &iter = copied_map.find(func.get_contents());
        if (iter != copied_map.end()) {
            FunctionPtr ptr = iter->second;
            debug(4) << "Adding deep-copied version to outputs: " << func.name() << "\n";
            copy_outputs.emplace_back(ptr);
        } else {
            debug(4) << "Adding original version to outputs: " << func.name() << "\n";
            copy_outputs.push_back(func);
        }
    }

    return {copy_outputs, copy_env};
}

}  // namespace Internal
}  // namespace Halide
