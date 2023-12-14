#include "BoundsInference.h"
#include "Bounds.h"
#include "CSE.h"
#include "ExprUsesVar.h"
#include "ExternFuncArgument.h"
#include "Function.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Inline.h"
#include "Qualify.h"
#include "Scope.h"
#include "Simplify.h"

#include <algorithm>
#include <iterator>

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace {

bool var_name_match(const string &candidate, const string &var) {
    internal_assert(var.find('.') == string::npos)
        << "var_name_match expects unqualified names for the second argument. "
        << "Name passed: " << var << "\n";
    return (candidate == var) || Internal::ends_with(candidate, "." + var);
}

class DependsOnBoundsInference : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Variable *var) override {
        if (ends_with(var->name, ".max") ||
            ends_with(var->name, ".min")) {
            result = true;
        }
    }

    void visit(const Call *op) override {
        if (op->name == Call::buffer_get_min ||
            op->name == Call::buffer_get_max) {
            result = true;
        } else {
            IRVisitor::visit(op);
        }
    }

public:
    bool result = false;
    DependsOnBoundsInference() = default;
};

bool depends_on_bounds_inference(const Expr &e) {
    DependsOnBoundsInference d;
    e.accept(&d);
    return d.result;
}

/** Compute the bounds of the value of some variable defined by an
 * inner let stmt or for loop. E.g. for the stmt:
 *
 *
 * for x from 0 to 10:
 *  let y = x + 2;
 *
 * bounds_of_inner_var(y) would return 2 to 12, and
 * bounds_of_inner_var(x) would return 0 to 10.
 */
class BoundsOfInnerVar : public IRVisitor {
public:
    Interval result;
    BoundsOfInnerVar(const string &v)
        : var(v) {
    }

private:
    string var;
    bool found = false;

    using IRVisitor::visit;

    void visit(const LetStmt *op) override {
        if (op->name == var) {
            result = Interval::single_point(op->value);
            found = true;
        } else if (!found) {
            op->body.accept(this);
            if (found) {
                if (expr_uses_var(result.min, op->name)) {
                    result.min = Let::make(op->name, op->value, result.min);
                }
                if (expr_uses_var(result.max, op->name)) {
                    result.max = Let::make(op->name, op->value, result.max);
                }
            }
        }
    }

    void visit(const Block *op) override {
        // We're most likely to find our var at the end of a
        // block. The start of the block could be unrelated producers.
        op->rest.accept(this);
        if (!found) {
            op->first.accept(this);
        }
    }

    void visit(const For *op) override {
        // At this stage of lowering, loop_min and loop_max
        // conveniently exist in scope.
        Interval in(Variable::make(Int(32), op->name + ".loop_min"),
                    Variable::make(Int(32), op->name + ".loop_max"));

        if (op->name == var) {
            result = in;
            found = true;
        } else if (!found) {
            op->body.accept(this);
            if (found) {
                Scope<Interval> scope;
                scope.push(op->name, in);
                if (expr_uses_var(result.min, op->name)) {
                    result.min = bounds_of_expr_in_scope(result.min, scope).min;
                }
                if (expr_uses_var(result.max, op->name)) {
                    result.max = bounds_of_expr_in_scope(result.max, scope).max;
                }
            }
        }
    }
};

Interval bounds_of_inner_var(const string &var, const Stmt &s) {
    BoundsOfInnerVar b(var);
    s.accept(&b);
    return b.result;
}

size_t find_fused_group_index(const Function &producing_func,
                              const vector<vector<Function>> &fused_groups) {
    const auto &iter = std::find_if(fused_groups.begin(), fused_groups.end(),
                                    [&producing_func](const vector<Function> &group) {
                                        return std::any_of(group.begin(), group.end(),
                                                           [&producing_func](const Function &f) {
                                                               return (f.name() == producing_func.name());
                                                           });
                                    });
    internal_assert(iter != fused_groups.end());
    return iter - fused_groups.begin();
}

// Determine if the current producing stage is fused with other
// stage (i.e. the consumer stage) at dimension 'var'.
bool is_fused_with_others(const vector<vector<Function>> &fused_groups,
                          const vector<set<FusedPair>> &fused_pairs_in_groups,
                          const Function &producing_func, int producing_stage_index,
                          const string &consumer_name, int consumer_stage,
                          string var) {
    if (producing_func.has_extern_definition()) {
        return false;
    }

    // Find the fused group this producing stage belongs to.
    size_t index = find_fused_group_index(producing_func, fused_groups);

    const vector<Dim> &dims = (producing_stage_index == 0) ? producing_func.definition().schedule().dims() : producing_func.update(producing_stage_index - 1).schedule().dims();

    size_t var_index;
    {
        const auto &iter = std::find_if(dims.begin(), dims.end(),
                                        [&var](const Dim &d) { return var_name_match(d.var, var); });
        if (iter == dims.end()) {
            return false;
        }
        var_index = iter - dims.begin();
    }

    // Iterate over the fused pair list to check if the producer stage
    // is fused with the consumer stage at 'var'
    for (const auto &pair : fused_pairs_in_groups[index]) {
        if (((pair.func_1 == consumer_name) && ((int)pair.stage_1 == consumer_stage)) ||
            ((pair.func_2 == consumer_name) && ((int)pair.stage_2 == consumer_stage))) {
            const auto &iter = std::find_if(dims.begin(), dims.end(),
                                            [&pair](const Dim &d) { return var_name_match(d.var, pair.var_name); });
            if (iter == dims.end()) {
                continue;
            }
            size_t idx = iter - dims.begin();
            if (var_index >= idx) {
                return true;
            }
        }
    }
    return false;
}

// An inliner that can inline an entire set of functions at once. The inliner in
// Inline.h only handles with one function at a time.
class Inliner : public IRMutator {
public:
    std::set<Function, Function::Compare> to_inline;

    Expr do_inlining(const Expr &e) {
        return common_subexpression_elimination(mutate(e));
    }

protected:
    std::map<Function, std::map<int, Expr>, Function::Compare> qualified_bodies;

    Expr get_qualified_body(const Function &f, int idx) {
        auto it = qualified_bodies.find(f);
        if (it != qualified_bodies.end()) {
            auto it2 = it->second.find(idx);
            if (it2 != it->second.end()) {
                return it2->second;
            }
        }
        Expr e = qualify(f.name() + ".", f.values()[idx]);
        e = do_inlining(e);
        qualified_bodies[f][idx] = e;
        return e;
    }

