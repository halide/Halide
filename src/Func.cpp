#include <algorithm>
#include <iostream>
#include <string.h>
#include <fstream>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include "IR.h"
#include "Func.h"
#include "Util.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "IRMutator.h"
#include "Function.h"
#include "Argument.h"
#include "Lower.h"
#include "Param.h"
#include "PrintLoopNest.h"
#include "Debug.h"
#include "IREquality.h"
#include "CodeGen_LLVM.h"
#include "LLVM_Headers.h"
#include "Outputs.h"
#include "LLVM_Output.h"
#include "Substitute.h"
#include "ExprUsesVar.h"
#include "Simplify.h"
#include "Solve.h"
#include "Associativity.h"
#include "ApplySplit.h"

namespace Halide {

using std::max;
using std::min;
using std::map;
using std::string;
using std::vector;
using std::pair;
using std::ofstream;

using namespace Internal;

Func::Func(const string &name) : func(unique_name(name)) {}

Func::Func() : func(make_entity_name(this, "Halide::Func", 'f')) {}

Func::Func(Expr e) : func(make_entity_name(this, "Halide::Func", 'f')) {
    (*this)(_) = e;
}

Func::Func(Function f) : func(f) {}

const string &Func::name() const {
    return func.name();
}

/** Get the pure arguments. */
std::vector<Var> Func::args() const {
    const std::vector<std::string> arg_names = func.args();
    std::vector<Var> args(arg_names.size());
    for (size_t i = 0; i < arg_names.size(); i++) {
        args[i] = Var(arg_names[i]);
    }
    return args;
}

/** The right-hand-side value of the pure definition of this
 * function. An error if the Func has no definition, or is defined as
 * a Tuple. */
Expr Func::value() const {
    user_assert(defined())
        << "Can't call Func::value() on an undefined Func. To check if a Func is defined, call Func::defined()\n";
    user_assert(func.outputs() == 1)
        << "Can't call Func::value() on Func \"" << name() << "\", because it has multiple values.\n";
    return func.values()[0];
}

/** The values returned by a Func, in Tuple form. */
Tuple Func::values() const {
    user_assert(defined())
        << "Can't call Func::values() on an undefined Func. To check if a Func is defined, call Func::defined().\n";
    return Tuple(func.values());
}

/** Get the left-hand-side of the update definition. An empty
 * vector if there's no update definition. */
const std::vector<Expr> &Func::update_args(int idx) const {
    user_assert(has_update_definition())
        << "Can't call Func::update_args() on Func \"" << name()
        << "\" as it has no update definition. "
        << "Use Func::has_update_definition() to check for the existence of an update definition.\n";
    user_assert(idx < num_update_definitions())
        << "Update definition index out of bounds.\n";
    return func.update(idx).args();
}

/** Get the right-hand-side of the update definition. An error if
 * there is no update definition. */
Expr Func::update_value(int idx) const {
    user_assert(has_update_definition())
        << "Can't call Func::update_args() on Func \"" << name() << "\" as it has no update definition. "
        << "Use Func::has_update_definition() to check for the existence of an update definition.\n";
    user_assert(idx < num_update_definitions())
        << "Update definition index out of bounds.\n";
    user_assert(func.update(idx).values().size() == 1)
        << "Can't call Func::update_value() on Func \"" << name() << "\", because it has multiple values.\n";
    return func.update(idx).values()[0];
}

/** The update values returned by a Func, in Tuple form. */
Tuple Func::update_values(int idx) const {
    user_assert(has_update_definition())
        << "Can't call Func::update_args() on Func \"" << name() << "\" as it has no update definition. "
        << "Use Func::has_update_definition() to check for the existence of an update definition.\n";
    user_assert(idx < num_update_definitions())
        << "Update definition index out of bounds.\n";
    return Tuple(func.update(idx).values());
}

/** Get the RVars of the reduction domain for the update definition. Returns an
 * empty vector if there's no update definition, or if the update definition has
 * no domain. Note that the RVars returned are floating RVars, i.e. they don't
 * actually have pointer to the reduction domain. */
vector<RVar> Func::rvars(int idx) const {
    user_assert(has_update_definition())
        << "Can't call Func::update_args() on Func \"" << name() << "\" as it has no update definition. "
        << "Use Func::has_update_definition() to check for the existence of an update definition.\n";
    user_assert(idx < num_update_definitions())
        << "Update definition index out of bounds.\n";
    const std::vector<ReductionVariable> rvars = func.update(idx).schedule().rvars();
    std::vector<RVar> rvs(rvars.size());
    for (size_t i = 0; i < rvars.size(); i++) {
        rvs[i] = RVar(rvars[i].var);
    }
    return rvs;
}

bool Func::defined() const {
    return func.has_pure_definition() || func.has_extern_definition();
}

/** Is this function a reduction? */
bool Func::has_update_definition() const {
    return func.has_update_definition();
}

/** How many update definitions are there? */
int Func::num_update_definitions() const {
    return static_cast<int>(func.updates().size());
}

/** Is this function external? */
EXPORT bool Func::is_extern() const {
    return func.has_extern_definition();
}

/** Add an extern definition for this Func. */
void Func::define_extern(const std::string &function_name,
                         const std::vector<ExternFuncArgument> &args,
                         const std::vector<Type> &types,
                         int dimensionality,
                         NameMangling mangling) {
    func.define_extern(function_name, args, types, dimensionality, mangling);
}

/** Get the types of the buffers returned by an extern definition. */
const std::vector<Type> &Func::output_types() const {
    return func.output_types();
}

/** Get the number of outputs this function has. */
int Func::outputs() const {
    return func.outputs();
}

/** Get the name of the extern function called for an extern
 * definition. */
EXPORT const std::string &Func::extern_function_name() const {
    return func.extern_function_name();
}

int Func::dimensions() const {
    if (!defined()) return 0;
    return func.dimensions();
}

FuncRef Func::operator()(vector<Var> args) const {
    int placeholder_pos, count;
    std::tie(placeholder_pos, count) = add_implicit_vars(args);
    return FuncRef(func, args, placeholder_pos, count);
}

FuncRef Func::operator()(vector<Expr> args) const {
    int placeholder_pos, count;
    std::tie(placeholder_pos, count) = add_implicit_vars(args);
    return FuncRef(func, args, placeholder_pos, count);
}

std::pair<int, int> Func::add_implicit_vars(vector<Var> &args) const {
    int placeholder_pos = -1;
    int count = 0;
    std::vector<Var>::iterator iter = args.begin();

    while (iter != args.end() && !iter->same_as(_)) {
        iter++;
    }
    if (iter != args.end()) {
        placeholder_pos = (int)(iter - args.begin());
        int i = 0;
        iter = args.erase(iter);
        while ((int)args.size() < dimensions()) {
            Internal::debug(2) << "Adding implicit var " << i << " to call to " << name() << "\n";
            iter = args.insert(iter, Var::implicit(i++));
            iter++;
            count++;
        }
    }

    if (func.has_pure_definition() && args.size() != (size_t)dimensions()) {
        user_error << "Func \"" << name() << "\" was called with "
                   << args.size() << " arguments, but was defined with " << dimensions() << "\n";
    }

    return { placeholder_pos, count };
}

std::pair<int, int> Func::add_implicit_vars(vector<Expr> &args) const {
    int placeholder_pos = -1;
    int count = 0;
    std::vector<Expr>::iterator iter = args.begin();
    while (iter != args.end()) {
        const Variable *var = iter->as<Variable>();
        if (var && var->name == _.name())
            break;
        iter++;
    }
    if (iter != args.end()) {
        placeholder_pos = (int)(iter - args.begin());
        int i = 0;
        iter = args.erase(iter);
        while ((int)args.size() < dimensions()) {
            Internal::debug(2) << "Adding implicit var " << i << " to call to " << name() << "\n";
            iter = args.insert(iter, Var::implicit(i++));
            iter++;
            count++;
        }
    }

    if (func.has_pure_definition() && args.size() != (size_t)dimensions()) {
        user_error << "Func \"" << name() << "\" was called with "
                   << args.size() << " arguments, but was defined with " << dimensions() << "\n";
    }

    return { placeholder_pos, count };
}

namespace {
bool var_name_match(string candidate, string var) {
    internal_assert(var.find('.') == string::npos)
        << "var_name_match expects unqualified names for the second argument. "
        << "Name passed: " << var << "\n";
    if (candidate == var) return true;
    return Internal::ends_with(candidate, "." + var);
}
}

const std::string &Stage::name() const {
    return stage_name;
}

void Stage::set_dim_type(VarOrRVar var, ForType t) {
    bool found = false;
    vector<Dim> &dims = definition.schedule().dims();
    for (size_t i = 0; i < dims.size(); i++) {
        if (var_name_match(dims[i].var, var.name())) {
            found = true;
            dims[i].for_type = t;

            // If it's an rvar and the for type is parallel, we need to
            // validate that this doesn't introduce a race condition.
            if (!dims[i].is_pure() && var.is_rvar &&
                (t == ForType::Vectorized || t == ForType::Parallel ||
                 t == ForType::GPUBlock || t == ForType::GPUThread)) {
                user_assert(definition.schedule().allow_race_conditions())
                    << "In schedule for " << stage_name
                    << ", marking var " << var.name()
                    << " as parallel or vectorized may introduce a race"
                    << " condition resulting in incorrect output."
                    << " It is possible to override this error using"
                    << " the allow_race_conditions() method. Use this"
                    << " with great caution, and only when you are willing"
                    << " to accept non-deterministic output, or you can prove"
                    << " that any race conditions in this code do not change"
                    << " the output, or you can prove that there are actually"
                    << " no race conditions, and that Halide is being too cautious.\n";
            }

        } else if (t == ForType::Vectorized) {
            user_assert(dims[i].for_type != ForType::Vectorized)
                << "In schedule for " << stage_name
                << ", can't vectorize across " << var.name()
                << " because Func is already vectorized across " << dims[i].var << "\n";
        }
    }

    if (!found) {
        user_error << "In schedule for " << stage_name
                   << ", could not find dimension "
                   << var.name()
                   << " to mark as " << t
                   << " in vars for function\n"
                   << dump_argument_list();
    }
}

void Stage::set_dim_device_api(VarOrRVar var, DeviceAPI device_api) {
    bool found = false;
    vector<Dim> &dims = definition.schedule().dims();
    for (size_t i = 0; i < dims.size(); i++) {
        if (var_name_match(dims[i].var, var.name())) {
            found = true;
            dims[i].device_api = device_api;
        }
    }

    if (!found) {
        user_error << "In schedule for " << stage_name
                   << ", could not find dimension "
                   << var.name()
                   << " to set to device API " << static_cast<int>(device_api)
                   << " in vars for function\n"
                   << dump_argument_list();
    }
}

std::string Stage::dump_argument_list() const {
    std::ostringstream oss;
    oss << "Vars:";
    for (size_t i = 0; i < definition.schedule().dims().size(); i++) {
        oss << " " << definition.schedule().dims()[i].var;
    }
    oss << "\n";
    return oss.str();
}

namespace {

class SubstituteSelfReference : public IRMutator {
    using IRMutator::visit;

    const string func;
    const Function substitute;
    const vector<Var> new_args;

