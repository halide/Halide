#include "BoundsInference.h"
#include "Bounds.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Inline.h"
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

bool var_name_match(string candidate, string var) {
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
    bool result;
    DependsOnBoundsInference()
        : result(false) {
    }
};

bool depends_on_bounds_inference(Expr e) {
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
    Scope<Interval> scope;

    using IRVisitor::visit;

    void visit(const LetStmt *op) override {
        Interval in = bounds_of_expr_in_scope(op->value, scope);
        if (op->name == var) {
            result = in;
        } else {
            ScopedBinding<Interval> p(scope, op->name, in);
            op->body.accept(this);
        }
    }

    void visit(const For *op) override {
        // At this stage of lowering, loop_min and loop_max
        // conveniently exist in scope.
        Interval in(Variable::make(Int(32), op->name + ".loop_min"),
                    Variable::make(Int(32), op->name + ".loop_max"));

        if (op->name == var) {
            result = in;
        } else {
            ScopedBinding<Interval> p(scope, op->name, in);
            op->body.accept(this);
        }
    }
};

Interval bounds_of_inner_var(string var, Stmt s) {
    BoundsOfInnerVar b(var);
    s.accept(&b);
    return b.result;
}

}  // namespace

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
    set<string> in_pipeline, inner_productions;
    const Target target;

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
                                                   {likely(predicates[0]), val, make_zero(val.type())},
                                                   Internal::Call::PureIntrinsic);
                        for (size_t i = 1; i < predicates.size(); ++i) {
                            cond_val = Call::make(cond_val.type(),
                                                  Internal::Call::if_then_else,
                                                  {likely(predicates[i]), cond_val, make_zero(cond_val.type())},
                                                  Internal::Call::PureIntrinsic);
                        }
                        result[i].push_back(CondValue(const_true(), cond_val));
                    } else {
                        result[i].push_back(CondValue(const_true(), val));
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
                        vec.push_back(CondValue(const_true(), val));
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
                exprs.push_back(CondValue(const_true(), func.extern_definition_proxy_expr()));
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

        // Determine if the current producing stage is fused with other
        // stage (i.e. the consumer stage) at dimension 'var'.
        bool is_fused_with_others(const vector<vector<Function>> &fused_groups,
                                  const vector<set<FusedPair>> &fused_pairs_in_groups,
                                  const Function &producing_func, int producing_stage_index,
                                  string consumer_name, int consumer_stage,
                                  string var) {
            if (producing_func.has_extern_definition()) {
                return false;
            }

            // Find the fused group this producing stage belongs to.
            size_t index;
            {
                const auto &iter = std::find_if(fused_groups.begin(), fused_groups.end(),
                                                [&producing_func](const vector<Function> &group) {
                                                    return std::any_of(group.begin(), group.end(),
                                                                       [&producing_func](const Function &f) {
                                                                           return (f.name() == producing_func.name());
                                                                       });
                                                });
                internal_assert(iter != fused_groups.end());
                index = iter - fused_groups.begin();
            }

            const vector<Dim> &dims = (producing_stage_index == 0) ? producing_func.definition().schedule().dims() : producing_func.update(producing_stage_index - 1).schedule().dims();

            size_t var_index;
            {
                const auto &iter = std::find_if(dims.begin(), dims.end(),
                                                [&var](const Dim &d) { return var_name_match(d.var, var); });
                internal_assert(iter != dims.end());
                var_index = iter - dims.begin();
            }

            // Iterate over the fused pair list to check if the producer stage
            // is fused with the consumer stage at 'var'
            for (const auto &pair : fused_pairs_in_groups[index]) {
                if (((pair.func_1 == consumer_name) && ((int)pair.stage_1 == consumer_stage)) ||
                    ((pair.func_2 == consumer_name) && ((int)pair.stage_2 == consumer_stage))) {
                    const auto &iter = std::find_if(dims.begin(), dims.end(),
                                                    [&pair](const Dim &d) { return var_name_match(d.var, pair.var_name); });
                    internal_assert(iter != dims.end());
                    size_t idx = iter - dims.begin();
                    if (var_index >= idx) {
                        return true;
                    }
                }
            }
            return false;
        }

        // Wrap a statement in let stmts defining the box
        Stmt define_bounds(Stmt s,
                           Function producing_func,
                           string producing_stage_index,
                           int producing_stage_index_index,
                           string loop_level,
                           const vector<vector<Function>> &fused_groups,
                           const vector<set<FusedPair>> &fused_pairs_in_groups,
                           const set<string> &in_pipeline,
                           const set<string> inner_productions,
                           const Target &target) {

            // Merge all the relevant boxes.
            Box b;

            const vector<string> func_args = func.args();

            size_t last_dot = loop_level.rfind('.');
            string var = loop_level.substr(last_dot + 1);

            for (const pair<pair<string, int>, Box> &i : bounds) {
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

                for (size_t i = 0; i < func.schedule().bounds().size(); i++) {
                    Bound bound = func.schedule().bounds()[i];
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
                        min_required -= bound.remainder;
                        min_required = (min_required / bound.modulus) * bound.modulus;
                        min_required += bound.remainder;
                        Expr max_plus_one = max_required + 1;
                        max_plus_one -= bound.remainder;
                        max_plus_one = ((max_plus_one + bound.modulus - 1) / bound.modulus) * bound.modulus;
                        max_plus_one += bound.remainder;
                        max_required = max_plus_one - 1;
                        s = LetStmt::make(min_var, min_required, s);
                        s = LetStmt::make(max_var, max_required, s);
                    }
                }
            }

            for (size_t d = 0; d < b.size(); d++) {
                string arg = name + ".s" + std::to_string(stage) + "." + func_args[d];

                if (b[d].is_single_point()) {
                    s = LetStmt::make(arg + ".min", Variable::make(Int(32), arg + ".max"), s);
                } else {
                    s = LetStmt::make(arg + ".min", b[d].min, s);
                }
                s = LetStmt::make(arg + ".max", b[d].max, s);
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
            for (size_t j = 0; j < args.size(); j++) {
                if (args[j].is_expr()) {
                    bounds_inference_args.push_back(args[j].expr);
                } else if (args[j].is_func()) {
                    Function input(args[j].func);
                    for (int k = 0; k < input.outputs(); k++) {
                        string name = input.name() + ".o" + std::to_string(k) + ".bounds_query." + func.name();

                        BufferBuilder builder;
                        builder.type = input.output_types()[k];
                        builder.dimensions = input.dimensions();
                        Expr buf = builder.build();

                        lets.push_back({name, buf});
                        bounds_inference_args.push_back(Variable::make(type_of<struct halide_buffer_t *>(), name));
                        buffers_to_annotate.emplace_back(bounds_inference_args.back(), input.dimensions());
                    }
                } else if (args[j].is_image_param() || args[j].is_buffer()) {
                    Parameter p = args[j].image_param;
                    Buffer<> b = args[j].buffer;
                    string name = args[j].is_image_param() ? p.name() : b.name();
                    int dims = args[j].is_image_param() ? p.dimensions() : b.dimensions();

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

                    lets.push_back({query_name, query_buf});
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
                for (const string arg : func.args()) {
                    string prefix = func.name() + ".s" + std::to_string(stage) + "." + arg;
                    Expr min = Variable::make(Int(32), prefix + ".min");
                    Expr max = Variable::make(Int(32), prefix + ".max");
                    builder.mins.push_back(min);
                    builder.extents.push_back(max + 1 - min);
                    builder.strides.push_back(0);
                }
                Expr output_buffer_t = builder.build();

                string buf_name = func.name() + ".o" + std::to_string(j) + ".bounds_query";
                bounds_inference_args.push_back(Variable::make(type_of<struct halide_buffer_t *>(), buf_name));
                // Since this is a temporary, internal-only buffer used for bounds inference,
                // we need to mark it
                buffers_to_annotate.emplace_back(bounds_inference_args.back(), func.dimensions());
                lets.push_back({buf_name, output_buffer_t});
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
            for (size_t i = 0; i < lets.size(); i++) {
                s = LetStmt::make(lets[i].first, lets[i].second, s);
            }

            return s;
        }

        // A scope giving the bounds for variables used by this stage.
        // We need to take into account specializations which may refer to
        // different reduction variables as well.
        void populate_scope(Scope<Interval> &result) {
            for (const string farg : func.args()) {
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
            } else {
                inlined[i] = false;
            }
        }

        // First lay out all the stages in their realization order.
        // The functions are already in topologically sorted order, so
        // this is straight-forward.
        for (size_t i = 0; i < f.size(); i++) {

            if (inlined[i]) continue;

            Stage s;
            s.func = f[i];
            s.stage = 0;
            s.name = s.func.name();
            s.compute_exprs();
            s.stage_prefix = s.name + ".s0.";
            stages.push_back(s);

            for (size_t j = 0; j < f[i].updates().size(); j++) {
                s.stage = (int)(j + 1);
                s.stage_prefix = s.name + ".s" + std::to_string(s.stage) + ".";
                s.compute_exprs();
                stages.push_back(s);
            }
        }

        // Do any pure inlining (TODO: This is currently slow)
        for (size_t i = f.size(); i > 0; i--) {
            Function func = f[i - 1];
            if (inlined[i - 1]) {
                for (size_t j = 0; j < stages.size(); j++) {
                    Stage &s = stages[j];
                    for (size_t k = 0; k < s.exprs.size(); k++) {
                        CondValue &cond_val = s.exprs[k];
                        internal_assert(cond_val.value.defined());
                        cond_val.value = inline_function(cond_val.value, func);
                    }
                }
            }
        }

        // Remove the inlined stages
        vector<Stage> new_stages;
        for (size_t i = 0; i < stages.size(); i++) {
            if (!stages[i].func.schedule().compute_level().is_inlined() ||
                !stages[i].func.can_be_inlined()) {
                new_stages.push_back(stages[i]);
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
                for (size_t j = 0; j < args.size(); j++) {
                    if (args[j].is_func()) {
                        Function f(args[j].func);
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

            // Expand the bounds required of all the producers found.
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
                             << " stage " << consumer.stage << ":\n";
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
        for (Function output : outputs) {
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
            for (size_t i = 0; i < stages.size(); i++) {
                Stage &s = stages[i];
                if (!s.func.same_as(output)) continue;
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

        // Walk inside of any let statements that don't depend on
        // bounds inference results so that we don't needlessly
        // complicate our bounds expressions.
        vector<pair<string, Expr>> lets;
        while (const LetStmt *let = body.as<LetStmt>()) {
            if (depends_on_bounds_inference(let->value)) {
                break;
            }

            body = let->body;
            lets.push_back({let->name, let->value});
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
        Box box;
        if (!no_pipelines && producing >= 0 && !f.has_extern_definition()) {
            Scope<Interval> empty_scope;
            box = box_provided(body, stages[producing].name, empty_scope, func_bounds);
            internal_assert((int)box.size() == f.dimensions());
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
                    for (size_t j = 0; j < stages[i].consumers.size(); j++) {
                        bounds_needed[stages[i].consumers[j]] = true;
                    }
                    body = stages[i].define_bounds(
                        body, f, stage_name, stage_index, op->name, fused_groups,
                        fused_pairs_in_groups, in_pipeline, inner_productions, target);
                }
            }

            // Finally, define the production bounds for the thing
            // we're producing.
            if (producing >= 0 && !inner_productions.empty()) {
                const vector<string> f_args = f.args();
                for (size_t i = 0; i < box.size(); i++) {
                    internal_assert(box[i].is_bounded());
                    string var = stage_name + "." + f_args[i];

                    if (box[i].is_single_point()) {
                        body = LetStmt::make(var + ".max", Variable::make(Int(32), var + ".min"), body);
                    } else {
                        body = LetStmt::make(var + ".max", box[i].max, body);
                    }

                    body = LetStmt::make(var + ".min", box[i].min, body);
                }
            }

            // And the current bounds on its reduction variables, and
            // variables from extern for loops.
            if (producing >= 0) {
                const Stage &s = stages[producing];
                vector<string> vars;
                if (s.func.has_extern_definition()) {
                    vars = s.func.args();
                }
                if (stages[producing].stage > 0) {
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
                        Expr val = Variable::make(Int(32), var);
                        body = LetStmt::make(var + ".min", val, body);
                        body = LetStmt::make(var + ".max", val, body);
                    }
                }
            }
        }

        inner_productions.insert(old_inner_productions.begin(),
                                 old_inner_productions.end());

        // Rewrap the let statements
        for (size_t i = lets.size(); i > 0; i--) {
            body = LetStmt::make(lets[i - 1].first, lets[i - 1].second, body);
        }

        return For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
    }

    Stmt visit(const ProducerConsumer *p) override {
        in_pipeline.insert(p->name);
        Stmt stmt = IRMutator::visit(p);
        in_pipeline.erase(p->name);
        inner_productions.insert(p->name);
        return stmt;
    }
};

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

                for (size_t i = 0; i < f.updates().size(); ++i) {
                    std::copy(f.updates()[i].schedule().fused_pairs().begin(),
                              f.updates()[i].schedule().fused_pairs().end(),
                              std::inserter(pairs, pairs.end()));
                }
            }
        }
        fused_pairs_in_groups.push_back(pairs);
    }

    // Add an outermost bounds inference marker
    s = For::make("<outermost>", 0, 1, ForType::Serial, DeviceAPI::None, s);
    s = BoundsInference(funcs, fused_func_groups, fused_pairs_in_groups,
                        outputs, func_bounds, target)
            .mutate(s);
    return s.as<For>()->body;
}

}  // namespace Internal
}  // namespace Halide