    Expr visit(const Call *op) override {
        if (op->func.defined()) {
            Function f(op->func);
            if (to_inline.count(f)) {
                auto args = mutate(op->args);
                Expr body = get_qualified_body(f, op->value_index);
                const vector<string> &func_args = f.args();
                for (size_t i = 0; i < args.size(); i++) {
                    body = Let::make(f.name() + "." + func_args[i], args[i], body);
                }
                return body;
            }
        }
        return IRMutator::visit(op);
    }

    using IRMutator::visit;
};

class BoundsInference : public IRMutator {
public:
    const vector<Function> &funcs;
    // Each element in the list indicates a group of functions which loops
    // are fused together.
    const vector<vector<Function>> &fused_groups;
    // Contain list of all pairwise fused function stages for each fused group.
    // The fused group is indexed in the same way as 'fused_groups'.
    const vector<set<FusedPair>> &fused_pairs_in_groups;
    const FuncValueBounds &func_bounds;
    set<string> in_pipeline, inner_productions, has_extern_consumer;
    const Target target;

    Inliner inliner;

    struct CondValue {
        Expr cond;  // Condition on params only (can't depend on loop variable)
        Expr value;

        CondValue(const Expr &c, const Expr &v)
            : cond(c), value(v) {
        }
    };

    struct Stage {
        Function func;
        size_t stage;  // 0 is the pure definition, 1 is the first update
        string name;
        vector<int> consumers;
        map<pair<string, int>, Box> bounds;
        vector<CondValue> exprs;
        set<ReductionVariable, ReductionVariable::Compare> rvars;
        string stage_prefix;
        size_t fused_group_index;
        Inliner *inliner;

        // Computed expressions on the left and right-hand sides.
        // Note that a function definition might have different LHS or reduction domain
        // (if it's an update def) or RHS per specialization. All specializations
        // of an init definition should have the same LHS.
        // This also pushes all the reduction domains it encounters into the 'rvars'
        // set for later use.
        vector<vector<CondValue>> compute_exprs_helper(const Definition &def, bool is_update) {
            vector<vector<CondValue>> result(2);  // <args, values>

            if (!def.defined()) {
                return result;
            }

            // Default case (no specialization)
            vector<Expr> predicates = def.split_predicate();
            for (const ReductionVariable &rv : def.schedule().rvars()) {
                rvars.insert(rv);
            }

            vector<vector<Expr>> vecs(2);
            if (is_update) {
                vecs[0] = def.args();
            }
            vecs[1] = def.values();

            for (size_t i = 0; i < result.size(); ++i) {
                for (const Expr &val : vecs[i]) {
                    if (!predicates.empty()) {
                        Expr cond_val = Call::make(val.type(),
                                                   Internal::Call::if_then_else,
                                                   {likely(predicates[0]), val},
                                                   Internal::Call::PureIntrinsic);
                        for (size_t i = 1; i < predicates.size(); ++i) {
                            cond_val = Call::make(cond_val.type(),
                                                  Internal::Call::if_then_else,
                                                  {likely(predicates[i]), cond_val},
                                                  Internal::Call::PureIntrinsic);
                        }
                        result[i].emplace_back(const_true(), cond_val);
                    } else {
                        result[i].emplace_back(const_true(), val);
                    }
                }
            }

            const vector<Specialization> &specializations = def.specializations();
            for (size_t i = specializations.size(); i > 0; i--) {
                Expr s_cond = specializations[i - 1].condition;
                const Definition &s_def = specializations[i - 1].definition;

                // Else case (i.e. specialization condition is false)
                for (auto &vec : result) {
                    for (CondValue &cval : vec) {
                        cval.cond = simplify(!s_cond && cval.cond);
                    }
                }

                // Then case (i.e. specialization condition is true)
                vector<vector<CondValue>> s_result = compute_exprs_helper(s_def, is_update);
                for (auto &vec : s_result) {
                    for (CondValue &cval : vec) {
                        cval.cond = simplify(s_cond && cval.cond);
                    }
                }
                for (size_t i = 0; i < result.size(); i++) {
                    result[i].insert(result[i].end(), s_result[i].begin(), s_result[i].end());
                }
            }

            // Optimization: If the args/values across specializations including
            // the default case, are the same, we can combine those args/values
            // into one arg/value with a const_true() condition for the purpose
            // of bounds inference.
            for (auto &vec : result) {
                if (vec.size() > 1) {
                    bool all_equal = true;
                    Expr val = vec[0].value;
                    for (size_t i = 1; i < vec.size(); ++i) {
                        if (!equal(val, vec[i].value)) {
                            all_equal = false;
                            break;
                        }
                    }
                    if (all_equal) {
                        debug(4) << "compute_exprs: all values (size: " << vec.size() << ") "
                                 << "(" << val << ") are equal, combine them together\n";
                        internal_assert(val.defined());
                        vec.clear();
                        vec.emplace_back(const_true(), val);
                    }
                }
            }
            return result;
        }

        // Computed expressions on the left and right-hand sides. This also
        // pushes all reduction domains it encounters into the 'rvars' set
        // for later use.
        void compute_exprs() {
            // We need to clear 'exprs' and 'rvars' first, in case compute_exprs()
            // is called multiple times.
            exprs.clear();
            rvars.clear();

            bool is_update = (stage != 0);
            vector<vector<CondValue>> result;
            if (!is_update) {
                result = compute_exprs_helper(func.definition(), is_update);
            } else {
                const Definition &def = func.update(stage - 1);
                result = compute_exprs_helper(def, is_update);
            }
            internal_assert(result.size() == 2);
            exprs = result[0];

            if (func.extern_definition_proxy_expr().defined()) {
                exprs.emplace_back(const_true(), func.extern_definition_proxy_expr());
            }

            exprs.insert(exprs.end(), result[1].begin(), result[1].end());

            // For the purposes of computation bounds inference, we
            // don't care what sites are loaded, just what sites need
            // to have the correct value in them. So remap all selects
            // to if_then_elses to get tighter bounds.
            class SelectToIfThenElse : public IRMutator {
                using IRMutator::visit;
                Expr visit(const Select *op) override {
                    if (is_pure(op->condition)) {
                        return Call::make(op->type, Call::if_then_else,
                                          {mutate(op->condition),
                                           mutate(op->true_value),
                                           mutate(op->false_value)},
                                          Call::PureIntrinsic);
                    } else {
                        return IRMutator::visit(op);
                    }
                }
            } select_to_if_then_else;

            for (auto &e : exprs) {
                e.value = select_to_if_then_else.mutate(e.value);
            }
        }