    void visit(const Call *c) {
        IRMutator::visit(c);
        c = expr.as<Call>();
        internal_assert(c);

        if ((c->call_type == Call::Halide) && (func == c->name)) {
            internal_assert(!c->func.defined())
                << "func should not have been defined for a self-reference\n";
            debug(4) << "...Replace call to Func \"" << c->name << "\" with "
                     << "\"" << substitute.name() << "\"\n";
            vector<Expr> args;
            args.insert(args.end(), c->args.begin(), c->args.end());
            args.insert(args.end(), new_args.begin(), new_args.end());
            expr = Call::make(substitute, args, c->value_index);
        }
    }
public:
    SubstituteSelfReference(const string &func, const Function &substitute,
                            const vector<Var> &new_args)
            : func(func), substitute(substitute), new_args(new_args) {
        internal_assert(substitute.get_contents().defined());
    }
};

/** Substitute all self-reference calls to 'func' with 'substitute' which
 * args (LHS) is the old args (LHS) plus 'new_args' in that order.
 * Expect this method to be called on the value (RHS) of an update definition. */
Expr substitute_self_reference(Expr val, const string &func, const Function &substitute,
                               const vector<Var> &new_args) {
    SubstituteSelfReference subs(func, substitute, new_args);
    val = subs.mutate(val);
    return val;
}

// Substitute the occurrence of 'name' in 'exprs' with 'value'.
void substitute_var_in_exprs(const string &name, Expr value, vector<Expr> &exprs) {
    for (auto &expr : exprs) {
        expr = substitute(name, value, expr);
    }
}

void apply_split_result(const vector<pair<string, Expr>> &bounds_let_stmts,
                        const vector<ApplySplitResult> &splits_result,
                        vector<Expr> &predicates, vector<Expr> &args,
                        vector<Expr> &values) {

    for (const auto &res : splits_result) {
        if (res.is_substitution() || res.is_let()) {
            // Apply substitutions to the list of predicates, args, and values.
            // Make sure we substitute in all the let stmts as well since we are
            // not going to add them to the exprs.
            substitute_var_in_exprs(res.name, res.value, predicates);
            substitute_var_in_exprs(res.name, res.value, args);
            substitute_var_in_exprs(res.name, res.value, values);
        } else {
            internal_assert(res.is_predicate());
            predicates.push_back(res.value);
        }
    }

    // Make sure we substitute in all the let stmts from 'bounds_let_stmts'
    // since we are not going to add them to the exprs.
    for (const auto &let: bounds_let_stmts) {
        substitute_var_in_exprs(let.first, let.second, predicates);
        substitute_var_in_exprs(let.first, let.second, args);
        substitute_var_in_exprs(let.first, let.second, values);
    }
}

/** Apply split directives on the reduction variables. Remove the old RVar from
 * the list and add the split result (inner and outer RVars) to the list. Add
 * new predicates corresponding to the TailStrategy to the RDom predicate list. */
bool apply_split(const Split &s, vector<ReductionVariable> &rvars,
                 vector<Expr> &predicates, vector<Expr> &args,
                 vector<Expr> &values, map<string, Expr> &dim_extent_alignment) {
    internal_assert(s.is_split());
    const auto it = std::find_if(rvars.begin(), rvars.end(),
        [&s](const ReductionVariable& rv) { return (s.old_var == rv.var); });

    Expr old_max, old_min, old_extent;

    if (it != rvars.end()) {
        debug(4) << "  Splitting " << it->var << " into " << s.outer << " and " << s.inner << "\n";

        old_max = simplify(it->min + it->extent - 1);
        old_min = it->min;
        old_extent = it->extent;

        it->var = s.inner;
        it->min = 0;
        it->extent = s.factor;

        rvars.insert(it + 1, {s.outer, 0, simplify((old_extent - 1 + s.factor)/s.factor)});

        vector<ApplySplitResult> splits_result = apply_split(s, true, "", dim_extent_alignment);
        vector<pair<string, Expr>> bounds_let_stmts = compute_loop_bounds_after_split(s, "");
        apply_split_result(bounds_let_stmts, splits_result, predicates, args, values);

        return true;
    }
    return false;
}

/** Apply fuse directives on the reduction variables. Remove the
 * fused RVars from the list and add the fused RVar to the list. */
bool apply_fuse(const Split &s, vector<ReductionVariable> &rvars,
                vector<Expr> &predicates, vector<Expr> &args,
                vector<Expr> &values, map<string, Expr> &dim_extent_alignment) {
    internal_assert(s.is_fuse());
    const auto iter_outer = std::find_if(rvars.begin(), rvars.end(),
        [&s](const ReductionVariable& rv) { return (s.outer == rv.var); });
    const auto iter_inner = std::find_if(rvars.begin(), rvars.end(),
        [&s](const ReductionVariable& rv) { return (s.inner == rv.var); });

    Expr inner_min, inner_extent, outer_min, outer_extent;
    if ((iter_outer != rvars.end()) && (iter_inner != rvars.end())) {
        debug(4) << "  Fusing " << s.outer << " and " << s.inner << " into " << s.old_var << "\n";

        inner_min = iter_inner->min;
        inner_extent = iter_inner->extent;
        outer_min = iter_outer->min;
        outer_extent = iter_outer->extent;

        Expr extent = iter_outer->extent * iter_inner->extent;
        iter_outer->var = s.old_var;
        iter_outer->min = 0;
        iter_outer->extent = extent;
        rvars.erase(iter_inner);

        vector<ApplySplitResult> splits_result = apply_split(s, true, "", dim_extent_alignment);
        vector<pair<string, Expr>> bounds_let_stmts = compute_loop_bounds_after_split(s, "");
        apply_split_result(bounds_let_stmts, splits_result, predicates, args, values);

        return true;
    }
    return false;
}

/** Apply purify directives on the reduction variables and predicates. Purify
 * replace a RVar with a Var, thus, the RVar needs to be removed from the list.
 * Any reference to the RVar in the predicates will be replaced with reference
 * to a Var. */
bool apply_purify(const Split &s, vector<ReductionVariable> &rvars,
                  vector<Expr> &predicates, vector<Expr> &args,
                  vector<Expr> &values, map<string, Expr> &dim_extent_alignment) {
    internal_assert(s.is_purify());
    const auto iter = std::find_if(rvars.begin(), rvars.end(),
        [&s](const ReductionVariable& rv) { return (s.old_var == rv.var); });
    if (iter != rvars.end()) {
        debug(4) << "  Purify RVar " << iter->var << " into Var " << s.outer
                 << ", deleting it from the rvars list\n";
        rvars.erase(iter);

        vector<ApplySplitResult> splits_result = apply_split(s, true, "", dim_extent_alignment);
        vector<pair<string, Expr>> bounds_let_stmts = compute_loop_bounds_after_split(s, "");
        apply_split_result(bounds_let_stmts, splits_result, predicates, args, values);

        return true;
    }
    return false;
}

/** Apply rename directives on the reduction variables. */
bool apply_rename(const Split &s, vector<ReductionVariable> &rvars,
                  vector<Expr> &predicates, vector<Expr> &args,
                  vector<Expr> &values, map<string, Expr> &dim_extent_alignment) {
    internal_assert(s.is_rename());
    const auto iter = std::find_if(rvars.begin(), rvars.end(),
        [&s](const ReductionVariable& rv) { return (s.old_var == rv.var); });
    if (iter != rvars.end()) {
        debug(4) << "  Renaming " << iter->var << " into " << s.outer << "\n";
        iter->var = s.outer;

        vector<ApplySplitResult> splits_result = apply_split(s, true, "", dim_extent_alignment);
        vector<pair<string, Expr>> bounds_let_stmts = compute_loop_bounds_after_split(s, "");
        apply_split_result(bounds_let_stmts, splits_result, predicates, args, values);

        return true;
    }
    return false;
}

/** Apply scheduling directives (e.g. split, fuse, etc.) on the reduction
 * variables. */
bool apply_split_directive(const Split &s, vector<ReductionVariable> &rvars,
                           vector<Expr> &predicates, vector<Expr> &args,
                           vector<Expr> &values) {
    map<string, Expr> dim_extent_alignment;
    for (const ReductionVariable &rv : rvars) {
        dim_extent_alignment[rv.var] = rv.extent;
    }

    vector<pair<string, Expr>> rvar_bounds;
    for (const ReductionVariable &rv : rvars) {
        rvar_bounds.push_back({ rv.var + ".loop_min", rv.min });
        rvar_bounds.push_back({ rv.var + ".loop_max", simplify(rv.min + rv.extent - 1) });
        rvar_bounds.push_back({ rv.var + ".loop_extent", rv.extent });
    }

    bool found = false;
    if (s.is_split()) {
        found = apply_split(s, rvars, predicates, args, values, dim_extent_alignment);
    } else if (s.is_fuse()) {
        found = apply_fuse(s, rvars, predicates, args, values, dim_extent_alignment);
    } else if (s.is_purify()) {
        found = apply_purify(s, rvars, predicates, args, values, dim_extent_alignment);
    } else {
        found = apply_rename(s, rvars, predicates, args, values, dim_extent_alignment);
    }

    if (found) {
        for (const auto &let: rvar_bounds) {
            substitute_var_in_exprs(let.first, let.second, predicates);
            substitute_var_in_exprs(let.first, let.second, args);
            substitute_var_in_exprs(let.first, let.second, values);
        }
    }
    return found;
}

} // anonymous namespace

Func Stage::rfactor(RVar r, Var v) {
    return rfactor({{r, v}});
}

Func Stage::rfactor(vector<pair<RVar, Var>> preserved) {
    user_assert(!definition.is_init()) << "rfactor() must be called on an update definition\n";

    string func_name;
    {
        vector<std::string> tmp = split_string(stage_name, ".update(");
        internal_assert(!tmp.empty() && !tmp[0].empty());
        func_name = tmp[0];
    }

    vector<Expr> &args = definition.args();
    vector<Expr> &values = definition.values();

    // Check whether the operator is associative and determine the operator and
    // its identity for each value in the definition if it is a Tuple
    ProveAssociativityResult prover_result = prove_associativity(func_name, args, values);
    vector<AssociativeOp> &ops = prover_result.ops;
    user_assert(prover_result.is_associative)
        << "Failed to call rfactor() on " << stage_name
        << " since it can't prove associativity of the operator\n";
    internal_assert(ops.size() == values.size());

    vector<Split> &splits = definition.schedule().splits();
    vector<ReductionVariable> &rvars = definition.schedule().rvars();
    vector<Dim> &dims = definition.schedule().dims();
    vector<Expr> predicates = definition.split_predicate();

    Scope<string> scope; // Contains list of RVars lifted to the intermediate Func
    vector<string> rvars_removed;

    vector<bool> is_rfactored(dims.size(), false);
    for (const pair<RVar, Var> &i : preserved) {
        const RVar &rv = i.first;
        const Var &v = i.second;
        {
            // Check that the RVar are in the dims list
            const auto iter = std::find_if(dims.begin(), dims.end(),
                [&rv](const Dim& dim) { return var_name_match(dim.var, rv.name()); });
            user_assert((iter != dims.end()) && (*iter).is_rvar())
                << "In schedule for " << stage_name
                << ", can't perform rfactor() on " << rv.name()
                << " since it is not in the reduction domain\n"
                << dump_argument_list();
            is_rfactored[iter - dims.begin()] = true;
        }
        {
            // Check that the new pure Vars we used to rename the RVar aren't already in the dims list
            const auto &iter = std::find_if(dims.begin(), dims.end(),
                [&v](const Dim& dim) { return var_name_match(dim.var, v.name()); });
            user_assert(iter == dims.end())
                << "In schedule for " << stage_name
                << ", can't rename the rvars " << rv.name() << " into " << v.name()
                << ", since it is already used in this Func's schedule elsewhere.\n"
                << dump_argument_list();
        }
    }

    // If the operator is associative but non-commutative, rfactor() on inner
    // dimensions (excluding the outer dimensions) is not valid.
    if (!prover_result.is_commutative) {
        int last_rvar = -1;
        for (int i = dims.size() - 1; i >= 0; --i) {
            if ((last_rvar != -1) && is_rfactored[i]) {
                user_assert(is_rfactored[last_rvar])
                    << "In schedule for " << stage_name
                    << ", can't rfactor an inner dimension " << dims[i].var
                    << " without rfactoring the outer dimensions, since the "
                    << "operator is non-commutative.\n"
                    << dump_argument_list();
            }
            if (dims[i].is_rvar()) {
                last_rvar = i;
            }
        }
    }

    // We need to apply the split directives on the reduction vars, so that we can
    // correctly lift the RVars not in 'rvars_kept' and distribute the RVars to the
    // intermediate and merge Funcs.
    {
        vector<Split> temp;
        for (const Split &s : splits) {
            // If it's already applied, we should remove it from the split list.
            if (!apply_split_directive(s, rvars, predicates, args, values)) {
                temp.push_back(s);
            }
        }
        splits = temp;
    }

    // Reduction domain of the intermediate update definition
    vector<ReductionVariable> intm_rvars;
    for (const auto &rv : rvars) {
        const auto &iter = std::find_if(preserved.begin(), preserved.end(),
            [&rv](const pair<RVar, Var>& pair) { return var_name_match(rv.var, pair.first.name()); });
        if (iter == preserved.end()) {
            intm_rvars.push_back(rv);
            scope.push(rv.var, rv.var);
        }
    }
    RDom intm_rdom(intm_rvars);

    // Sort the Rvars kept and their Vars replacement based on the RVars of
    // the reduction domain AFTER applying the split directives, so that we
    // can have a consistent args order for the update definition of the
    // intermediate and new merge Funcs.
    std::sort(preserved.begin(), preserved.end(),
        [&](const pair<RVar, Var> &lhs, const pair<RVar, Var> &rhs){
            const auto iter_lhs = std::find_if(rvars.begin(), rvars.end(),
                [&lhs](const ReductionVariable& rv) { return var_name_match(rv.var, lhs.first.name()); });
            const auto iter_rhs = std::find_if(rvars.begin(), rvars.end(),
                [&rhs](const ReductionVariable& rv) { return var_name_match(rv.var, rhs.first.name()); });
            return iter_lhs < iter_rhs;
        }
    );
    // The list of RVars to keep in the new update definition
    vector<RVar> rvars_kept(preserved.size());
    // List of pure Vars to replace the RVars in the intermediate's update definition
    vector<Var> vars_rename(preserved.size());
    for (size_t i = 0; i < preserved.size(); ++i) {
        const auto &val = preserved[i];
        rvars_kept[i] = val.first;
        vars_rename[i] = val.second;
    }

    // List of RVars for the new reduction domain. Any RVars not in 'rvars_kept'
    // are removed from the RDom
    {
        vector<ReductionVariable> temp;
        for (const auto &rv : rvars) {
            const auto &iter = std::find_if(rvars_kept.begin(), rvars_kept.end(),
                [&rv](const RVar &rvar) { return var_name_match(rv.var, rvar.name()); });
            if (iter != rvars_kept.end()) {
                temp.push_back(rv);
            } else {
                rvars_removed.push_back(rv.var);
            }
        }
        rvars.swap(temp);
    }
    RDom f_rdom(rvars);

    // Init definition of the intermediate Func

    // Compute args of the init definition of the intermediate Func.
    // Replace the RVars, which are in 'rvars_kept', with the specified new pure
    // Vars. Also, add the pure Vars of the original init definition as part of
    // the args.
    // For example, if we have the following Func f:
    //   f(x, y) = 10
    //   f(r.x, r.y) += h(r.x, r.y)
    // Calling f.update(0).rfactor({{r.y, u}}) will generate the following
    // intermediate Func:
    //   f_intm(x, y, u) = 0
    //   f_intm(r.x, u, u) += h(r.x, u)

    vector<Var> init_args;
    init_args.insert(init_args.end(), dim_vars.begin(), dim_vars.end());
    init_args.insert(init_args.end(), vars_rename.begin(), vars_rename.end());

    vector<Expr> init_vals(values.size());
    for (size_t i = 0; i < init_vals.size(); ++i) {
        init_vals[i] = ops[i].identity;
    }

    Func intm(func_name + "_intm");
    intm(init_args) = Tuple(init_vals);

    // Args of the update definition of the intermediate Func
    vector<Expr> update_args(args.size() + vars_rename.size());

    // We need to substitute the reference to the old RDom's RVars with
    // the new RDom's RVars. Also, substitute the reference to RVars which
    // are in 'rvars_kept' with their corresponding new pure Vars
    map<string, Expr> substitution_map;
    for (size_t i = 0; i < intm_rvars.size(); ++i) {
        substitution_map[intm_rvars[i].var] = intm_rdom[i];
    }
    for (size_t i = 0; i < vars_rename.size(); i++) {
        update_args[i + args.size()] = vars_rename[i];
        RVar rvar_kept = rvars_kept[i];
        // Find the full name of rvar_kept in rvars
        const auto iter = std::find_if(rvars.begin(), rvars.end(),
            [&rvar_kept](const ReductionVariable &rv) { return var_name_match(rv.var, rvar_kept.name()); });
        substitution_map[iter->var] = vars_rename[i];
    }
    for (size_t i = 0; i < args.size(); i++) {
        Expr arg = substitute(substitution_map, args[i]);
        update_args[i] = arg;
    }

    // Compute the predicates for the intermediate Func and the new update definition
    for (const Expr &pred : predicates) {
        Expr subs_pred = substitute(substitution_map, pred);
        intm_rdom.where(subs_pred);
        if (!expr_uses_vars(pred, scope)) {
            // Only keep the predicate that does not depend on the lifted RVars
            // (either explicitly or implicitly). For example, if 'rx' is split
            // into 'rxo' and 'rxi' and 'rxo' is part of the lifted RVars, we'll
            // ignore every predicate that depends on 'rx'
            f_rdom.where(pred);
        }
    }
    definition.predicate() = f_rdom.domain().predicate();

    // The update values the intermediate Func should compute
    vector<Expr> update_vals(values.size());
    for (size_t i = 0; i < update_vals.size(); i++) {
        Expr val = substitute(substitution_map, values[i]);
        // Need to update the self-reference in the update definition to point
        // to the new intermediate Func
        val = substitute_self_reference(val, func_name, intm.function(), vars_rename);
        update_vals[i] = val;
    }
    intm(update_args) = Tuple(update_vals);


    // Determine the dims and schedule of the update definition of the
    // intermediate Func. We copy over the schedule from the original
    // update definition (e.g. split, parallelize, vectorize, etc.)
    intm.function().update(0).schedule().dims() = dims;
    intm.function().update(0).schedule().splits() = splits;

    // Copy over the storage order of the original pure dims
    vector<StorageDim> &intm_storage_dims = intm.function().schedule().storage_dims();
    internal_assert(intm_storage_dims.size() == storage_dims.size() + vars_rename.size());
    for (size_t i = 0; i < storage_dims.size(); ++i) {
        intm_storage_dims[i] = storage_dims[i];
    }

    for (size_t i = 0; i < rvars_kept.size(); ++i) {
        // Apply the purify directive that replaces the RVar in rvars_kept
        // with a pure Var
        intm.update(0).purify(rvars_kept[i], vars_rename[i]);
    }

    // Determine the dims of the new update definition

    // Add pure Vars from the original init definition to the dims list
    // if they are not already in the list
    for (const Var &v : dim_vars) {
        const auto iter = std::find_if(dims.begin(), dims.end(),
            [&v](const Dim& dim) { return var_name_match(dim.var, v.name()); });
        if (iter == dims.end()) {
            Dim d = {v.name(), ForType::Serial, DeviceAPI::None, Dim::Type::PureVar};
            dims.insert(dims.end()-1, d);
        }
    }
    // Then, we need to remove lifted RVars from the dims list
    for (const string &rv : rvars_removed) {
        remove(rv);
    }

    // Define the new update definition which refers to the intermediate Func.
    // Using the same example as above, the new update definition is:
    //   f(x, y) += f_intm(x, y, r.y)

    // Args for store in the new update definition
    vector<Expr> f_store_args(dim_vars.size());
    for (size_t i = 0; i < f_store_args.size(); ++i) {
        f_store_args[i] = dim_vars[i];
    }

    // Call's args to the intermediate Func in the new update definition
    vector<Expr> f_load_args;
    f_load_args.insert(f_load_args.end(), dim_vars.begin(), dim_vars.end());
    for (int i = 0; i < f_rdom.dimensions(); ++i) {
        f_load_args.push_back(f_rdom[i]);
    }
    internal_assert(f_load_args.size() == init_args.size());

    // Update value of the new update definition. It loads values from
    // the intermediate Func.
    vector<Expr> f_values(values.size());
    if (values.size() > 1) {
        for (size_t i = 0; i < f_values.size(); ++i) {
            Expr prev_val = Call::make(intm.output_types()[i], func_name,
                                       f_store_args, Call::CallType::Halide,
                                       nullptr, i);
            const AssociativeOp &op = ops[i];
            Expr val = substitute(op.y.first, intm(f_load_args)[i], op.op);
            if (!op.x.first.empty()) {
                val = substitute(op.x.first, prev_val, val);
            } else {
                user_warning << "Update definition of " << stage_name << " at index " << i
                             << " doesn't depend on the previous value. This isn't a"
                             << " reduction operation\n";
            }
            f_values[i] = val;
        }
    } else {
        Expr prev_val = Call::make(intm.output_types()[0], func_name,
                                   f_store_args, Call::CallType::Halide);
        const AssociativeOp &op = ops[0];
        Expr val = substitute(op.y.first, intm(f_load_args), op.op);
        if (!op.x.first.empty()) {
            val = substitute(op.x.first, prev_val, val);
        } else {
            user_warning << "Update definition of " << stage_name
                         << " doesn't depend on the previous value. This isn't a"
                         << " reduction operation\n";
        }
        f_values[0] = val;
    }

    // Update the definition
    args.swap(f_store_args);
    values.swap(f_values);

    return intm;
}

void Stage::split(const string &old, const string &outer, const string &inner, Expr factor, bool exact, TailStrategy tail) {
    debug(4) << "In schedule for " << stage_name << ", split " << old << " into "
             << outer << " and " << inner << " with factor of " << factor << "\n";
    vector<Dim> &dims = definition.schedule().dims();

    // Check that the new names aren't already in the dims list.
    for (size_t i = 0; i < dims.size(); i++) {
        string new_names[2] = {inner, outer};
        for (int j = 0; j < 2; j++) {
            if (var_name_match(dims[i].var, new_names[j]) && new_names[j] != old) {
                user_error << "In schedule for " << stage_name
                           << ", can't create var " << new_names[j]
                           << " using a split or tile, because " << new_names[j]
                           << " is already used in this Func's schedule elsewhere.\n"
                           << dump_argument_list();
            }
        }
    }

    // Replace the old dimension with the new dimensions in the dims list
    bool found = false;
    string inner_name, outer_name, old_name;

    for (size_t i = 0; (!found) && i < dims.size(); i++) {
        if (var_name_match(dims[i].var, old)) {
            found = true;
            old_name = dims[i].var;
            inner_name = old_name + "." + inner;
            outer_name = old_name + "." + outer;
            dims.insert(dims.begin() + i, dims[i]);
            dims[i].var = inner_name;
            dims[i+1].var = outer_name;
        }
    }

    if (!found) {
        user_error << "In schedule for " << stage_name
                   << ", could not find split dimension: "
                   << old
                   << "\n"
                   << dump_argument_list();
    }

    if (tail == TailStrategy::Auto) {
        // Select a tail strategy
        if (exact) {
            tail = TailStrategy::GuardWithIf;
        } else if (!definition.is_init()) {
            tail = TailStrategy::RoundUp;
        } else {
            // We should employ ShiftInwards when we can to prevent
            // overcompute and adding constraints to the bounds of
            // inputs and outputs. However, if we're already covered
            // by an earlier ShiftInwards split, there's no point - it
            // just complicates the IR and confuses bounds inference. An example of this is:
            //
            // f.vectorize(x, 8).unroll(x, 4);
            //
            // The vectorize-induced split is ShiftInwards. There's no
            // point also applying ShiftInwards to the unroll-induced
            // split.
            //
            // Note that we'll still partition the outermost loop to
            // avoid the overhead of the min we placed in the inner
            // loop with the vectorize, because that's how loop
            // partitioning works. The steady-state will be just as
            // efficient as:
            //
            // f.split(x, x, xi, 32).vectorize(xi, 8).unroll(xi);
            //
            // It's only the tail/epilogue that changes.

            std::set<string> descends_from_shiftinwards_outer;
            for (const Split &s : definition.schedule().splits()) {
                if (s.is_split() && s.tail == TailStrategy::ShiftInwards) {
                    descends_from_shiftinwards_outer.insert(s.outer);
                } else if (s.is_split() && descends_from_shiftinwards_outer.count(s.old_var)) {
                    descends_from_shiftinwards_outer.insert(s.inner);
                    descends_from_shiftinwards_outer.insert(s.outer);
                } else if ((s.is_rename() || s.is_purify()) &&
                           descends_from_shiftinwards_outer.count(s.old_var)) {
                    descends_from_shiftinwards_outer.insert(s.outer);
                }
            }
            if (descends_from_shiftinwards_outer.count(old_name)) {
                tail = TailStrategy::RoundUp;
            } else {
                tail = TailStrategy::ShiftInwards;
            }
        }
    }

    if (!definition.is_init()) {
        user_assert(tail != TailStrategy::ShiftInwards)
            << "When splitting Var " << old_name
            << " ShiftInwards is not a legal tail strategy for update definitions, as"
            << " it may change the meaning of the algorithm\n";
    }

    if (exact) {
        user_assert(tail == TailStrategy::GuardWithIf)
            << "When splitting Var " << old_name
            << " the tail strategy must be GuardWithIf or Auto. "
            << "Anything else may change the meaning of the algorithm\n";
    }

    // Add the split to the splits list
    Split split = {old_name, outer_name, inner_name, factor, exact, tail, Split::SplitVar};
    definition.schedule().splits().push_back(split);
}

Stage &Stage::split(VarOrRVar old, VarOrRVar outer, VarOrRVar inner, Expr factor, TailStrategy tail) {
    if (old.is_rvar) {
        user_assert(outer.is_rvar) << "Can't split RVar " << old.name() << " into Var " << outer.name() << "\n";
        user_assert(inner.is_rvar) << "Can't split RVar " << old.name() << " into Var " << inner.name() << "\n";
    } else {
        user_assert(!outer.is_rvar) << "Can't split Var " << old.name() << " into RVar " << outer.name() << "\n";
        user_assert(!inner.is_rvar) << "Can't split Var " << old.name() << " into RVar " << inner.name() << "\n";
    }
    split(old.name(), outer.name(), inner.name(), factor, old.is_rvar, tail);
    return *this;
}

Stage &Stage::fuse(VarOrRVar inner, VarOrRVar outer, VarOrRVar fused) {
    if (inner.is_rvar) {
        user_assert(outer.is_rvar) << "Can't fuse RVar " << inner.name()
                                   << " with Var " << outer.name() << "\n";
        user_assert(fused.is_rvar) << "Can't fuse RVar " << inner.name()
                                   << "into Var " << fused.name() << "\n";
    } else {
        user_assert(!outer.is_rvar) << "Can't fuse Var " << inner.name()
                                    << " with RVar " << outer.name() << "\n";
        user_assert(!fused.is_rvar) << "Can't fuse Var " << inner.name()
                                    << "into RVar " << fused.name() << "\n";
    }

    debug(4) << "In schedule for " << stage_name << ", fuse " << outer.name()
             << " and " << inner.name() << " into " << fused.name() << "\n";

    // Replace the old dimensions with the new dimension in the dims list
    bool found_outer = false, found_inner = false;
    string inner_name, outer_name, fused_name;
    vector<Dim> &dims = definition.schedule().dims();

    Dim::Type outer_type = Dim::Type::PureRVar;
    for (size_t i = 0; (!found_outer) && i < dims.size(); i++) {
        if (var_name_match(dims[i].var, outer.name())) {
            found_outer = true;
            outer_name = dims[i].var;
            outer_type = dims[i].dim_type;
            dims.erase(dims.begin() + i);
        }
    }
    if (!found_outer) {
        user_error << "In schedule for " << stage_name
                   << ", could not find outer fuse dimension: "
                   << outer.name()
                   << "\n"
                   << dump_argument_list();
    }

    for (size_t i = 0; (!found_inner) && i < dims.size(); i++) {
        if (var_name_match(dims[i].var, inner.name())) {
            found_inner = true;
            inner_name = dims[i].var;
            fused_name = inner_name + "." + fused.name();
            dims[i].var = fused_name;

            internal_assert(
                (dims[i].is_rvar() && ((outer_type == Dim::Type::PureRVar) ||
                                       (outer_type == Dim::Type::ImpureRVar))) ||
                (!dims[i].is_rvar() && (outer_type == Dim::Type::PureVar)));

            if (dims[i].is_rvar()) {
                dims[i].dim_type = (dims[i].dim_type == Dim::Type::PureRVar) && (outer_type == Dim::Type::PureRVar) ?
                    Dim::Type::PureRVar : Dim::Type::ImpureRVar;
            }
        }
    }

    if (!found_inner) {
        user_error << "In schedule for " << stage_name
                   << ", could not find inner fuse dimension: "
                   << inner.name()
                   << "\n"
                   << dump_argument_list();
    }

    // Add the fuse to the splits list
    Split split = {fused_name, outer_name, inner_name, Expr(), true, TailStrategy::RoundUp, Split::FuseVars};
    definition.schedule().splits().push_back(split);
    return *this;
}

namespace Internal {
class CheckForFreeVars : public IRGraphVisitor {
public:
    string offending_var;
protected:
    using IRGraphVisitor::visit;
    void visit(const Variable *var) {
        if (!var->param.defined() && !var->image.defined()) {
            offending_var = var->name;
        }
    }
};
}

Stage Stage::specialize(Expr condition) {
    user_assert(condition.type().is_bool()) << "Argument passed to specialize must be of type bool\n";

    // The condition may not depend on Vars or RVars
    Internal::CheckForFreeVars check;
    condition.accept(&check);
    if (!check.offending_var.empty()) {
        user_error << "Specialization condition " << condition << " for " << stage_name
                   << " depends on Var or RVar " << check.offending_var << ". "
                   << "Specialization conditions may not depend on any Vars or RVars.\n";
    }

    // The user may be retrieving a reference to an existing
    // specialization.
    const vector<Specialization> &specializations = definition.specializations();
    for (size_t i = 0; i < specializations.size(); i++) {
        if (equal(condition, specializations[i].condition)) {
            return Stage(specializations[i].definition, stage_name, dim_vars, storage_dims);
        }
    }
    const Specialization &s = definition.add_specialization(condition);

    return Stage(s.definition, stage_name, dim_vars, storage_dims);
}

Stage &Stage::purify(VarOrRVar old_var, VarOrRVar new_var) {
    user_assert(old_var.is_rvar && !new_var.is_rvar)
        << "In schedule for " << stage_name
        << ", can't rename " << (old_var.is_rvar ? "RVar " : "Var ") << old_var.name()
        << " to " << (new_var.is_rvar ? "RVar " : "Var ") << new_var.name()
        << "; purify must take a RVar as old_Var and a Var as new_var\n";

    debug(4) << "In schedule for " << stage_name << ", purify RVar "
             << old_var.name() << " to Var " << new_var.name() << "\n";

    Schedule &schedule = definition.schedule();

    // Replace the old dimension with the new dimensions in the dims list
    bool found = false;
    string old_name, new_name = new_var.name();
    vector<Dim> &dims = schedule.dims();

    for (size_t i = 0; (!found) && i < dims.size(); i++) {
        if (var_name_match(dims[i].var, old_var.name())) {
            found = true;
            old_name = dims[i].var;
            dims[i].var = new_name;
            dims[i].dim_type = Dim::Type::PureVar;
        }
    }

    if (!found) {
        user_error
            << "In schedule for " << stage_name
            << ", could not find rename dimension: "
            << old_var.name()
            << "\n"
            << dump_argument_list();
    }

    Split split = {old_name, new_name, "", 1, false, TailStrategy::RoundUp, Split::PurifyRVar};
    definition.schedule().splits().push_back(split);
    return *this;
}

void Stage::remove(const string &var) {
    debug(4) << "In schedule for " << stage_name << ", remove " << var << "\n";

    Schedule &schedule = definition.schedule();

    // Replace the old dimension with the new dimensions in the dims list
    bool found = false;
    string old_name = var;
    vector<Dim> &dims = schedule.dims();
    for (size_t i = 0; (!found) && i < dims.size(); i++) {
        if (dims[i].var == var) {
            found = true;
            old_name = dims[i].var;
            dims.erase(dims.begin() + i);
        }
    }

    if (!found) {
        user_error
            << "In schedule for " << stage_name
            << ", could not find remove dimension: "
            << var
            << "\n"
            << dump_argument_list();

    }

    std::set<string> removed_vars;
    removed_vars.insert(var);

    auto should_remove = [&removed_vars](const string &var) {
        const auto &iter = std::find_if(
            removed_vars.begin(), removed_vars.end(), [&var](const string& rv) { return rv == var; });
        return iter != removed_vars.end();
    };

    vector<Split> &splits = schedule.splits();
    vector<Split> temp;
    for (size_t i = splits.size(); i > 0; i--) {
        bool is_removed = false;
        if (splits[i-1].is_fuse()) {
            debug(4) << "    checking fuse " << splits[i-1].inner << " and "
                     << splits[i-1].inner << " into " << splits[i-1].old_var << "\n";
            if (splits[i-1].inner == old_name ||
                splits[i-1].outer == old_name) {
                user_error
                    << "In schedule for " << stage_name
                    << ", can't remove variable " << old_name
                    << " because it has already been fused into "
                    << splits[i-1].old_var << "\n"
                    << dump_argument_list();
            }
            if (should_remove(splits[i-1].old_var)) {
                is_removed = true;
                removed_vars.insert(splits[i-1].outer);
                removed_vars.insert(splits[i-1].inner);
            }
        } else if (splits[i-1].is_split()) {
            debug(4) << "    splitting " << splits[i-1].old_var << " into "
                     << splits[i-1].outer << " and " << splits[i-1].inner << "\n";
            if (should_remove(splits[i-1].inner)) {
                is_removed = true;
                removed_vars.insert(splits[i-1].old_var);
            } else if (should_remove(splits[i-1].outer)) {
                is_removed = true;
                removed_vars.insert(splits[i-1].old_var);
            }
            if (splits[i-1].old_var == old_name) {
                user_error
                    << "In schedule for " << stage_name
                    << ", can't remove a variable " << old_name
                    << " because it has already been renamed or split.\n"
                    << dump_argument_list();
            }
        } else {
            debug(4) << "    replace/rename " << splits[i-1].old_var
                     << " into " << splits[i-1].outer << "\n";
            if (should_remove(splits[i-1].outer)) {
                is_removed = true;
                removed_vars.insert(splits[i-1].old_var);
            }
            if (splits[i-1].old_var == old_name) {
                user_error
                    << "In schedule for " << stage_name
                    << ", can't remove a variable " << old_name
                    << " because it has already been renamed or split.\n"
                    << dump_argument_list();
            }
        }
        if (!is_removed) {
            temp.insert(temp.begin(), splits[i-1]);
        }
    }
    splits.swap(temp);
}

Stage &Stage::rename(VarOrRVar old_var, VarOrRVar new_var) {
    if (old_var.is_rvar) {
        user_assert(new_var.is_rvar)
            << "In schedule for " << stage_name
            << ", can't rename RVar " << old_var.name()
            << " to Var " << new_var.name() << "\n";
    } else {
        user_assert(!new_var.is_rvar)
            << "In schedule for " << stage_name
            << ", can't rename Var " << old_var.name()
            << " to RVar " << new_var.name() << "\n";
    }

    debug(4) << "In schedule for " << stage_name << ", rename " << old_var.name()
             << " to " << new_var.name() << "\n";

    Schedule &schedule = definition.schedule();

    // Replace the old dimension with the new dimensions in the dims list
    bool found = false;
    string old_name;
    vector<Dim> &dims = schedule.dims();
    for (size_t i = 0; (!found) && i < dims.size(); i++) {
        if (var_name_match(dims[i].var, old_var.name())) {
            found = true;
            old_name = dims[i].var;
            dims[i].var += "." + new_var.name();
        }
    }

    string new_name = old_name + "." + new_var.name();

    if (!found) {
        user_error
            << "In schedule for " << stage_name
            << ", could not find rename dimension: "
            << old_var.name()
            << "\n"
            << dump_argument_list();
    }

    // If possible, rewrite the split or rename that defines it.
    found = false;
    vector<Split> &splits = schedule.splits();
    for (size_t i = splits.size(); i > 0; i--) {
        if (splits[i-1].is_fuse()) {
            if (splits[i-1].inner == old_name ||
                splits[i-1].outer == old_name) {
                user_error
                    << "In schedule for " << stage_name
                    << ", can't rename variable " << old_name
                    << " because it has already been fused into "
                    << splits[i-1].old_var << "\n"
                    << dump_argument_list();
            }
            if (splits[i-1].old_var == old_name) {
                splits[i-1].old_var = new_name;
                found = true;
                break;
            }
        } else {
            if (splits[i-1].inner == old_name) {
                splits[i-1].inner = new_name;
                found = true;
                break;
            }
            if (splits[i-1].outer == old_name) {
                splits[i-1].outer = new_name;
                found = true;
                break;
            }
            if (splits[i-1].old_var == old_name) {
                user_error
                    << "In schedule for " << stage_name
                    << ", can't rename a variable " << old_name
                    << " because it has already been renamed or split.\n"
                    << dump_argument_list();
            }
        }
    }

    if (!found) {
        Split split = {old_name, new_name, "", 1, old_var.is_rvar, TailStrategy::RoundUp, Split::RenameVar};
        definition.schedule().splits().push_back(split);
    }

    return *this;
}

Stage &Stage::allow_race_conditions() {
    definition.schedule().allow_race_conditions() = true;
    return *this;
}

Stage &Stage::serial(VarOrRVar var) {
    set_dim_type(var, ForType::Serial);
    return *this;
}

Stage &Stage::parallel(VarOrRVar var) {
    set_dim_type(var, ForType::Parallel);
    return *this;
}

Stage &Stage::vectorize(VarOrRVar var) {
    set_dim_type(var, ForType::Vectorized);
    return *this;
}

Stage &Stage::unroll(VarOrRVar var) {
    set_dim_type(var, ForType::Unrolled);
    return *this;
}

Stage &Stage::parallel(VarOrRVar var, Expr factor, TailStrategy tail) {
    if (var.is_rvar) {
        RVar tmp;
        split(var.rvar, var.rvar, tmp, factor, tail);
    } else {
        Var tmp;
        split(var.var, var.var, tmp, factor, tail);
    }
    parallel(var);
    return *this;
}

Stage &Stage::vectorize(VarOrRVar var, int factor, TailStrategy tail) {
    if (var.is_rvar) {
        RVar tmp;
        split(var.rvar, var.rvar, tmp, factor, tail);
        vectorize(tmp);
    } else {
        Var tmp;
        split(var.var, var.var, tmp, factor, tail);
        vectorize(tmp);
    }
    return *this;
}

Stage &Stage::unroll(VarOrRVar var, int factor, TailStrategy tail) {
    if (var.is_rvar) {
        RVar tmp;
        split(var.rvar, var.rvar, tmp, factor, tail);
        unroll(tmp);
    } else {
        Var tmp;
        split(var.var, var.var, tmp, factor, tail);
        unroll(tmp);
    }

    return *this;
}

Stage &Stage::tile(VarOrRVar x, VarOrRVar y,
                   VarOrRVar xo, VarOrRVar yo,
                   VarOrRVar xi, VarOrRVar yi,
                   Expr xfactor, Expr yfactor,
                   TailStrategy tail) {
    split(x, xo, xi, xfactor, tail);
    split(y, yo, yi, yfactor, tail);
    reorder(xi, yi, xo, yo);
    return *this;
}

Stage &Stage::tile(VarOrRVar x, VarOrRVar y,
                   VarOrRVar xi, VarOrRVar yi,
                   Expr xfactor, Expr yfactor,
                   TailStrategy tail) {
    split(x, x, xi, xfactor, tail);
    split(y, y, yi, yfactor, tail);
    reorder(xi, yi, x, y);
    return *this;
}

namespace {
// An helper function for reordering vars in a schedule.
void reorder_vars(vector<Dim> &dims_old, const VarOrRVar *vars, size_t size, const Stage &stage) {
    vector<Dim> dims = dims_old;

    // Tag all the vars with their locations in the dims list.
    vector<size_t> idx(size);
    for (size_t i = 0; i < size; i++) {
        bool found = false;
        for (size_t j = 0; j < dims.size(); j++) {
            if (var_name_match(dims[j].var, vars[i].name())) {
                idx[i] = j;
                found = true;
            }
        }
        user_assert(found)
            << "In schedule for " << stage.name()
            << ", could not find var " << vars[i].name()
            << " to reorder in the argument list.\n"
            << stage.dump_argument_list();
    }

    // Look for illegal reorderings
    for (size_t i = 0; i < idx.size(); i++) {
        if (dims[idx[i]].is_pure()) continue;
        for (size_t j = i+1; j < idx.size(); j++) {
            if (dims[idx[j]].is_pure()) continue;

            if (idx[i] > idx[j]) {
                user_error
                    << "In schedule for " << stage.name()
                    << ", can't reorder RVars " << vars[i].name()
                    << " and " << vars[j].name()
                    << " because it may change the meaning of the algorithm.\n";
            }
        }
    }

    // Sort idx to get the new locations
    vector<size_t> sorted = idx;
    std::sort(sorted.begin(), sorted.end());

    for (size_t i = 0; i < size; i++) {
        dims[sorted[i]] = dims_old[idx[i]];
    }

    dims_old.swap(dims);
}
}

Stage &Stage::reorder(const std::vector<VarOrRVar>& vars) {
    reorder_vars(definition.schedule().dims(), &vars[0], vars.size(), *this);
    return *this;
}

Stage &Stage::gpu_threads(VarOrRVar tx, DeviceAPI device_api) {
    set_dim_device_api(tx, device_api);
    set_dim_type(tx, ForType::GPUThread);
    return *this;
}

Stage &Stage::gpu_threads(VarOrRVar tx, VarOrRVar ty, DeviceAPI device_api) {
    set_dim_device_api(tx, device_api);
    set_dim_device_api(ty, device_api);
    set_dim_type(tx, ForType::GPUThread);
    set_dim_type(ty, ForType::GPUThread);
    return *this;
}

Stage &Stage::gpu_threads(VarOrRVar tx, VarOrRVar ty, VarOrRVar tz, DeviceAPI device_api) {
    set_dim_device_api(tx, device_api);
    set_dim_device_api(ty, device_api);
    set_dim_device_api(tz, device_api);
    set_dim_type(tx, ForType::GPUThread);
    set_dim_type(ty, ForType::GPUThread);
    set_dim_type(tz, ForType::GPUThread);
    return *this;
}

Stage &Stage::gpu_blocks(VarOrRVar bx, DeviceAPI device_api) {
    set_dim_device_api(bx, device_api);
    set_dim_type(bx, ForType::GPUBlock);
    return *this;
}

Stage &Stage::gpu_blocks(VarOrRVar bx, VarOrRVar by, DeviceAPI device_api) {
    set_dim_device_api(bx, device_api);
    set_dim_device_api(by, device_api);
    set_dim_type(bx, ForType::GPUBlock);
    set_dim_type(by, ForType::GPUBlock);
    return *this;
}

Stage &Stage::gpu_blocks(VarOrRVar bx, VarOrRVar by, VarOrRVar bz, DeviceAPI device_api) {
    set_dim_device_api(bx, device_api);
    set_dim_device_api(by, device_api);
    set_dim_device_api(bz, device_api);
    set_dim_type(bx, ForType::GPUBlock);
    set_dim_type(by, ForType::GPUBlock);
    set_dim_type(bz, ForType::GPUBlock);
    return *this;
}

Stage &Stage::gpu_single_thread(DeviceAPI device_api) {
    Var block;
    split(Var::outermost(), Var::outermost(), block, 1);
    set_dim_device_api(block, device_api);
    set_dim_type(block, ForType::GPUBlock);
    return *this;
}

Stage &Stage::gpu(VarOrRVar bx, VarOrRVar tx, DeviceAPI device_api) {
    return gpu_blocks(bx).gpu_threads(tx);
}

Stage &Stage::gpu(VarOrRVar bx, VarOrRVar by,
                  VarOrRVar tx, VarOrRVar ty, DeviceAPI device_api) {
    return gpu_blocks(bx, by).gpu_threads(tx, ty);
}

Stage &Stage::gpu(VarOrRVar bx, VarOrRVar by, VarOrRVar bz,
                  VarOrRVar tx, VarOrRVar ty, VarOrRVar tz,
                  DeviceAPI device_api) {
    return gpu_blocks(bx, by, bz).gpu_threads(tx, ty, tz);
}

Stage &Stage::gpu_tile(VarOrRVar x, VarOrRVar bx, Var tx, Expr x_size,
                       TailStrategy tail, DeviceAPI device_api) {
    split(x, bx, tx, x_size, tail);
    set_dim_device_api(bx, device_api);
    set_dim_device_api(tx, device_api);
    set_dim_type(bx, ForType::GPUBlock);
    set_dim_type(tx, ForType::GPUThread);
    return *this;
}

Stage &Stage::gpu_tile(VarOrRVar x, VarOrRVar bx, RVar tx, Expr x_size,
                       TailStrategy tail, DeviceAPI device_api) {
    split(x, bx, tx, x_size, tail);
    set_dim_device_api(bx, device_api);
    set_dim_device_api(tx, device_api);
    set_dim_type(bx, ForType::GPUBlock);
    set_dim_type(tx, ForType::GPUThread);
    return *this;
}

Stage &Stage::gpu_tile(VarOrRVar x, VarOrRVar tx, Expr x_size,
                       TailStrategy tail, DeviceAPI device_api) {
    split(x, x, tx, x_size, tail);
    set_dim_device_api(x, device_api);
    set_dim_device_api(tx, device_api);
    set_dim_type(x, ForType::GPUBlock);
    set_dim_type(tx, ForType::GPUThread);
    return *this;
}


Stage &Stage::gpu_tile(VarOrRVar x, VarOrRVar y,
                       VarOrRVar bx, VarOrRVar by,
                       VarOrRVar tx, VarOrRVar ty,
                       Expr x_size, Expr y_size,
                       TailStrategy tail,
                       DeviceAPI device_api) {
    tile(x, y, bx, by, tx, ty, x_size, y_size, tail);
    set_dim_device_api(bx, device_api);
    set_dim_device_api(by, device_api);
    set_dim_device_api(tx, device_api);
    set_dim_device_api(ty, device_api);
    set_dim_type(bx, ForType::GPUBlock);
    set_dim_type(by, ForType::GPUBlock);
    set_dim_type(tx, ForType::GPUThread);
    set_dim_type(ty, ForType::GPUThread);
    return *this;
}

Stage &Stage::gpu_tile(VarOrRVar x, VarOrRVar y,
                       VarOrRVar tx, Var ty,
                       Expr x_size, Expr y_size,
                       TailStrategy tail,
                       DeviceAPI device_api) {
    return gpu_tile(x, y, x, y, tx, ty, x_size, y_size, tail, device_api);
}

Stage &Stage::gpu_tile(VarOrRVar x, VarOrRVar y,
                       VarOrRVar tx, RVar ty,
                       Expr x_size, Expr y_size,
                       TailStrategy tail,
                       DeviceAPI device_api) {
    return gpu_tile(x, y, x, y, tx, ty, x_size, y_size, tail, device_api);
}

Stage &Stage::gpu_tile(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                       VarOrRVar bx, VarOrRVar by, VarOrRVar bz,
                       VarOrRVar tx, VarOrRVar ty, VarOrRVar tz,
                       Expr x_size, Expr y_size, Expr z_size,
                       TailStrategy tail,
                       DeviceAPI device_api) {
    split(x, bx, tx, x_size, tail);
    split(y, by, ty, y_size, tail);
    split(z, bz, tz, z_size, tail);
    // current order is:
    // tx bx ty by tz bz
    reorder(ty, bx);
    // tx ty bx by tz bz
    reorder(tz, bx);
    // tx ty tz by bx bz
    reorder(bx, by);
    // tx ty tz bx by bz
    set_dim_device_api(bx, device_api);
    set_dim_device_api(by, device_api);
    set_dim_device_api(bz, device_api);
    set_dim_device_api(tx, device_api);
    set_dim_device_api(ty, device_api);
    set_dim_device_api(tz, device_api);

    set_dim_type(bx, ForType::GPUBlock);
    set_dim_type(by, ForType::GPUBlock);
    set_dim_type(bz, ForType::GPUBlock);
    set_dim_type(tx, ForType::GPUThread);
    set_dim_type(ty, ForType::GPUThread);
    set_dim_type(tz, ForType::GPUThread);
    return *this;
}

Stage &Stage::gpu_tile(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                       VarOrRVar tx, VarOrRVar ty, VarOrRVar tz,
                       Expr x_size, Expr y_size, Expr z_size,
                       TailStrategy tail,
                       DeviceAPI device_api) {
    return gpu_tile(x, y, z, x, y, z, tx, ty, tz, x_size, y_size, z_size, tail, device_api);
}

Stage &Stage::gpu_tile(VarOrRVar x, Expr x_size, TailStrategy tail, DeviceAPI device_api) {
    VarOrRVar bx("__deprecated_block_id_x", x.is_rvar),
        tx("__deprecated_thread_id_x", x.is_rvar);
    split(x, bx, tx, x_size, tail);
    set_dim_device_api(bx, device_api);
    set_dim_device_api(tx, device_api);
    set_dim_type(bx, ForType::GPUBlock);
    set_dim_type(tx, ForType::GPUThread);
    return *this;
}


Stage &Stage::gpu_tile(VarOrRVar x, VarOrRVar y,
                       Expr x_size, Expr y_size,
                       TailStrategy tail,
                       DeviceAPI device_api) {
    VarOrRVar bx("__deprecated_block_id_x", x.is_rvar),
        by("__deprecated_block_id_y", y.is_rvar),
        tx("__deprecated_thread_id_x", x.is_rvar),
        ty("__deprecated_thread_id_y", y.is_rvar);
    return gpu_tile(x, y, bx, by, tx, ty, x_size, y_size, tail, device_api);
}

Stage &Stage::gpu_tile(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                       Expr x_size, Expr y_size, Expr z_size,
                       TailStrategy tail,
                       DeviceAPI device_api) {
    VarOrRVar bx("__deprecated_block_id_x", x.is_rvar),
        by("__deprecated_block_id_y", y.is_rvar),
        bz("__deprecated_block_id_z", z.is_rvar),
        tx("__deprecated_thread_id_x", x.is_rvar),
        ty("__deprecated_thread_id_y", y.is_rvar),
        tz("__deprecated_thread_id_z", z.is_rvar);
    return gpu_tile(x, y, z, bx, by, bz, tx, ty, tz, x_size, y_size, z_size, tail, device_api);
}

Stage &Stage::hexagon(VarOrRVar x) {
    set_dim_device_api(x, DeviceAPI::Hexagon);
    return *this;
}

Stage &Stage::prefetch(VarOrRVar var, Expr offset) {
    Prefetch prefetch = {var.name(), offset};
    definition.schedule().prefetches().push_back(prefetch);

    return *this;
}

void Func::invalidate_cache() {
    if (pipeline_.defined()) {
        pipeline_.invalidate_cache();
    }
}

Func Func::in(const Func &f) {
    invalidate_cache();
    user_assert(name() != f.name()) << "Cannot call 'in()' on itself\n";
    const map<string, IntrusivePtr<FunctionContents>> &wrappers = func.wrappers();
    const auto &iter = wrappers.find(f.name());
    if (iter == wrappers.end()) {
        Func wrapper(name() + "_in_" + f.name());
        wrapper(args()) = (*this)(args());
        func.add_wrapper(f.name(), wrapper.func);
        return wrapper;
    }

    IntrusivePtr<FunctionContents> wrapper_contents = iter->second;
    internal_assert(wrapper_contents.defined());

    // Make sure that no other Func shares the same wrapper as 'f'
    for (const auto &it : wrappers) {
        if (it.first == f.name()) {
            continue;
        }
        user_assert(!it.second.same_as(wrapper_contents))
            << "Redefinition of shared wrapper with " << it.first << " [" << name() << " -> "
            << Function(wrapper_contents).name() << "] in " << f.name() << " is not allowed\n";
    }
    Function wrapper(wrapper_contents);
    internal_assert(wrapper.frozen());
    return Func(wrapper);
}

Func Func::in(const vector<Func>& fs) {
    invalidate_cache();
    if (fs.empty()) {
        user_error << "Could not create a wrapper for an empty list of Funcs\n";
    }

    // Either all Funcs have the same wrapper or they don't already have any wrappers.
    // Otherwise, throw an error.
    const map<string, IntrusivePtr<FunctionContents>> &wrappers = func.wrappers();

    const auto &iter = wrappers.find(fs[0].name());
    if (iter == wrappers.end()) {
        // Make sure the other Funcs also don't have any wrappers
        for (size_t i = 1; i < fs.size(); ++i) {
            user_assert(wrappers.count(fs[i].name()) == 0)
                << "Cannot define the wrapper since " << fs[i].name()
                << " already has a wrapper while " << fs[0].name() << " doesn't \n";
        }
        Func wrapper(name() + "_wrapper");
        wrapper(args()) = (*this)(args());
        for (const Func &f : fs) {
            user_assert(name() != f.name()) << "Cannot call 'in()' on itself\n";
            func.add_wrapper(f.name(), wrapper.func);
        }
        return wrapper;
    }

    IntrusivePtr<FunctionContents> wrapper_contents = iter->second;
    internal_assert(wrapper_contents.defined());

    // Make sure all the other Funcs in 'fs' share the same wrapper and no other
    // Func not in 'fs' share the same wrapper.
    for (const auto &it : wrappers) {
        if (it.first == fs[0].name()) {
            continue;
        }
        const auto &fs_iter = std::find_if(
            fs.begin(), fs.end(), [&it](const Func& f) { return f.name() == it.first; });
        bool in_fs = fs_iter != fs.end();

        if (in_fs) {
            user_assert(it.second.same_as(wrapper_contents))
                << it.first << " should have shared the same wrapper as " << fs[0].name() << "\n";
        } else {
            user_assert(!it.second.same_as(wrapper_contents))
                << "Redefinition of shared wrapper [" << name() << " -> "
                << Function(wrapper_contents).name() << "] in " << fs[0].name() << " is illegal since "
                << it.first << " shares the same wrapper but not part of the redefinition\n";
        }
    }
    Function wrapper(wrapper_contents);
    internal_assert(wrapper.frozen());
    return Func(wrapper);
}

Func Func::in() {
    invalidate_cache();
    const map<string, IntrusivePtr<FunctionContents>> &wrappers = func.wrappers();
    const auto &iter = wrappers.find("");
    if (iter == wrappers.end()) {
        Func wrapper(name() + "_global_wrapper");
        wrapper(args()) = (*this)(args());
        func.add_wrapper("", wrapper.func);
        return wrapper;
    }

    IntrusivePtr<FunctionContents> wrapper_contents = iter->second;
    internal_assert(wrapper_contents.defined());
    Function wrapper(wrapper_contents);
    internal_assert(wrapper.frozen());
    return Func(wrapper);
}

Func &Func::split(VarOrRVar old, VarOrRVar outer, VarOrRVar inner, Expr factor, TailStrategy tail) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).split(old, outer, inner, factor, tail);
    return *this;
}