        // Check if the dimension at index 'dim_idx' is always pure (i.e. equal to 'dim')
        // in the definition (including in its specializations)
        bool is_dim_always_pure(const Definition &def, const string &dim, int dim_idx) {
            const Variable *var = def.args()[dim_idx].as<Variable>();
            if ((!var) || (var->name != dim)) {
                return false;
            }

            for (const Specialization &s : def.specializations()) {
                bool pure = is_dim_always_pure(s.definition, dim, dim_idx);
                if (!pure) {
                    return false;
                }
            }
            return true;
        }

        // Wrap a statement in let stmts defining the box
        Stmt define_bounds(Stmt s,
                           const Function &producing_func,
                           const string &producing_stage_index,
                           int producing_stage_index_index,
                           const string &loop_level,
                           const vector<vector<Function>> &fused_groups,
                           const vector<set<FusedPair>> &fused_pairs_in_groups,
                           const set<string> &in_pipeline,
                           const set<string> &inner_productions,
                           const set<string> &has_extern_consumer,
                           const Target &target) {

            // Merge all the relevant boxes.
            Box b;

            const vector<string> func_args = func.args();

            size_t last_dot = loop_level.rfind('.');
            string var = loop_level.substr(last_dot + 1);

            for (const pair<const pair<string, int>, Box> &i : bounds) {
                string func_name = i.first.first;
                int func_stage_index = i.first.second;
                string stage_name = func_name + ".s" + std::to_string(func_stage_index);
                if (stage_name == producing_stage_index ||
                    inner_productions.count(func_name) ||
                    is_fused_with_others(fused_groups, fused_pairs_in_groups,
                                         producing_func, producing_stage_index_index,
                                         func_name, func_stage_index, var)) {
                    merge_boxes(b, i.second);
                }
            }

            internal_assert(b.empty() || b.size() == func_args.size());

            if (!b.empty()) {
                // Optimization: If a dimension is pure in every update
                // step of a func, then there exists a single bound for
                // that dimension, instead of one bound per stage. Let's
                // figure out what those dimensions are, and just have all
                // stages but the last use the bounds for the last stage.
                vector<bool> always_pure_dims(func_args.size(), true);
                for (const Definition &def : func.updates()) {
                    for (size_t j = 0; j < always_pure_dims.size(); j++) {
                        bool pure = is_dim_always_pure(def, func_args[j], j);
                        if (!pure) {
                            always_pure_dims[j] = false;
                        }
                    }
                }

                if (stage < func.updates().size()) {
                    size_t stages = func.updates().size();
                    string last_stage = func.name() + ".s" + std::to_string(stages) + ".";
                    for (size_t i = 0; i < always_pure_dims.size(); i++) {
                        if (always_pure_dims[i]) {
                            const string &dim = func_args[i];
                            Expr min = Variable::make(Int(32), last_stage + dim + ".min");
                            Expr max = Variable::make(Int(32), last_stage + dim + ".max");
                            b[i] = Interval(min, max);
                        }
                    }
                }
            }

            if (func.has_extern_definition() &&
                !func.extern_definition_proxy_expr().defined()) {
                // After we define our bounds required, we need to
                // figure out what we're actually going to compute,
                // and what inputs we need. To do this we:

                // 1) Grab a handle on the bounds query results from one level up

                // 2) Run the bounds query to let it round up the output size.

                // 3) Shift the requested output box back inside of the
                // bounds query result from one loop level up (in case
                // it was rounded up)

                // 4) then run the bounds query again to get the input
                // sizes.

                // Because we're wrapping a stmt, this happens in reverse order.

                // 4)
                s = do_bounds_query(s, in_pipeline, target);

                if (!in_pipeline.empty()) {
                    // 3)
                    string outer_query_name = func.name() + ".outer_bounds_query";
                    Expr outer_query = Variable::make(type_of<struct halide_buffer_t *>(), outer_query_name);
                    string inner_query_name = func.name() + ".o0.bounds_query";
                    Expr inner_query = Variable::make(type_of<struct halide_buffer_t *>(), inner_query_name);
                    for (int i = 0; i < func.dimensions(); i++) {
                        Expr outer_min = Call::make(Int(32), Call::buffer_get_min,
                                                    {outer_query, i}, Call::Extern);
                        Expr outer_max = Call::make(Int(32), Call::buffer_get_max,
                                                    {outer_query, i}, Call::Extern);

                        Expr inner_min = Call::make(Int(32), Call::buffer_get_min,
                                                    {inner_query, i}, Call::Extern);
                        Expr inner_max = Call::make(Int(32), Call::buffer_get_max,
                                                    {inner_query, i}, Call::Extern);

                        // Push 'inner' inside of 'outer'
                        Expr shift = Min::make(0, outer_max - inner_max);
                        Expr new_min = inner_min + shift;
                        Expr new_max = inner_max + shift;

                        // Modify the region to be computed accordingly
                        s = LetStmt::make(func.name() + ".s0." + func_args[i] + ".max", new_max, s);
                        s = LetStmt::make(func.name() + ".s0." + func_args[i] + ".min", new_min, s);
                    }

                    // 2)
                    s = do_bounds_query(s, in_pipeline, target);

                    // 1)
                    s = LetStmt::make(func.name() + ".outer_bounds_query",
                                      Variable::make(type_of<struct halide_buffer_t *>(), func.name() + ".o0.bounds_query"), s);
                } else {
                    // If we're at the outermost loop, there is no
                    // bounds query result from one level up, but we
                    // still need to modify the region to be computed
                    // based on the bounds query result and then do
                    // another bounds query to ask for the required
                    // input size given that.

                    // 2)
                    string inner_query_name = func.name() + ".o0.bounds_query";
                    Expr inner_query = Variable::make(type_of<struct halide_buffer_t *>(), inner_query_name);
                    for (int i = 0; i < func.dimensions(); i++) {
                        Expr new_min = Call::make(Int(32), Call::buffer_get_min,
                                                  {inner_query, i}, Call::Extern);
                        Expr new_max = Call::make(Int(32), Call::buffer_get_max,
                                                  {inner_query, i}, Call::Extern);

                        s = LetStmt::make(func.name() + ".s0." + func_args[i] + ".max", new_max, s);
                        s = LetStmt::make(func.name() + ".s0." + func_args[i] + ".min", new_min, s);
                    }

                    s = do_bounds_query(s, in_pipeline, target);
                }
            }

            if (in_pipeline.count(name) == 0) {
                // Inject any explicit bounds
                string prefix = name + ".s" + std::to_string(stage) + ".";

                LoopLevel compute_at = func.schedule().compute_level();
                LoopLevel store_at = func.schedule().store_level();

                for (auto bound : func.schedule().bounds()) {
                    string min_var = prefix + bound.var + ".min";
                    string max_var = prefix + bound.var + ".max";
                    Expr min_required = Variable::make(Int(32), min_var);
                    Expr max_required = Variable::make(Int(32), max_var);

                    if (bound.extent.defined()) {
                        // If the Func is compute_at some inner loop, and
                        // only extent is bounded, then the min could
                        // actually move around, which makes the extent
                        // bound not actually useful for determining the
                        // max required from the point of view of
                        // producers.
                        if (bound.min.defined() ||
                            compute_at.is_root() ||
                            (compute_at.match(loop_level) &&
                             store_at.match(loop_level))) {
                            if (!bound.min.defined()) {
                                bound.min = min_required;
                            }
                            s = LetStmt::make(min_var, bound.min, s);
                            s = LetStmt::make(max_var, bound.min + bound.extent - 1, s);
                        }

                        // Save the unbounded values to use in bounds-checking assertions
                        s = LetStmt::make(min_var + "_unbounded", min_required, s);
                        s = LetStmt::make(max_var + "_unbounded", max_required, s);
                    }

                    if (bound.modulus.defined()) {
                        if (bound.remainder.defined()) {
                            min_required -= bound.remainder;
                            min_required = (min_required / bound.modulus) * bound.modulus;
                            min_required += bound.remainder;
                            Expr max_plus_one = max_required + 1;
                            max_plus_one -= bound.remainder;
                            max_plus_one = ((max_plus_one + bound.modulus - 1) / bound.modulus) * bound.modulus;
                            max_plus_one += bound.remainder;
                            max_required = max_plus_one - 1;
                        } else {
                            Expr extent = (max_required - min_required) + 1;
                            extent = simplify(((extent + bound.modulus - 1) / bound.modulus) * bound.modulus);
                            max_required = simplify(min_required + extent - 1);
                        }
                        s = LetStmt::make(min_var, min_required, s);
                        s = LetStmt::make(max_var, max_required, s);
                    }
                }
            }

            for (size_t d = 0; d < b.size(); d++) {
                string arg = name + ".s" + std::to_string(stage) + "." + func_args[d];

                const bool clamp_to_outer_bounds =
                    !in_pipeline.empty() && has_extern_consumer.count(name);
                if (clamp_to_outer_bounds) {
                    // Allocation bounds inference is going to have a
                    // bad time lifting the results of the bounds
                    // queries outwards. Help it out by insisting that
                    // the bounds are clamped to lie within the bounds
                    // one loop level up.
                    Expr outer_min = Variable::make(Int(32), arg + ".outer_min");
                    Expr outer_max = Variable::make(Int(32), arg + ".outer_max");
                    b[d].min = clamp(b[d].min, outer_min, outer_max);
                    b[d].max = clamp(b[d].max, outer_min, outer_max);
                }

                if (b[d].is_single_point()) {
                    s = LetStmt::make(arg + ".min", Variable::make(Int(32), arg + ".max"), s);
                } else {
                    s = LetStmt::make(arg + ".min", b[d].min, s);
                }
                s = LetStmt::make(arg + ".max", b[d].max, s);

                if (clamp_to_outer_bounds) {
                    s = LetStmt::make(arg + ".outer_min", Variable::make(Int(32), arg + ".min"), s);
                    s = LetStmt::make(arg + ".outer_max", Variable::make(Int(32), arg + ".max"), s);
                }
            }

            if (stage > 0) {
                for (const ReductionVariable &rvar : rvars) {
                    string arg = name + ".s" + std::to_string(stage) + "." + rvar.var;
                    s = LetStmt::make(arg + ".min", rvar.min, s);
                    s = LetStmt::make(arg + ".max", rvar.extent + rvar.min - 1, s);
                }
            }

            return s;
        }

        Stmt do_bounds_query(Stmt s, const set<string> &in_pipeline, const Target &target) {

            const string &extern_name = func.extern_function_name();
            const vector<ExternFuncArgument> &args = func.extern_arguments();

            vector<Expr> bounds_inference_args;

            vector<pair<string, Expr>> lets;

            // Iterate through all of the input args to the extern
            // function building a suitable argument list for the
            // extern function call.  We need a query halide_buffer_t per
            // producer and a query halide_buffer_t for the output

            Expr null_handle = make_zero(Handle());

            vector<pair<Expr, int>> buffers_to_annotate;
            for (const auto &arg : args) {
                if (arg.is_expr()) {
                    bounds_inference_args.push_back(inliner->do_inlining(arg.expr));
                } else if (arg.is_func()) {
                    Function input(arg.func);
                    for (int k = 0; k < input.outputs(); k++) {
                        string name = input.name() + ".o" + std::to_string(k) + ".bounds_query." + func.name();

                        BufferBuilder builder;
                        builder.type = input.output_types()[k];
                        builder.dimensions = input.dimensions();
                        Expr buf = builder.build();

                        lets.emplace_back(name, buf);
                        bounds_inference_args.push_back(Variable::make(type_of<struct halide_buffer_t *>(), name));
                        buffers_to_annotate.emplace_back(bounds_inference_args.back(), input.dimensions());
                    }
                } else if (arg.is_image_param() || arg.is_buffer()) {
                    Parameter p = arg.image_param;
                    Buffer<> b = arg.buffer;
                    string name = arg.is_image_param() ? p.name() : b.name();
                    int dims = arg.is_image_param() ? p.dimensions() : b.dimensions();

                    Expr in_buf = Variable::make(type_of<struct halide_buffer_t *>(), name + ".buffer");

                    // Copy the input buffer into a query buffer to mutate.
                    string query_name = name + ".bounds_query." + func.name();

                    Expr alloca_size = Call::make(Int(32), Call::size_of_halide_buffer_t, {}, Call::Intrinsic);
                    Expr query_buf = Call::make(type_of<struct halide_buffer_t *>(), Call::alloca,
                                                {alloca_size}, Call::Intrinsic);
                    Expr query_shape = Call::make(type_of<struct halide_dimension_t *>(), Call::alloca,
                                                  {(int)(sizeof(halide_dimension_t) * dims)}, Call::Intrinsic);
                    query_buf = Call::make(type_of<struct halide_buffer_t *>(), Call::buffer_init_from_buffer,
                                           {query_buf, query_shape, in_buf}, Call::Extern);

                    lets.emplace_back(query_name, query_buf);
                    Expr buf = Variable::make(type_of<struct halide_buffer_t *>(), query_name, b, p, ReductionDomain());
                    bounds_inference_args.push_back(buf);
                    // Although we expect ImageParams to be properly initialized and sanitized by the caller,
                    // we create a copy with copy_memory (not msan-aware), so we need to annotate it as initialized.
                    buffers_to_annotate.emplace_back(bounds_inference_args.back(), dims);
                } else {
                    internal_error << "Bad ExternFuncArgument type";
                }
            }

            // Make the buffer_ts representing the output. They all
            // use the same size, but have differing types.
            for (int j = 0; j < func.outputs(); j++) {
                BufferBuilder builder;
                builder.type = func.output_types()[j];
                builder.dimensions = func.dimensions();
                for (const string &arg : func.args()) {
                    string prefix = func.name() + ".s" + std::to_string(stage) + "." + arg;
                    Expr min = Variable::make(Int(32), prefix + ".min");
                    Expr max = Variable::make(Int(32), prefix + ".max");
                    builder.mins.push_back(min);
                    builder.extents.push_back(max + 1 - min);
                    builder.strides.emplace_back(0);
                }
                Expr output_buffer_t = builder.build();

                string buf_name = func.name() + ".o" + std::to_string(j) + ".bounds_query";
                bounds_inference_args.push_back(Variable::make(type_of<struct halide_buffer_t *>(), buf_name));
                // Since this is a temporary, internal-only buffer used for bounds inference,
                // we need to mark it
                buffers_to_annotate.emplace_back(bounds_inference_args.back(), func.dimensions());
                lets.emplace_back(buf_name, output_buffer_t);
            }

            Stmt annotate;
            if (target.has_feature(Target::MSAN)) {
                // Mark the buffers as initialized before calling out.
                for (const auto &p : buffers_to_annotate) {
                    Expr buffer = p.first;
                    int dimensions = p.second;
                    // Return type is really 'void', but no way to represent that in our IR.
                    // Precedent (from halide_print, etc) is to use Int(32) and ignore the result.
                    Expr sizeof_buffer_t = cast<uint64_t>(
                        Call::make(Int(32), Call::size_of_halide_buffer_t, {}, Call::Intrinsic));
                    Stmt mark_buffer =
                        Evaluate::make(Call::make(Int(32), "halide_msan_annotate_memory_is_initialized",
                                                  {buffer, sizeof_buffer_t}, Call::Extern));
                    Expr shape = Call::make(type_of<halide_dimension_t *>(), Call::buffer_get_shape, {buffer},
                                            Call::Extern);
                    Expr shape_size = Expr((uint64_t)(sizeof(halide_dimension_t) * dimensions));
                    Stmt mark_shape =
                        Evaluate::make(Call::make(Int(32), "halide_msan_annotate_memory_is_initialized",
                                                  {shape, shape_size}, Call::Extern));

                    mark_buffer = Block::make(mark_buffer, mark_shape);
                    if (annotate.defined()) {
                        annotate = Block::make(annotate, mark_buffer);
                    } else {
                        annotate = mark_buffer;
                    }
                }
            }

            // Make the extern call
            Expr e = func.make_call_to_extern_definition(bounds_inference_args, target);

            // Check if it succeeded
            string result_name = unique_name('t');
            Expr result = Variable::make(Int(32), result_name);
            Expr error = Call::make(Int(32), "halide_error_bounds_inference_call_failed",
                                    {extern_name, result}, Call::Extern);
            Stmt check = AssertStmt::make(EQ::make(result, 0), error);

            check = LetStmt::make(result_name, e, check);

            if (annotate.defined()) {
                check = Block::make(annotate, check);
            }

            // Now inner code is free to extract the fields from the halide_buffer_t
            s = Block::make(check, s);

            // Wrap in let stmts defining the args
            for (const auto &let : lets) {
                s = LetStmt::make(let.first, let.second, s);
            }

            return s;
        }