Func &Func::fuse(VarOrRVar inner, VarOrRVar outer, VarOrRVar fused) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).fuse(inner, outer, fused);
    return *this;
}

Func &Func::rename(VarOrRVar old_name, VarOrRVar new_name) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).rename(old_name, new_name);
    return *this;
}

Func &Func::allow_race_conditions() {
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).allow_race_conditions();
    return *this;
}

Func &Func::memoize() {
    invalidate_cache();
    func.schedule().memoized() = true;
    return *this;
}

Stage Func::specialize(Expr c) {
    invalidate_cache();
    return Stage(func.definition(), name(), args(), func.schedule().storage_dims()).specialize(c);
}

Func &Func::serial(VarOrRVar var) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).serial(var);
    return *this;
}

Func &Func::parallel(VarOrRVar var) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).parallel(var);
    return *this;
}

Func &Func::vectorize(VarOrRVar var) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).vectorize(var);
    return *this;
}

Func &Func::unroll(VarOrRVar var) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).unroll(var);
    return *this;
}

Func &Func::parallel(VarOrRVar var, Expr factor, TailStrategy tail) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).parallel(var, factor, tail);
    return *this;
}

Func &Func::vectorize(VarOrRVar var, int factor, TailStrategy tail) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).vectorize(var, factor, tail);
    return *this;
}