        // A scope giving the bounds for variables used by this stage.
        // We need to take into account specializations which may refer to
        // different reduction variables as well.
        void populate_scope(Scope<Interval> &result) {
            for (const string &farg : func.args()) {
                string arg = name + ".s" + std::to_string(stage) + "." + farg;
                result.push(farg,
                            Interval(Variable::make(Int(32), arg + ".min"),
                                     Variable::make(Int(32), arg + ".max")));
            }
            if (stage > 0) {
                for (const ReductionVariable &rv : rvars) {
                    string arg = name + ".s" + std::to_string(stage) + "." + rv.var;
                    result.push(rv.var, Interval(Variable::make(Int(32), arg + ".min"),
                                                 Variable::make(Int(32), arg + ".max")));
                }
            }

            /*for (size_t i = 0; i < func.definition().schedule().bounds().size(); i++) {
                const Bound &b = func.definition().schedule().bounds()[i];
                result.push(b.var, Interval(b.min, (b.min + b.extent) - 1));
            }*/
        }
    };
    vector<Stage> stages;

    BoundsInference(const vector<Function> &f,
                    const vector<vector<Function>> &fg,
                    const vector<set<FusedPair>> &fp,
                    const vector<Function> &outputs,
                    const FuncValueBounds &fb,
                    const Target &target)
        : funcs(f), fused_groups(fg), fused_pairs_in_groups(fp), func_bounds(fb), target(target) {
        internal_assert(!f.empty());

        // Compute the intrinsic relationships between the stages of
        // the functions.

        // Figure out which functions will be inlined away
        vector<bool> inlined(f.size());
        for (size_t i = 0; i < inlined.size(); i++) {
            if (i < f.size() - 1 &&
                f[i].schedule().compute_level().is_inlined() &&
                f[i].can_be_inlined()) {
                inlined[i] = true;
                inliner.to_inline.insert(f[i]);
            } else {
                inlined[i] = false;
            }
        }

        // First lay out all the stages in their realization order.
        // The functions are already in topologically sorted order, so
        // this is straight-forward.
        for (size_t i = 0; i < f.size(); i++) {

            if (inlined[i]) {
                continue;
            }

            Stage s;
            s.func = f[i];
            s.stage = 0;
            s.name = s.func.name();
            s.fused_group_index = find_fused_group_index(s.func, fused_groups);
            s.compute_exprs();
            s.stage_prefix = s.name + ".s0.";
            s.inliner = &inliner;
            stages.push_back(s);

            for (size_t j = 0; j < f[i].updates().size(); j++) {
                s.stage = (int)(j + 1);
                s.stage_prefix = s.name + ".s" + std::to_string(s.stage) + ".";
                s.compute_exprs();
                stages.push_back(s);
            }
        }

        // Do any pure inlining
        for (auto &s : stages) {
            for (auto &cond_val : s.exprs) {
                internal_assert(cond_val.value.defined());
                cond_val.value = inliner.do_inlining(cond_val.value);
            }
        }

        // Remove the inlined stages
        vector<Stage> new_stages;
        for (const auto &stage : stages) {
            if (!stage.func.schedule().compute_level().is_inlined() ||
                !stage.func.can_be_inlined()) {
                new_stages.push_back(stage);
            }
        }
        new_stages.swap(stages);

        // Dump the stages post-inlining for debugging
        /*
        debug(0) << "Bounds inference stages after inlining: \n";
        for (size_t i = 0; i < stages.size(); i++) {
            debug(0) << " " << i << ") " << stages[i].name << "\n";
        }
        */

        // Then compute relationships between them.
        for (size_t i = 0; i < stages.size(); i++) {

            Stage &consumer = stages[i];

            // Set up symbols representing the bounds over which this
            // stage will be computed.
            Scope<Interval> scope;
            consumer.populate_scope(scope);

            // Compute all the boxes of the producers this consumer
            // uses.
            map<string, Box> boxes;
            if (consumer.func.has_extern_definition() &&
                !consumer.func.extern_definition_proxy_expr().defined()) {

                const vector<ExternFuncArgument> &args = consumer.func.extern_arguments();
                // Stage::define_bounds is going to compute a query
                // halide_buffer_t per producer for bounds inference to
                // use. We just need to extract those values.
                for (const auto &arg : args) {
                    if (arg.is_func()) {
                        Function f(arg.func);
                        has_extern_consumer.insert(f.name());
                        string stage_name = f.name() + ".s" + std::to_string(f.updates().size());
                        Box b(f.dimensions());
                        for (int d = 0; d < f.dimensions(); d++) {
                            string buf_name = f.name() + ".o0.bounds_query." + consumer.name;
                            Expr buf = Variable::make(type_of<struct halide_buffer_t *>(), buf_name);
                            Expr min = Call::make(Int(32), Call::buffer_get_min,
                                                  {buf, d}, Call::Extern);
                            Expr max = Call::make(Int(32), Call::buffer_get_max,
                                                  {buf, d}, Call::Extern);
                            b[d] = Interval(min, max);
                        }
                        merge_boxes(boxes[f.name()], b);
                    }
                }
            } else {
                for (const auto &cval : consumer.exprs) {
                    map<string, Box> new_boxes;
                    new_boxes = boxes_required(cval.value, scope, func_bounds);
                    for (auto &i : new_boxes) {
                        // Add the condition on which this value is evaluated to the box before merging
                        Box &box = i.second;
                        box.used = cval.cond;
                        merge_boxes(boxes[i.first], box);
                    }
                }
            }

            // Expand the bounds required of all the producers found
            // (and we are checking until i, because stages are topologically sorted).
            for (size_t j = 0; j < i; j++) {
                Stage &producer = stages[j];
                // A consumer depends on *all* stages of a producer, not just the last one.
                const Box &b = boxes[producer.func.name()];

                if (!b.empty()) {
                    // Check for unboundedness
                    for (size_t k = 0; k < b.size(); k++) {
                        if (!b[k].is_bounded()) {
                            std::ostringstream err;
                            if (consumer.stage == 0) {
                                err << "The pure definition ";
                            } else {
                                err << "Update definition number " << (consumer.stage - 1);
                            }
                            err << " of Function " << consumer.name
                                << " calls function " << producer.name
                                << " in an unbounded way in dimension " << k << "\n";
                            user_error << err.str();
                        }
                    }

                    // Dump out the region required of each stage for debugging.
                    /*
                    debug(0) << "Box required of " << producer.name
                             << " by " << consumer.name
                             << " stage " << consumer.stage << ":\n"
                             << " used: " << b.used << "\n";
                    for (size_t k = 0; k < b.size(); k++) {
                        debug(0) << "  " << b[k].min << " ... " << b[k].max << "\n";
                    }
                    debug(0) << "\n";
                    */

                    producer.bounds[{consumer.name, consumer.stage}] = b;
                    producer.consumers.push_back((int)i);
                }
            }
        }

        // The region required of the each output is expanded to include the size of the output buffer.
        for (const Function &output : outputs) {
            Box output_box;
            string buffer_name = output.name();
            if (output.outputs() > 1) {
                // Use the output size of the first output buffer
                buffer_name += ".0";
            }
            for (int d = 0; d < output.dimensions(); d++) {
                Parameter buf = output.output_buffers()[0];
                Expr min = Variable::make(Int(32), buffer_name + ".min." + std::to_string(d), buf);
                Expr extent = Variable::make(Int(32), buffer_name + ".extent." + std::to_string(d), buf);

                // Respect any output min and extent constraints
                Expr min_constraint = buf.min_constraint(d);
                Expr extent_constraint = buf.extent_constraint(d);

                if (min_constraint.defined()) {
                    min = min_constraint;
                }
                if (extent_constraint.defined()) {
                    extent = extent_constraint;
                }

                output_box.push_back(Interval(min, (min + extent) - 1));
            }
            for (auto &s : stages) {
                if (!s.func.same_as(output)) {
                    continue;
                }
                s.bounds[{s.name, s.stage}] = output_box;
            }
        }
    }