Func &Func::unroll(VarOrRVar var, int factor, TailStrategy tail) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).unroll(var, factor, tail);
    return *this;
}

Func &Func::bound(Var var, Expr min, Expr extent) {
    user_assert(!min.defined() || Int(32).can_represent(min.type())) << "Can't represent min bound in int32\n";
    user_assert(extent.defined()) << "Extent bound of a Func can't be undefined\n";
    user_assert(Int(32).can_represent(extent.type())) << "Can't represent extent bound in int32\n";

    if (min.defined()) {
        min = cast<int32_t>(min);
    }
    extent = cast<int32_t>(extent);

    invalidate_cache();
    bool found = false;
    for (size_t i = 0; i < func.args().size(); i++) {
        if (var.name() == func.args()[i]) {
            found = true;
        }
    }
    user_assert(found)
        << "Can't bound variable " << var.name()
        << " of function " << name()
        << " because " << var.name()
        << " is not one of the pure variables of " << name() << ".\n";

    Bound b = {var.name(), min, extent, Expr(), Expr()};
    func.schedule().bounds().push_back(b);
    return *this;
}

Func &Func::bound_extent(Var var, Expr extent) {
    return bound(var, Expr(), extent);
}

Func &Func::align_bounds(Var var, Expr modulus, Expr remainder) {
    user_assert(modulus.defined()) << "modulus is undefined\n";
    user_assert(remainder.defined()) << "remainder is undefined\n";
    user_assert(Int(32).can_represent(modulus.type())) << "Can't represent modulus as int32\n";
    user_assert(Int(32).can_represent(remainder.type())) << "Can't represent remainder as int32\n";

    modulus = cast<int32_t>(modulus);
    remainder = cast<int32_t>(remainder);

    // Reduce the remainder
    remainder = remainder % modulus;

    invalidate_cache();

    bool found = false;
    for (size_t i = 0; i < func.args().size(); i++) {
        if (var.name() == func.args()[i]) {
            found = true;
        }
    }
    user_assert(found)
        << "Can't align bounds of variable " << var.name()
        << " of function " << name()
        << " because " << var.name()
        << " is not one of the pure variables of " << name() << ".\n";

    Bound b = {var.name(), Expr(), Expr(), modulus, remainder};
    func.schedule().bounds().push_back(b);
    return *this;
}

Func &Func::tile(VarOrRVar x, VarOrRVar y,
                 VarOrRVar xo, VarOrRVar yo,
                 VarOrRVar xi, VarOrRVar yi,
                 Expr xfactor, Expr yfactor,
                 TailStrategy tail) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).tile(x, y, xo, yo, xi, yi, xfactor, yfactor, tail);
    return *this;
}

Func &Func::tile(VarOrRVar x, VarOrRVar y,
                 VarOrRVar xi, VarOrRVar yi,
                 Expr xfactor, Expr yfactor,
                 TailStrategy tail) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).tile(x, y, xi, yi, xfactor, yfactor, tail);
    return *this;
}

Func &Func::reorder(const std::vector<VarOrRVar> &vars) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).reorder(vars);
    return *this;
}

Func &Func::gpu_threads(VarOrRVar tx, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu_threads(tx, device_api);
    return *this;
}

Func &Func::gpu_threads(VarOrRVar tx, VarOrRVar ty, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu_threads(tx, ty, device_api);
    return *this;
}

Func &Func::gpu_threads(VarOrRVar tx, VarOrRVar ty, VarOrRVar tz, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu_threads(tx, ty, tz, device_api);
    return *this;
}

Func &Func::gpu_blocks(VarOrRVar bx, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu_blocks(bx, device_api);
    return *this;
}