    using IRMutator::visit;

    Stmt visit(const For *op) override {
        // Don't recurse inside loops marked 'Extern', they will be
        // removed later.
        if (op->for_type == ForType::Extern) {
            return op;
        }

        set<string> old_inner_productions;
        inner_productions.swap(old_inner_productions);

        Stmt body = op->body;

        // Walk inside of any let/if statements that don't depend on
        // bounds inference results so that we don't needlessly
        // complicate our bounds expressions.
        vector<pair<string, Expr>> wrappers;
        vector<ScopedBinding<>> bindings;
        while (true) {
            if (const LetStmt *let = body.as<LetStmt>()) {
                if (depends_on_bounds_inference(let->value)) {
                    break;
                }

                body = let->body;
                wrappers.emplace_back(let->name, let->value);
                bindings.emplace_back(let_vars_in_scope, let->name);
            } else if (const IfThenElse *if_then_else = body.as<IfThenElse>()) {
                if (depends_on_bounds_inference(if_then_else->condition) ||
                    if_then_else->else_case.defined()) {
                    break;
                }

                body = if_then_else->then_case;
                wrappers.emplace_back(std::string(), if_then_else->condition);
            } else {
                break;
            }
        }

        // If there are no pipelines at this loop level, we can skip
        // most of the work.  Consider 'extern' for loops as pipelines
        // (we aren't recursing into these loops above).
        bool no_pipelines =
            body.as<For>() != nullptr &&
            body.as<For>()->for_type != ForType::Extern;

        // Figure out which stage of which function we're producing
        int producing = -1;
        Function f;
        int stage_index = -1;
        string stage_name;
        for (size_t i = 0; i < stages.size(); i++) {
            if (starts_with(op->name, stages[i].stage_prefix)) {
                producing = i;
                f = stages[i].func;
                stage_index = (int)stages[i].stage;
                stage_name = stages[i].name + ".s" + std::to_string(stages[i].stage);
                break;
            }
        }

        // Figure out how much of it we're producing

        // Note: the case when functions are fused is a little bit tricky, so may need extra care:
        // when we're producing some of a Func A, at every loop belonging to A
        // you potentially need to define symbols for what box is being computed
        // of A (A.x.min, A.x.max ...), because that any other producer Func P nested
        // there is going to define its loop bounds in terms of these symbols, to ensure
        // it computes enough of itself to satisfy the consumer.
        // Now say we compute B with A, and say B consumes P, not A. Bounds inference
        // will see the shared loop, and think it belongs to A only. It will define A.x.min and
        // friends, but that's not very useful, because P's loops are in terms of B.x.min, B.x.max, etc.
        // So without a local definition of those symbols, P will use the one in the outer scope, and
        // compute way too much of itself. It'll still be correct, but it's massive over-compute.
        // The fix is to realize that in this loop belonging to A, we also potentially need to define
        // a box for B, because B belongs to the same fused group as A, so really this loop belongs to A and B.
        // We'll get the box using boxes_provided and only filtering for A and B after the fact
        // Note that even though the loops are fused, the boxes touched of A and B might be totally different,
        // because e.g. B could be double-resolution (as happens when fusing yuv computations), so this
        // is not just a matter of giving A's box B's name as an alias.
        set<pair<string, int>> fused_group;
        map<string, Box> boxes_for_fused_group;
        map<string, Function> stage_name_to_func;

        if (producing >= 0) {
            fused_group.insert(make_pair(f.name(), stage_index));
        }

        if (!no_pipelines && producing >= 0 && !f.has_extern_definition()) {
            Scope<Interval> empty_scope;
            size_t last_dot = op->name.rfind('.');
            string var = op->name.substr(last_dot + 1);

            for (const auto &pair : fused_pairs_in_groups[stages[producing].fused_group_index]) {
                if (!((pair.func_1 == stages[producing].name) && ((int)pair.stage_1 == stage_index)) && is_fused_with_others(fused_groups, fused_pairs_in_groups,
                                                                                                                             f, stage_index,
                                                                                                                             pair.func_1, pair.stage_1, var)) {
                    fused_group.insert(make_pair(pair.func_1, pair.stage_1));
                }
                if (!((pair.func_2 == stages[producing].name) && ((int)pair.stage_2 == stage_index)) && is_fused_with_others(fused_groups, fused_pairs_in_groups,
                                                                                                                             f, stage_index,
                                                                                                                             pair.func_2, pair.stage_2, var)) {
                    fused_group.insert(make_pair(pair.func_2, pair.stage_2));
                }
            }

            if (fused_group.size() == 1) {
                boxes_for_fused_group[stage_name] = box_provided(body, stages[producing].name, empty_scope, func_bounds);
                stage_name_to_func[stage_name] = f;
                internal_assert((int)boxes_for_fused_group[stage_name].size() == f.dimensions());
            } else {
                auto boxes = boxes_provided(body, empty_scope, func_bounds);
                for (const auto &fused : fused_group) {
                    string fused_stage_name = fused.first + ".s" + std::to_string(fused.second);
                    auto it = boxes.find(fused.first);
                    if (it != boxes.end()) {
                        boxes_for_fused_group[fused_stage_name] = it->second;
                    }
                    for (const auto &fn : funcs) {
                        if (fn.name() == fused.first) {
                            stage_name_to_func[fused_stage_name] = fn;
                            break;
                        }
                    }
                }
            }
        }

        // Recurse.
        body = mutate(body);

        if (!no_pipelines) {
            // We only care about the bounds of a func if:
            // A) We're not already in a pipeline over that func AND
            // B.1) There's a production of this func somewhere inside this loop OR
            // B.2) We're downstream (a consumer) of a func for which we care about the bounds.
            vector<bool> bounds_needed(stages.size(), false);
            for (size_t i = 0; i < stages.size(); i++) {
                if (inner_productions.count(stages[i].name)) {
                    bounds_needed[i] = true;
                }

                if (in_pipeline.count(stages[i].name)) {
                    bounds_needed[i] = false;
                }

                if (bounds_needed[i]) {
                    for (int consumer : stages[i].consumers) {
                        bounds_needed[consumer] = true;
                    }
                    body = stages[i].define_bounds(
                        body, f, stage_name, stage_index, op->name, fused_groups,
                        fused_pairs_in_groups, in_pipeline, inner_productions,
                        has_extern_consumer, target);
                }
            }

            // Finally, define the production bounds for the thing
            // we're producing.
            if (producing >= 0 && !inner_productions.empty()) {
                for (const auto &b : boxes_for_fused_group) {
                    const vector<string> &f_args = stage_name_to_func[b.first].args();
                    const auto &box = b.second;
                    internal_assert(f_args.size() == box.size());
                    for (size_t i = 0; i < box.size(); i++) {
                        internal_assert(box[i].is_bounded());
                        string var = b.first + "." + f_args[i];

                        if (box[i].is_single_point()) {
                            body = LetStmt::make(var + ".max", Variable::make(Int(32), var + ".min"), body);
                        } else {
                            body = LetStmt::make(var + ".max", box[i].max, body);
                        }

                        body = LetStmt::make(var + ".min", box[i].min, body);
                    }
                }
            }

            // And the current bounds on its reduction variables, and
            // variables from extern for loops.
            if (producing >= 0) {
                // Iterate over all fused stages to make sure that bounds for their reduction variables
                // are included as well (see a detailed explanation of bounds for fused functions in the
                // long comment above).
                for (const auto &fused : fused_group) {
                    size_t si = 0;
                    // Find a Stage structure corresponding to a current fused stage.
                    for (si = 0; si < stages.size(); si++) {
                        if ((fused.first == stages[si].name) && fused.second == (int)stages[si].stage) {
                            break;
                        }
                    }
                    internal_assert(si < stages.size());
                    const Stage &s = stages[si];

                    vector<string> vars;
                    if (s.func.has_extern_definition()) {
                        vars = s.func.args();
                    }
                    if (s.stage > 0) {
                        for (const ReductionVariable &rv : s.rvars) {
                            vars.push_back(rv.var);
                        }
                    }
                    for (const string &i : vars) {
                        string var = s.stage_prefix + i;
                        Interval in = bounds_of_inner_var(var, body);
                        if (in.is_bounded()) {
                            // bounds_of_inner_var doesn't understand
                            // GuardWithIf, but we know split rvars never
                            // have inner bounds that exceed the outer
                            // ones.
                            if (!s.rvars.empty()) {
                                in.min = max(in.min, Variable::make(Int(32), var + ".min"));
                                in.max = min(in.max, Variable::make(Int(32), var + ".max"));
                            }
                            body = LetStmt::make(var + ".min", in.min, body);
                            body = LetStmt::make(var + ".max", in.max, body);
                        } else {
                            // If it's not found, we're already in the
                            // scope of the injected let. The let was
                            // probably lifted to an outer level.
                            Expr val;
                            if (let_vars_in_scope.contains(var + ".guarded")) {
                                // Use a guarded version if it exists, for tighter bounds inference.
                                val = Variable::make(Int(32), var + ".guarded");
                            } else {
                                val = Variable::make(Int(32), var);
                            }
                            body = LetStmt::make(var + ".min", val, body);
                            body = LetStmt::make(var + ".max", val, body);
                        }
                    }
                }
            }
        }

        inner_productions.insert(old_inner_productions.begin(),
                                 old_inner_productions.end());

        // Rewrap the let/if statements
        for (size_t i = wrappers.size(); i > 0; i--) {
            const auto &p = wrappers[i - 1];
            if (p.first.empty()) {
                body = IfThenElse::make(p.second, body);
            } else {
                body = LetStmt::make(p.first, p.second, body);
            }
        }

        return For::make(op->name, op->min, op->extent, op->for_type, op->partition_policy, op->device_api, body);
    }