Func &Func::gpu_blocks(VarOrRVar bx, VarOrRVar by, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu_blocks(bx, by, device_api);
    return *this;
}

Func &Func::gpu_blocks(VarOrRVar bx, VarOrRVar by, VarOrRVar bz, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu_blocks(bx, by, bz, device_api);
    return *this;
}

Func &Func::gpu_single_thread(DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu_single_thread(device_api);
    return *this;
}

Func &Func::gpu(VarOrRVar bx, VarOrRVar tx, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu(bx, tx, device_api);
    return *this;
}

Func &Func::gpu(VarOrRVar bx, VarOrRVar by, VarOrRVar tx, VarOrRVar ty, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu(bx, by, tx, ty, device_api);
    return *this;
}

Func &Func::gpu(VarOrRVar bx, VarOrRVar by, VarOrRVar bz, VarOrRVar tx, VarOrRVar ty, VarOrRVar tz, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu(bx, by, bz, tx, ty, tz, device_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, VarOrRVar bx, Var tx, int x_size, TailStrategy tail, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu_tile(x, bx, tx, x_size, tail, device_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, VarOrRVar bx, RVar tx, int x_size, TailStrategy tail, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu_tile(x, bx, tx, x_size, tail, device_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, VarOrRVar tx, int x_size, TailStrategy tail, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu_tile(x, tx, x_size, tail, device_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, VarOrRVar y,
                     VarOrRVar bx, VarOrRVar by,
                     VarOrRVar tx, VarOrRVar ty,
                     int x_size, int y_size,
                     TailStrategy tail,
                     DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims())
        .gpu_tile(x, y, bx, by, tx, ty, x_size, y_size, tail, device_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, VarOrRVar y,
                     VarOrRVar tx, Var ty,
                     int x_size, int y_size,
                     TailStrategy tail,
                     DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims())
        .gpu_tile(x, y, tx, ty, x_size, y_size, tail, device_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, VarOrRVar y,
                     VarOrRVar tx, RVar ty,
                     int x_size, int y_size,
                     TailStrategy tail,
                     DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims())
        .gpu_tile(x, y, tx, ty, x_size, y_size, tail, device_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                     VarOrRVar bx, VarOrRVar by, VarOrRVar bz,
                     VarOrRVar tx, VarOrRVar ty, VarOrRVar tz,
                     int x_size, int y_size, int z_size,
                     TailStrategy tail,
                     DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims())
        .gpu_tile(x, y, z, bx, by, bz, tx, ty, tz, x_size, y_size, z_size, tail, device_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                     VarOrRVar tx, VarOrRVar ty, VarOrRVar tz,
                     int x_size, int y_size, int z_size,
                     TailStrategy tail,
                     DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims())
        .gpu_tile(x, y, z, tx, ty, tz, x_size, y_size, z_size, tail, device_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, int x_size, TailStrategy tail, DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu_tile(x, x_size, tail, device_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, VarOrRVar y,
                     int x_size, int y_size,
                     TailStrategy tail,
                     DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu_tile(x, y, x_size, y_size, tail, device_api);
    return *this;
}

Func &Func::gpu_tile(VarOrRVar x, VarOrRVar y, VarOrRVar z,
                     int x_size, int y_size, int z_size,
                     TailStrategy tail,
                     DeviceAPI device_api) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu_tile(x, y, z, x_size, y_size, z_size, tail, device_api);
    return *this;
}

Func &Func::shader(Var x, Var y, Var c, DeviceAPI device_api) {
    invalidate_cache();

    reorder(c, x, y);
    // GLSL outputs must be stored interleaved
    reorder_storage(c, x, y);

    // TODO: Set appropriate constraints if this is the output buffer?

    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).gpu_blocks(x, y, device_api);

    bool constant_bounds = false;
    Schedule &sched = func.schedule();
    for (size_t i = 0; i < sched.bounds().size(); i++) {
        if (c.name() == sched.bounds()[i].var) {
            constant_bounds = is_const(sched.bounds()[i].min) &&
                is_const(sched.bounds()[i].extent);
            break;
        }
    }
    user_assert(constant_bounds)
        << "The color channel for image loops must have constant bounds, e.g., .bound(c, 0, 3).\n";
    return *this;
}

Func &Func::glsl(Var x, Var y, Var c) {
    return shader(x, y, c, DeviceAPI::GLSL).vectorize(c);
}

Func &Func::hexagon(VarOrRVar x) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).hexagon(x);
    return *this;
}

Func &Func::prefetch(VarOrRVar var, Expr offset) {
    invalidate_cache();
    Stage(func.definition(), name(), args(), func.schedule().storage_dims()).prefetch(var, offset);
    return *this;
}

Func &Func::reorder_storage(Var x, Var y) {
    invalidate_cache();

    vector<StorageDim> &dims = func.schedule().storage_dims();
    bool found_y = false;
    size_t y_loc = 0;
    for (size_t i = 0; i < dims.size(); i++) {
        if (var_name_match(dims[i].var, y.name())) {
            found_y = true;
            y_loc = i;
        } else if (var_name_match(dims[i].var, x.name())) {
            if (found_y) std::swap(dims[i], dims[y_loc]);
            return *this;
        }
    }
    user_error << "Could not find variables " << x.name()
               << " and " << y.name() << " to reorder in schedule.\n";
    return *this;
}

Func &Func::reorder_storage(const std::vector<Var> &dims, size_t start) {
    // Reorder the first dimension with respect to all others, then
    // recursively reorder all remaining dimensions.
    for (size_t i = start + 1; i < dims.size(); i++) {
        reorder_storage(dims[start], dims[i]);
    }
    if ((dims.size() - start) > 2) {
        reorder_storage(dims, start + 1);
    }
    return *this;
}

Func &Func::reorder_storage(const std::vector<Var> &dims) {
    user_assert(dims.size() > 1) <<
        "reorder_storage must have at least two dimensions in reorder list.\n";

    return reorder_storage(dims, 0);
}

Func &Func::align_storage(Var dim, Expr alignment) {
    invalidate_cache();

    vector<StorageDim> &dims = func.schedule().storage_dims();
    for (size_t i = 0; i < dims.size(); i++) {
        if (var_name_match(dims[i].var, dim.name())) {
            dims[i].alignment = alignment;
            return *this;
        }
    }
    user_error << "Could not find variable " << dim.name()
               << " to align the storage of.\n";
    return *this;
}

Func &Func::fold_storage(Var dim, Expr factor, bool fold_forward) {
    invalidate_cache();

    vector<StorageDim> &dims = func.schedule().storage_dims();
    for (size_t i = 0; i < dims.size(); i++) {
        if (var_name_match(dims[i].var, dim.name())) {
            dims[i].fold_factor = factor;
            dims[i].fold_forward = fold_forward;
            return *this;
        }
    }
    user_error << "Could not find variable " << dim.name()
               << " to fold the storage of.\n";
    return *this;
}

Func &Func::compute_at(LoopLevel loop_level) {
    invalidate_cache();
    func.schedule().compute_level() = loop_level;
    if (func.schedule().store_level().is_inline()) {
        func.schedule().store_level() = loop_level;
    }
    return *this;
}

Func &Func::compute_at(Func f, RVar var) {
    return compute_at(LoopLevel(f, var));
}

Func &Func::compute_at(Func f, Var var) {
    return compute_at(LoopLevel(f, var));
}

Func &Func::compute_root() {
    return compute_at(LoopLevel::root());
}

Func &Func::store_at(LoopLevel loop_level) {
    invalidate_cache();
    func.schedule().store_level() = loop_level;
    return *this;
}

Func &Func::store_at(Func f, RVar var) {
    return store_at(LoopLevel(f, var));
}

Func &Func::store_at(Func f, Var var) {
    return store_at(LoopLevel(f, var));
}

Func &Func::store_root() {
    return store_at(LoopLevel::root());
}

Func &Func::compute_inline() {
    return compute_at(LoopLevel());
}

Func &Func::trace_loads() {
    invalidate_cache();
    func.trace_loads();
    return *this;
}

Func &Func::trace_stores() {
    invalidate_cache();
    func.trace_stores();
    return *this;
}

Func &Func::trace_realizations() {
    invalidate_cache();
    func.trace_realizations();
    return *this;
}

void Func::debug_to_file(const string &filename) {
    invalidate_cache();
    func.debug_file() = filename;
}

Stage Func::update(int idx) {
    user_assert(idx < num_update_definitions()) <<
      "Call to update with index larger than last defined update stage for Func \"" <<
      name() << "\".\n";
    invalidate_cache();
    return Stage(func.update(idx),
                 name() + ".update(" + std::to_string(idx) + ")",
                 args(),
                 func.schedule().storage_dims());
}

Func::operator Stage() const {
    return Stage(func.definition(), name(), args(), func.schedule().storage_dims());
}

namespace {
class CountImplicitVars : public Internal::IRGraphVisitor {
public:
    int count;

    CountImplicitVars(const vector<Expr> &e) : count(0) {
        for (size_t i = 0; i < e.size(); i++) {
            e[i].accept(this);
        }
    }

    using IRGraphVisitor::visit;

    void visit(const Variable *v) {
        int index = Var::implicit_index(v->name);
        if (index != -1) {
            if (index >= count) count = index + 1;
        }
    }
};
}

FuncRef::FuncRef(Internal::Function f, const vector<Expr> &a, int placeholder_pos,
                 int count) : func(f), implicit_count(count), args(a){
    implicit_placeholder_pos = placeholder_pos;
    Internal::check_call_arg_types(f.name(), &args, args.size());
}

FuncRef::FuncRef(Internal::Function f, const vector<Var> &a, int placeholder_pos,
                 int count) : func(f), implicit_count(count) {
    implicit_placeholder_pos = placeholder_pos;
    args.resize(a.size());
    for (size_t i = 0; i < a.size(); i++) {
        args[i] = a[i];
    }
}

vector<Expr> FuncRef::args_with_implicit_vars(const vector<Expr> &e) const {
    vector<Expr> a = args;

    for (size_t i = 0; i < a.size(); i++) {
        user_assert(a[i].defined())
            << "Argument " << (i+1) << " in call to \"" << func.name() << "\" is undefined.\n";
    }
    for (size_t i = 0; i < e.size(); i++) {
        user_assert(e[i].defined())
            << "Value " << (i+1) << " in definition of \"" << func.name() << "\" is undefined.\n";
    }

    CountImplicitVars count(e);
    for (size_t i = 0; i < a.size(); i++) {
        a[i].accept(&count);
    }

    if (count.count > 0) {
        if (func.has_pure_definition()) {
            // If the func already has pure definition, the number of implicit
            // vars in the RHS can only be at most the number of implicit vars
            // in the LHS.
            user_assert(implicit_count >= count.count)
                << "The update definition of " << func.name() << " uses " << count.count
                << " implicit variables, but the initial definition uses only "
                << implicit_count << " implicit variables.\n";
        } else if (implicit_placeholder_pos != -1) {
            internal_assert(implicit_count == 0)
                << "Pure definition can't possibly already have implicit variables defined\n";

            Internal::debug(2) << "Adding " << count.count << " implicit vars to LHS of " << func.name() << "\n";

            vector<Expr>::iterator iter = a.begin() + implicit_placeholder_pos;
            for (int i = 0; i < count.count; i++) {
                iter = a.insert(iter, Var::implicit(i));
                iter++;
            }
        }
    }

    // Check the implicit vars in the RHS also exist in the LHS
    for (int i = 0; i < count.count; i++) {
        Var v = Var::implicit(i);
        bool found = false;
        for (size_t j = 0; j < a.size(); j++) {
            if (const Variable *arg = a[j].as<Variable>()) {
                if (arg->name == v.name()) {
                    found = true;
                }
            }
        }
        user_assert(found)
            << "Right-hand-side of update definition of " << func.name()
            << " uses implicit variables, but the left-hand-side does not"
            << " contain the placeholder symbol '_'.\n";
    }

    return a;
}

Stage FuncRef::operator=(Expr e) {
    return (*this) = Tuple(e);
}

Stage FuncRef::operator=(const Tuple &e) {
    if (!func.has_pure_definition()) {
        for (size_t i = 0; i < args.size(); ++i) {
            const Variable *var = args[i].as<Variable>();
            user_assert((var != nullptr) && (!var->reduction_domain.defined()))
                << "Argument " << (i+1) << " in initial definition of \""
                << func.name() << "\" is not a Var.\n";
        }

        // Find implicit args in the expr and add them to the args list before calling define
        vector<Expr> expanded_args = args_with_implicit_vars(e.as_vector());
        vector<string> expanded_args_str(expanded_args.size());
        for (size_t i = 0; i < expanded_args.size(); ++i) {
            const Variable *v = expanded_args[i].as<Variable>();
            internal_assert(v);
            expanded_args_str[i] = v->name;
        }
        func.define(expanded_args_str, e.as_vector());
        return Stage(func.definition(), func.name(), func.args(), func.schedule().storage_dims());

    } else {
        func.define_update(args, e.as_vector());

        size_t update_stage = func.updates().size() - 1;
        return Stage(func.update(update_stage),
                     func.name() + ".update(" + std::to_string(update_stage) + ")",
                     func.args(),
                     func.schedule().storage_dims());
    }
}

Stage FuncRef::operator=(const FuncRef &e) {
    if (e.size() == 1) {
        return (*this) = Expr(e);
    } else {
        return (*this) = Tuple(e);
    }
}

// Inject a suitable base-case definition given an update
// definition. This is a helper for FuncRef::operator+= and co.
Func define_base_case(Internal::Function func, const vector<Expr> &a, const Tuple &e) {
    Func f(func);

    if (func.has_pure_definition()) return f;
    vector<Var> pure_args(a.size());

    // Reuse names of existing pure args
    for (size_t i = 0; i < a.size(); i++) {
        if (const Variable *v = a[i].as<Variable>()) {
            if (!v->param.defined()) {
                pure_args[i] = Var(v->name);
            }
        } else {
            pure_args[i] = Var();
        }
    }

    f(pure_args) = e;
    return f;
}

Func define_base_case(Internal::Function func, const vector<Expr> &a, Expr e) {
    return define_base_case(func, a, Tuple(e));
}

template <typename BinaryOp>
Stage FuncRef::func_ref_update(const Tuple &e, int init_val) {
    internal_assert(e.size() > 1);

    vector<Expr> init_values(e.size());
    for (int i = 0; i < (int)init_values.size(); ++i) {
        init_values[i] = cast(e[i].type(), init_val);
    }
    vector<Expr> expanded_args = args_with_implicit_vars(e.as_vector());
    FuncRef self_ref = define_base_case(func, expanded_args, Tuple(init_values))(expanded_args);

    vector<Expr> values(e.size());
    for (int i = 0; i < (int)values.size(); ++i) {
        values[i] = BinaryOp()(self_ref[i], e[i]);
    }
    return self_ref = Tuple(values);
}

template <typename BinaryOp>
Stage FuncRef::func_ref_update(Expr e, int init_val) {
    vector<Expr> expanded_args = args_with_implicit_vars({e});
    FuncRef self_ref = define_base_case(func, expanded_args, cast(e.type(), init_val))(expanded_args);
    return self_ref = BinaryOp()(Expr(self_ref), e);
}

Stage FuncRef::operator+=(Expr e) {
    return func_ref_update<std::plus<Expr>>(e, 0);
}

Stage FuncRef::operator+=(const Tuple &e) {
    if (e.size() == 1) {
        return (*this) += e[0];
    } else {
        return func_ref_update<std::plus<Expr>>(e, 0);
    }
}

Stage FuncRef::operator+=(const FuncRef &e) {
    if (e.size() == 1) {
        return (*this) += Expr(e);
    } else {
        return (*this) += Tuple(e);
    }
}

Stage FuncRef::operator*=(Expr e) {
    return func_ref_update<std::multiplies<Expr>>(e, 1);
}

Stage FuncRef::operator*=(const Tuple &e) {
    if (e.size() == 1) {
        return (*this) *= e[0];
    } else {
        return func_ref_update<std::multiplies<Expr>>(e, 1);
    }
}

Stage FuncRef::operator*=(const FuncRef &e) {
    if (e.size() == 1) {
        return (*this) *= Expr(e);
    } else {
        return (*this) *= Tuple(e);
    }
}

Stage FuncRef::operator-=(Expr e) {
    return func_ref_update<std::minus<Expr>>(e, 0);
}

Stage FuncRef::operator-=(const Tuple &e) {
    if (e.size() == 1) {
        return (*this) -= e[0];
    } else {
        return func_ref_update<std::minus<Expr>>(e, 0);
    }
}

Stage FuncRef::operator-=(const FuncRef &e) {
    if (e.size() == 1) {
        return (*this) -= Expr(e);
    } else {
        return (*this) -= Tuple(e);
    }
}

Stage FuncRef::operator/=(Expr e) {
    return func_ref_update<std::divides<Expr>>(e, 1);
}

Stage FuncRef::operator/=(const Tuple &e) {
    if (e.size() == 1) {
        return (*this) /= e[0];
    } else {
        return func_ref_update<std::divides<Expr>>(e, 1);
    }
}

Stage FuncRef::operator/=(const FuncRef &e) {
    if (e.size() == 1) {
        return (*this) /= Expr(e);
    } else {
        return (*this) /= Tuple(e);
    }
}

FuncRef::operator Expr() const {
    user_assert(func.has_pure_definition() || func.has_extern_definition())
        << "Can't call Func \"" << func.name() << "\" because it has not yet been defined.\n";

    user_assert(func.outputs() == 1)
        << "Can't convert a reference Func \"" << func.name()
        << "\" to an Expr, because " << func.name() << " returns a Tuple.\n";

    return Call::make(func, args);
}

FuncTupleElementRef FuncRef::operator[](int i) const {
    user_assert(func.has_pure_definition() || func.has_extern_definition())
        << "Can't call Func \"" << func.name() << "\" because it has not yet been defined.\n";

    user_assert(func.outputs() != 1)
        << "Can't index into a reference to Func \"" << func.name()
        << "\", because it does not return a Tuple.\n";

    user_assert(i >= 0 && i < func.outputs())
        << "Tuple index out of range in reference to Func \"" << func.name() << "\".\n";

    return FuncTupleElementRef(*this, args, i);
}

size_t FuncRef::size() const {
    return func.outputs();
}

FuncTupleElementRef::FuncTupleElementRef(
        const FuncRef &ref, const std::vector<Expr>& args, int idx)
        : func_ref(ref), args(args), idx(idx) {
    internal_assert(func_ref.size() > 1)
        << "Func " << ref.function().name() << " does not return a Tuple\n";
    internal_assert(idx >= 0 && idx < (int)func_ref.size());
}

Tuple FuncTupleElementRef::values_with_undefs(Expr e) const {
    vector<Expr> values(func_ref.size());
    for (int i = 0; i < (int)values.size(); ++i) {
        if (i == idx) {
            values[i] = e;
        } else {
            Type t = func_ref.function().values()[i].type();
            values[i] = undef(t);
        }
    }
    return Tuple(values);
}

Stage FuncTupleElementRef::operator=(Expr e) {
    return func_ref = values_with_undefs(e);
}

Stage FuncTupleElementRef::operator+=(Expr e) {
    return func_ref += values_with_undefs(e);
}

Stage FuncTupleElementRef::operator*=(Expr e) {
    return func_ref *= values_with_undefs(e);
}

Stage FuncTupleElementRef::operator-=(Expr e) {
    return func_ref -= values_with_undefs(e);
}

Stage FuncTupleElementRef::operator/=(Expr e) {
    return func_ref /= values_with_undefs(e);
}

Stage FuncTupleElementRef::operator=(const FuncRef &e) {
    return func_ref = values_with_undefs(e);
}

FuncTupleElementRef::operator Expr() const {
    return Internal::Call::make(func_ref.function(), args, idx);
}

Realization Func::realize(std::vector<int32_t> sizes, const Target &target) {
    user_assert(defined()) << "Can't realize undefined Func.\n";
    return pipeline().realize(sizes, target);
}

Realization Func::realize(int x_size, int y_size, int z_size, int w_size, const Target &target) {
    return realize({x_size, y_size, z_size, w_size}, target);
}

Realization Func::realize(int x_size, int y_size, int z_size, const Target &target) {
    return realize({x_size, y_size, z_size}, target);
}

Realization Func::realize(int x_size, int y_size, const Target &target) {
    return realize({x_size, y_size}, target);
}

Realization Func::realize(int x_size, const Target &target) {
    return realize(std::vector<int>{x_size}, target);
}

Realization Func::realize(const Target &target) {
    return realize(std::vector<int>{}, target);
}

void Func::infer_input_bounds(int x_size, int y_size, int z_size, int w_size) {
    user_assert(defined()) << "Can't infer input bounds on an undefined Func.\n";
    vector<Buffer<>> outputs(func.outputs());
    int sizes[] = {x_size, y_size, z_size, w_size};
    for (size_t i = 0; i < outputs.size(); i++) {
        // We're not actually going to read from these outputs, so
        // make the allocation tiny, then expand them with unsafe
        // cropping.
        Buffer<> im = Buffer<>::make_scalar(func.output_types()[i]);
        for (int s : sizes) {
            if (!s) break;
            im.add_dimension();
            im.crop(im.dimensions()-1, 0, s);
        }
        outputs[i] = std::move(im);
    }
    Realization r(outputs);
    infer_input_bounds(r);
}

OutputImageParam Func::output_buffer() const {
    user_assert(defined())
        << "Can't access output buffer of undefined Func.\n";
    user_assert(func.output_buffers().size() == 1)
        << "Can't call Func::output_buffer on Func \"" << name()
        << "\" because it returns a Tuple.\n";
    return OutputImageParam(func.output_buffers()[0], Argument::OutputBuffer);
}

vector<OutputImageParam> Func::output_buffers() const {
    user_assert(defined())
        << "Can't access output buffers of undefined Func.\n";

    vector<OutputImageParam> bufs(func.output_buffers().size());
    for (size_t i = 0; i < bufs.size(); i++) {
        bufs[i] = OutputImageParam(func.output_buffers()[i], Argument::OutputBuffer);
    }
    return bufs;
}

Pipeline Func::pipeline() {
    if (!pipeline_.defined()) {
        pipeline_ = Pipeline(*this);
    }
    internal_assert(pipeline_.defined());
    return pipeline_;
}

vector<Argument> Func::infer_arguments() const {
    return Pipeline(*this).infer_arguments();
}

Module Func::compile_to_module(const vector<Argument> &args, const std::string &fn_name, const Target &target) {
    return pipeline().compile_to_module(args, fn_name, target);
}


void Func::compile_to(const Outputs &output_files,
                      const vector<Argument> &args,
                      const string &fn_name,
                      const Target &target) {
    pipeline().compile_to(output_files, args, fn_name, target);
}

void Func::compile_to_bitcode(const string &filename, const vector<Argument> &args, const string &fn_name,
                              const Target &target) {
    pipeline().compile_to_bitcode(filename, args, fn_name, target);
}

void Func::compile_to_bitcode(const string &filename, const vector<Argument> &args,
                              const Target &target) {
    pipeline().compile_to_bitcode(filename, args, "", target);
}

void Func::compile_to_llvm_assembly(const string &filename, const vector<Argument> &args, const string &fn_name,
                                    const Target &target) {
    pipeline().compile_to_llvm_assembly(filename, args, fn_name, target);
}

void Func::compile_to_llvm_assembly(const string &filename, const vector<Argument> &args,
                                    const Target &target) {
    pipeline().compile_to_llvm_assembly(filename, args, "", target);
}

void Func::compile_to_object(const string &filename, const vector<Argument> &args,
                             const string &fn_name, const Target &target) {
    pipeline().compile_to_object(filename, args, fn_name, target);
}

void Func::compile_to_object(const string &filename, const vector<Argument> &args,
                             const Target &target) {
    pipeline().compile_to_object(filename, args, "", target);
}

void Func::compile_to_header(const string &filename, const vector<Argument> &args,
                             const string &fn_name, const Target &target) {
    pipeline().compile_to_header(filename, args, fn_name, target);
}

void Func::compile_to_c(const string &filename, const vector<Argument> &args,
                        const string &fn_name, const Target &target) {
    pipeline().compile_to_c(filename, args, fn_name, target);
}

void Func::compile_to_lowered_stmt(const string &filename,
                                   const vector<Argument> &args,
                                   StmtOutputFormat fmt,
                                   const Target &target) {
    pipeline().compile_to_lowered_stmt(filename, args, fmt, target);
}

void Func::print_loop_nest() {
    pipeline().print_loop_nest();
}

void Func::compile_to_file(const string &filename_prefix,
                           const vector<Argument> &args,
                           const std::string &fn_name,
                           const Target &target) {
    pipeline().compile_to_file(filename_prefix, args, fn_name, target);
}

void Func::compile_to_static_library(const string &filename_prefix,
                                     const vector<Argument> &args,
                                     const std::string &fn_name,
                                     const Target &target) {
    pipeline().compile_to_static_library(filename_prefix, args, fn_name, target);
}

void Func::compile_to_multitarget_static_library(const std::string &filename_prefix,
                                                 const std::vector<Argument> &args,
                                                 const std::vector<Target> &targets) {
    pipeline().compile_to_multitarget_static_library(filename_prefix, args, targets);
}

void Func::compile_to_assembly(const string &filename, const vector<Argument> &args, const string &fn_name,
                               const Target &target) {
    pipeline().compile_to_assembly(filename, args, fn_name, target);
}

void Func::compile_to_assembly(const string &filename, const vector<Argument> &args, const Target &target) {
    pipeline().compile_to_assembly(filename, args, "", target);
}

// JIT-related code

void Func::set_error_handler(void (*handler)(void *, const char *)) {
    pipeline().set_error_handler(handler);
}

void Func::set_custom_allocator(void *(*cust_malloc)(void *, size_t),
                                void (*cust_free)(void *, void *)) {
    pipeline().set_custom_allocator(cust_malloc, cust_free);
}

void Func::set_custom_do_par_for(int (*cust_do_par_for)(void *, int (*)(void *, int, uint8_t *), int, int, uint8_t *)) {
    pipeline().set_custom_do_par_for(cust_do_par_for);
}

void Func::set_custom_do_task(int (*cust_do_task)(void *, int (*)(void *, int, uint8_t *), int, uint8_t *)) {
    pipeline().set_custom_do_task(cust_do_task);
}

void Func::set_custom_trace(int (*trace_fn)(void *, const halide_trace_event_t *)) {
    pipeline().set_custom_trace(trace_fn);
}

void Func::set_custom_print(void (*cust_print)(void *, const char *)) {
    pipeline().set_custom_print(cust_print);
}

void Func::add_custom_lowering_pass(IRMutator *pass, void (*deleter)(IRMutator *)) {
    pipeline().add_custom_lowering_pass(pass, deleter);
}

void Func::clear_custom_lowering_passes() {
    pipeline().clear_custom_lowering_passes();
}

const vector<CustomLoweringPass> &Func::custom_lowering_passes() {
    return pipeline().custom_lowering_passes();
}

const Internal::JITHandlers &Func::jit_handlers() {
    return pipeline().jit_handlers();
}

void Func::realize(Realization dst, const Target &target) {
    pipeline().realize(dst, target);
}

void Func::infer_input_bounds(Realization dst) {
    pipeline().infer_input_bounds(dst);
}

void *Func::compile_jit(const Target &target) {
    return pipeline().compile_jit(target);
}

EXPORT Var _("_");
EXPORT Var _0("_0"), _1("_1"), _2("_2"), _3("_3"), _4("_4"),
           _5("_5"), _6("_6"), _7("_7"), _8("_8"), _9("_9");

}