    Scope<> let_vars_in_scope;
    Stmt visit(const LetStmt *op) override {
        ScopedBinding<> bind(let_vars_in_scope, op->name);
        return IRMutator::visit(op);
    }

    Stmt visit(const ProducerConsumer *p) override {
        in_pipeline.insert(p->name);
        Stmt stmt = IRMutator::visit(p);
        in_pipeline.erase(p->name);
        inner_productions.insert(p->name);
        return stmt;
    }
};

}  // namespace

Stmt bounds_inference(Stmt s,
                      const vector<Function> &outputs,
                      const vector<string> &order,
                      const vector<vector<string>> &fused_groups,
                      const map<string, Function> &env,
                      const FuncValueBounds &func_bounds,
                      const Target &target) {

    vector<Function> funcs(order.size());
    for (size_t i = 0; i < order.size(); i++) {
        funcs[i] = env.find(order[i])->second;
    }

    // Each element in 'fused_func_groups' indicates a group of functions
    // which loops should be fused together.
    vector<vector<Function>> fused_func_groups;
    for (const vector<string> &group : fused_groups) {
        vector<Function> fs;
        for (const string &fname : group) {
            fs.push_back(env.find(fname)->second);
        }
        fused_func_groups.push_back(fs);
    }

    // For each fused group, collect the pairwise fused function stages.
    vector<set<FusedPair>> fused_pairs_in_groups;
    for (const vector<string> &group : fused_groups) {
        set<FusedPair> pairs;
        for (const string &fname : group) {
            Function f = env.find(fname)->second;
            if (!f.has_extern_definition()) {
                std::copy(f.definition().schedule().fused_pairs().begin(),
                          f.definition().schedule().fused_pairs().end(),
                          std::inserter(pairs, pairs.end()));

                for (const auto &update : f.updates()) {
                    std::copy(update.schedule().fused_pairs().begin(),
                              update.schedule().fused_pairs().end(),
                              std::inserter(pairs, pairs.end()));
                }
            }
        }
        fused_pairs_in_groups.push_back(pairs);
    }

    // Add a note in the IR for where assertions on input images
    // should go. Those are handled by a later lowering pass.
    Expr marker = Call::make(Int(32), Call::add_image_checks_marker, {}, Call::Intrinsic);
    s = Block::make(Evaluate::make(marker), s);

    // Add a synthetic outermost loop to act as 'root'.
    s = For::make("<outermost>", 0, 1, ForType::Serial, Partition::Never, DeviceAPI::None, s);

    s = BoundsInference(funcs, fused_func_groups, fused_pairs_in_groups,
                        outputs, func_bounds, target)
            .mutate(s);
    return s.as<For>()->body;
}

}  // namespace Internal
}  // namespace Halide
