#include <algorithm>
#include <utility>

#include "CSE.h"
#include "CodeGen_GPU_Dev.h"
#include "Deinterleave.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Scope.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"
#include "VectorizeLoops.h"

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::string;
using std::vector;

namespace {

// For a given var, replace expressions like shuffle_vector(var, 4)
// with var.lane.4
class ReplaceShuffleVectors : public IRMutator {
    string var;

    using IRMutator::visit;

    Expr visit(const Shuffle *op) override {
        const Variable *v;
        if (op->indices.size() == 1 &&
            (v = op->vectors[0].as<Variable>()) &&
            v->name == var) {
            return Variable::make(op->type, var + ".lane." + std::to_string(op->indices[0]));
        } else {
            return IRMutator::visit(op);
        }
    }

public:
    ReplaceShuffleVectors(const string &v)
        : var(v) {
    }
};

/** Find the exact max and min lanes of a vector expression. Not
 * conservative like bounds_of_expr, but uses similar rules for some
 * common node types where it can be exact. Assumes any vector
 * variables defined externally also have .min_lane and .max_lane
 * versions in scope. */
Interval bounds_of_lanes(const Expr &e) {
    if (const Add *add = e.as<Add>()) {
        if (const Broadcast *b = add->b.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(add->a);
            return {ia.min + b->value, ia.max + b->value};
        } else if (const Broadcast *b = add->a.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(add->b);
            return {b->value + ia.min, b->value + ia.max};
        }
    } else if (const Sub *sub = e.as<Sub>()) {
        if (const Broadcast *b = sub->b.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(sub->a);
            return {ia.min - b->value, ia.max - b->value};
        } else if (const Broadcast *b = sub->a.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(sub->b);
            return {b->value - ia.max, b->value - ia.max};
        }
    } else if (const Mul *mul = e.as<Mul>()) {
        if (const Broadcast *b = mul->b.as<Broadcast>()) {
            if (is_positive_const(b->value)) {
                Interval ia = bounds_of_lanes(mul->a);
                return {ia.min * b->value, ia.max * b->value};
            } else if (is_negative_const(b->value)) {
                Interval ia = bounds_of_lanes(mul->a);
                return {ia.max * b->value, ia.min * b->value};
            }
        } else if (const Broadcast *b = mul->a.as<Broadcast>()) {
            if (is_positive_const(b->value)) {
                Interval ia = bounds_of_lanes(mul->b);
                return {b->value * ia.min, b->value * ia.max};
            } else if (is_negative_const(b->value)) {
                Interval ia = bounds_of_lanes(mul->b);
                return {b->value * ia.max, b->value * ia.min};
            }
        }
    } else if (const Div *div = e.as<Div>()) {
        if (const Broadcast *b = div->b.as<Broadcast>()) {
            if (is_positive_const(b->value)) {
                Interval ia = bounds_of_lanes(div->a);
                return {ia.min / b->value, ia.max / b->value};
            } else if (is_negative_const(b->value)) {
                Interval ia = bounds_of_lanes(div->a);
                return {ia.max / b->value, ia.min / b->value};
            }
        }
    } else if (const And *and_ = e.as<And>()) {
        if (const Broadcast *b = and_->b.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(and_->a);
            return {ia.min && b->value, ia.max && b->value};
        } else if (const Broadcast *b = and_->a.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(and_->b);
            return {ia.min && b->value, ia.max && b->value};
        }
    } else if (const Or *or_ = e.as<Or>()) {
        if (const Broadcast *b = or_->b.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(or_->a);
            return {ia.min && b->value, ia.max && b->value};
        } else if (const Broadcast *b = or_->a.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(or_->b);
            return {ia.min && b->value, ia.max && b->value};
        }
    } else if (const Min *min = e.as<Min>()) {
        if (const Broadcast *b = min->b.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(min->a);
            return {Min::make(ia.min, b->value), Min::make(ia.max, b->value)};
        } else if (const Broadcast *b = min->a.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(min->b);
            return {Min::make(ia.min, b->value), Min::make(ia.max, b->value)};
        }
    } else if (const Max *max = e.as<Max>()) {
        if (const Broadcast *b = max->b.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(max->a);
            return {Max::make(ia.min, b->value), Max::make(ia.max, b->value)};
        } else if (const Broadcast *b = max->a.as<Broadcast>()) {
            Interval ia = bounds_of_lanes(max->b);
            return {Max::make(ia.min, b->value), Max::make(ia.max, b->value)};
        }
    } else if (const Not *not_ = e.as<Not>()) {
        Interval ia = bounds_of_lanes(not_->a);
        return {!ia.max, !ia.min};
    } else if (const Ramp *r = e.as<Ramp>()) {
        Expr last_lane_idx = make_const(r->base.type(), r->lanes - 1);
        if (is_positive_const(r->stride)) {
            return {r->base, r->base + last_lane_idx * r->stride};
        } else if (is_negative_const(r->stride)) {
            return {r->base + last_lane_idx * r->stride, r->base};
        }
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return {b->value, b->value};
    } else if (const Variable *var = e.as<Variable>()) {
        return {Variable::make(var->type.element_of(), var->name + ".min_lane"),
                Variable::make(var->type.element_of(), var->name + ".max_lane")};
    } else if (const Let *let = e.as<Let>()) {
        Interval ia = bounds_of_lanes(let->value);
        Interval ib = bounds_of_lanes(let->body);
        if (expr_uses_var(ib.min, let->name + ".min_lane")) {
            ib.min = Let::make(let->name + ".min_lane", ia.min, ib.min);
        }
        if (expr_uses_var(ib.max, let->name + ".min_lane")) {
            ib.max = Let::make(let->name + ".min_lane", ia.min, ib.max);
        }
        if (expr_uses_var(ib.min, let->name + ".max_lane")) {
            ib.min = Let::make(let->name + ".max_lane", ia.max, ib.min);
        }
        if (expr_uses_var(ib.max, let->name + ".max_lane")) {
            ib.max = Let::make(let->name + ".max_lane", ia.max, ib.max);
        }
        if (expr_uses_var(ib.min, let->name)) {
            ib.min = Let::make(let->name, let->value, ib.min);
        }
        if (expr_uses_var(ib.max, let->name)) {
            ib.max = Let::make(let->name, let->value, ib.max);
        }
        return ib;
    }

    // Take the explicit min and max over the lanes
    if (e.type().is_bool()) {
        Expr min_lane = VectorReduce::make(VectorReduce::And, e, 1);
        Expr max_lane = VectorReduce::make(VectorReduce::Or, e, 1);
        return {min_lane, max_lane};
    } else {
        Expr min_lane = VectorReduce::make(VectorReduce::Min, e, 1);
        Expr max_lane = VectorReduce::make(VectorReduce::Max, e, 1);
        return {min_lane, max_lane};
    }
};

// A ramp with the lanes repeated (e.g. <0 0 2 2 4 4 6 6>)
struct InterleavedRamp {
    Expr base, stride;
    int lanes, repetitions;
};

bool is_interleaved_ramp(const Expr &e, const Scope<Expr> &scope, InterleavedRamp *result) {
    if (const Ramp *r = e.as<Ramp>()) {
        result->base = r->base;
        result->stride = r->stride;
        result->lanes = r->lanes;
        result->repetitions = 1;
        return true;
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        result->base = b->value;
        result->stride = 0;
        result->lanes = b->lanes;
        result->repetitions = 0;
        return true;
    } else if (const Add *add = e.as<Add>()) {
        InterleavedRamp ra;
        if (is_interleaved_ramp(add->a, scope, &ra) &&
            is_interleaved_ramp(add->b, scope, result) &&
            (ra.repetitions == 0 ||
             result->repetitions == 0 ||
             ra.repetitions == result->repetitions)) {
            result->base = simplify(result->base + ra.base);
            result->stride = simplify(result->stride + ra.stride);
            if (!result->repetitions) {
                result->repetitions = ra.repetitions;
            }
            return true;
        }
    } else if (const Sub *sub = e.as<Sub>()) {
        InterleavedRamp ra;
        if (is_interleaved_ramp(sub->a, scope, &ra) &&
            is_interleaved_ramp(sub->b, scope, result) &&
            (ra.repetitions == 0 ||
             result->repetitions == 0 ||
             ra.repetitions == result->repetitions)) {
            result->base = simplify(ra.base - result->base);
            result->stride = simplify(ra.stride - result->stride);
            if (!result->repetitions) {
                result->repetitions = ra.repetitions;
            }
            return true;
        }
    } else if (const Mul *mul = e.as<Mul>()) {
        const int64_t *b = nullptr;
        if (is_interleaved_ramp(mul->a, scope, result) &&
            (b = as_const_int(mul->b))) {
            result->base = simplify(result->base * (int)(*b));
            result->stride = simplify(result->stride * (int)(*b));
            return true;
        }
    } else if (const Div *div = e.as<Div>()) {
        const int64_t *b = nullptr;
        if (is_interleaved_ramp(div->a, scope, result) &&
            (b = as_const_int(div->b)) &&
            is_one(result->stride) &&
            (result->repetitions == 1 ||
             result->repetitions == 0) &&
            can_prove((result->base % (int)(*b)) == 0)) {
            // TODO: Generalize this. Currently only matches
            // ramp(base*b, 1, lanes) / b
            // broadcast(base * b, lanes) / b
            result->base = simplify(result->base / (int)(*b));
            result->repetitions *= (int)(*b);
            return true;
        }
    } else if (const Variable *var = e.as<Variable>()) {
        if (scope.contains(var->name)) {
            return is_interleaved_ramp(scope.get(var->name), scope, result);
        }
    }
    return false;
}

// Allocations inside vectorized loops grow an additional inner
// dimension to represent the separate copy of the allocation per
// vector lane. This means loads and stores to them need to be
// rewritten slightly.
class RewriteAccessToVectorAlloc : public IRMutator {
    Expr var;
    string alloc;
    int lanes;

    using IRMutator::visit;

    Expr mutate_index(const string &a, Expr index) {
        index = mutate(index);
        if (a == alloc) {
            return index * lanes + var;
        } else {
            return index;
        }
    }

    ModulusRemainder mutate_alignment(const string &a, const ModulusRemainder &align) {
        if (a == alloc) {
            return align * lanes;
        } else {
            return align;
        }
    }

    Expr visit(const Load *op) override {
        return Load::make(op->type, op->name, mutate_index(op->name, op->index),
                          op->image, op->param, mutate(op->predicate), mutate_alignment(op->name, op->alignment));
    }

    Stmt visit(const Store *op) override {
        return Store::make(op->name, mutate(op->value), mutate_index(op->name, op->index),
                           op->param, mutate(op->predicate), mutate_alignment(op->name, op->alignment));
    }

public:
    RewriteAccessToVectorAlloc(const string &v, string a, int l)
        : var(Variable::make(Int(32), v)), alloc(std::move(a)), lanes(l) {
    }
};

class UsesGPUVars : public IRVisitor {
private:
    using IRVisitor::visit;
    void visit(const Variable *op) override {
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            debug(3) << "Found gpu loop var: " << op->name << "\n";
            uses_gpu = true;
        }
    }

public:
    bool uses_gpu = false;
};

bool uses_gpu_vars(const Expr &s) {
    UsesGPUVars uses;
    s.accept(&uses);
    return uses.uses_gpu;
}

// Wrap a vectorized predicate around a Load/Store node.
class PredicateLoadStore : public IRMutator {
    string var;
    Expr vector_predicate;
    bool in_hexagon;
    const Target &target;
    int lanes;
    bool valid;
    bool vectorized;

    using IRMutator::visit;

    bool should_predicate_store_load(int bit_size) {
        if (in_hexagon) {
            internal_assert(target.features_any_of({Target::HVX_64, Target::HVX_128}))
                << "We are inside a hexagon loop, but the target doesn't have hexagon's features\n";
            return true;
        } else if (target.arch == Target::X86) {
            // Should only attempt to predicate store/load if the lane size is
            // no less than 4
            // TODO: disabling for now due to trunk LLVM breakage.
            // See: https://github.com/halide/Halide/issues/3534
            // return (bit_size == 32) && (lanes >= 4);
            return false;
        }
        // For other architecture, do not predicate vector load/store
        return false;
    }

    Expr merge_predicate(Expr pred, const Expr &new_pred) {
        if (pred.type().lanes() == new_pred.type().lanes()) {
            Expr res = simplify(pred && new_pred);
            return res;
        }
        valid = false;
        return pred;
    }

    Expr visit(const Load *op) override {
        valid = valid && should_predicate_store_load(op->type.bits());
        if (!valid) {
            return op;
        }

        Expr predicate, index;
        if (!op->index.type().is_scalar()) {
            internal_assert(op->predicate.type().lanes() == lanes);
            internal_assert(op->index.type().lanes() == lanes);

            predicate = mutate(op->predicate);
            index = mutate(op->index);
        } else if (expr_uses_var(op->index, var)) {
            predicate = mutate(Broadcast::make(op->predicate, lanes));
            index = mutate(Broadcast::make(op->index, lanes));
        } else {
            return IRMutator::visit(op);
        }

        predicate = merge_predicate(predicate, vector_predicate);
        if (!valid) {
            return op;
        }
        vectorized = true;
        return Load::make(op->type, op->name, index, op->image, op->param, predicate, op->alignment);
    }

    Stmt visit(const Store *op) override {
        valid = valid && should_predicate_store_load(op->value.type().bits());
        if (!valid) {
            return op;
        }

        Expr predicate, value, index;
        if (!op->index.type().is_scalar()) {
            internal_assert(op->predicate.type().lanes() == lanes);
            internal_assert(op->index.type().lanes() == lanes);
            internal_assert(op->value.type().lanes() == lanes);

            predicate = mutate(op->predicate);
            value = mutate(op->value);
            index = mutate(op->index);
        } else if (expr_uses_var(op->index, var)) {
            predicate = mutate(Broadcast::make(op->predicate, lanes));
            value = mutate(Broadcast::make(op->value, lanes));
            index = mutate(Broadcast::make(op->index, lanes));
        } else {
            return IRMutator::visit(op);
        }

        predicate = merge_predicate(predicate, vector_predicate);
        if (!valid) {
            return op;
        }
        vectorized = true;
        return Store::make(op->name, value, index, op->param, predicate, op->alignment);
    }

    Expr visit(const Call *op) override {
        // We should not vectorize calls with side-effects
        valid = valid && op->is_pure();
        return IRMutator::visit(op);
    }

public:
    PredicateLoadStore(string v, const Expr &vpred, bool in_hexagon, const Target &t)
        : var(std::move(v)), vector_predicate(vpred), in_hexagon(in_hexagon), target(t),
          lanes(vpred.type().lanes()), valid(true), vectorized(false) {
        internal_assert(lanes > 1);
    }

    bool is_vectorized() const {
        return valid && vectorized;
    }
};

// Substitutes a vector for a scalar var in a Stmt. Used on the
// body of every vectorized loop.
class VectorSubs : public IRMutator {
    // The var we're vectorizing
    string var;

    // What we're replacing it with. Usually a ramp.
    Expr replacement;

    const Target &target;

    bool in_hexagon;  // Are we inside the hexagon loop?

    // A suffix to attach to widened variables.
    string widening_suffix;

    // A scope containing lets and letstmts whose values became
    // vectors.
    Scope<Expr> scope;

    // The same set of Exprs, indexed by the vectorized var name
    Scope<Expr> vector_scope;

    // A stack of all containing lets. We need to reinject the scalar
    // version of them if we scalarize inner code.
    vector<pair<string, Expr>> containing_lets;

    // Widen an expression to the given number of lanes.
    Expr widen(Expr e, int lanes) {
        if (e.type().lanes() == lanes) {
            return e;
        } else if (e.type().lanes() == 1) {
            return Broadcast::make(e, lanes);
        } else {
            internal_error << "Mismatched vector lanes in VectorSubs\n";
        }
        return Expr();
    }

    using IRMutator::visit;

    Expr visit(const Cast *op) override {
        Expr value = mutate(op->value);
        if (value.same_as(op->value)) {
            return op;
        } else {
            Type t = op->type.with_lanes(value.type().lanes());
            return Cast::make(t, value);
        }
    }

    Expr visit(const Variable *op) override {
        string widened_name = op->name + widening_suffix;
        if (op->name == var) {
            return replacement;
        } else if (scope.contains(op->name)) {
            // If the variable appears in scope then we previously widened
            // it and we use the new widened name for the variable.
            return Variable::make(scope.get(op->name).type(), widened_name);
        } else {
            return op;
        }
    }

    template<typename T>
    Expr mutate_binary_operator(const T *op) {
        Expr a = mutate(op->a), b = mutate(op->b);
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            int w = std::max(a.type().lanes(), b.type().lanes());
            return T::make(widen(a, w), widen(b, w));
        }
    }

    Expr visit(const Add *op) override {
        return mutate_binary_operator(op);
    }
    Expr visit(const Sub *op) override {
        return mutate_binary_operator(op);
    }
    Expr visit(const Mul *op) override {
        return mutate_binary_operator(op);
    }
    Expr visit(const Div *op) override {
        return mutate_binary_operator(op);
    }
    Expr visit(const Mod *op) override {
        return mutate_binary_operator(op);
    }
    Expr visit(const Min *op) override {
        return mutate_binary_operator(op);
    }
    Expr visit(const Max *op) override {
        return mutate_binary_operator(op);
    }
    Expr visit(const EQ *op) override {
        return mutate_binary_operator(op);
    }
    Expr visit(const NE *op) override {
        return mutate_binary_operator(op);
    }
    Expr visit(const LT *op) override {
        return mutate_binary_operator(op);
    }
    Expr visit(const LE *op) override {
        return mutate_binary_operator(op);
    }
    Expr visit(const GT *op) override {
        return mutate_binary_operator(op);
    }
    Expr visit(const GE *op) override {
        return mutate_binary_operator(op);
    }
    Expr visit(const And *op) override {
        return mutate_binary_operator(op);
    }
    Expr visit(const Or *op) override {
        return mutate_binary_operator(op);
    }

    Expr visit(const Select *op) override {
        Expr condition = mutate(op->condition);
        Expr true_value = mutate(op->true_value);
        Expr false_value = mutate(op->false_value);
        if (condition.same_as(op->condition) &&
            true_value.same_as(op->true_value) &&
            false_value.same_as(op->false_value)) {
            return op;
        } else {
            int lanes = std::max(true_value.type().lanes(), false_value.type().lanes());
            lanes = std::max(lanes, condition.type().lanes());
            // Widen the true and false values, but we don't have to widen the condition
            true_value = widen(true_value, lanes);
            false_value = widen(false_value, lanes);
            return Select::make(condition, true_value, false_value);
        }
    }

    Expr visit(const Load *op) override {
        Expr predicate = mutate(op->predicate);
        Expr index = mutate(op->index);

        if (predicate.same_as(op->predicate) && index.same_as(op->index)) {
            return op;
        } else {
            int w = index.type().lanes();
            predicate = widen(predicate, w);
            return Load::make(op->type.with_lanes(w), op->name, index, op->image,
                              op->param, predicate, op->alignment);
        }
    }

    Expr visit(const Call *op) override {
        // Widen the call by changing the lanes of all of its
        // arguments and its return type
        vector<Expr> new_args(op->args.size());
        bool changed = false;

        // Mutate the args
        int max_lanes = 0;
        for (size_t i = 0; i < op->args.size(); i++) {
            Expr old_arg = op->args[i];
            Expr new_arg = mutate(old_arg);
            if (!new_arg.same_as(old_arg)) changed = true;
            new_args[i] = new_arg;
            max_lanes = std::max(new_arg.type().lanes(), max_lanes);
        }

        if (!changed) {
            return op;
        } else if (op->name == Call::trace) {
            const int64_t *event = as_const_int(op->args[6]);
            internal_assert(event != nullptr);
            if (*event == halide_trace_begin_realization || *event == halide_trace_end_realization) {
                // Call::trace vectorizes uniquely for begin/end realization, because the coordinates
                // for these are actually min/extent pairs; we need to maintain the proper dimensionality
                // count and instead aggregate the widened values into a single pair.
                for (size_t i = 1; i <= 2; i++) {
                    const Call *call = new_args[i].as<Call>();
                    internal_assert(call && call->is_intrinsic(Call::make_struct));
                    if (i == 1) {
                        // values should always be empty for these events
                        internal_assert(call->args.empty());
                        continue;
                    }
                    vector<Expr> call_args(call->args.size());
                    for (size_t j = 0; j < call_args.size(); j += 2) {
                        Expr min_v = widen(call->args[j], max_lanes);
                        Expr extent_v = widen(call->args[j + 1], max_lanes);
                        Expr min_scalar = extract_lane(min_v, 0);
                        Expr max_scalar = min_scalar + extract_lane(extent_v, 0);
                        for (int k = 1; k < max_lanes; ++k) {
                            Expr min_k = extract_lane(min_v, k);
                            Expr extent_k = extract_lane(extent_v, k);
                            min_scalar = min(min_scalar, min_k);
                            max_scalar = max(max_scalar, min_k + extent_k);
                        }
                        call_args[j] = min_scalar;
                        call_args[j + 1] = max_scalar - min_scalar;
                    }
                    new_args[i] = Call::make(call->type.element_of(), Call::make_struct, call_args, Call::Intrinsic);
                }
            } else {
                // Call::trace vectorizes uniquely, because we want a
                // single trace call for the entire vector, instead of
                // scalarizing the call and tracing each element.
                for (size_t i = 1; i <= 2; i++) {
                    // Each struct should be a struct-of-vectors, not a
                    // vector of distinct structs.
                    const Call *call = new_args[i].as<Call>();
                    internal_assert(call && call->is_intrinsic(Call::make_struct));
                    // Widen the call args to have the same lanes as the max lanes found
                    vector<Expr> call_args(call->args.size());
                    for (size_t j = 0; j < call_args.size(); j++) {
                        call_args[j] = widen(call->args[j], max_lanes);
                    }
                    new_args[i] = Call::make(call->type.element_of(), Call::make_struct,
                                             call_args, Call::Intrinsic);
                }
                // One of the arguments to the trace helper
                // records the number of vector lanes in the type being
                // stored.
                new_args[5] = max_lanes;
                // One of the arguments to the trace helper
                // records the number entries in the coordinates (which we just widened)
                if (max_lanes > 1) {
                    new_args[9] = new_args[9] * max_lanes;
                }
            }
            return Call::make(op->type, Call::trace, new_args, op->call_type);
        } else {
            // Widen the args to have the same lanes as the max lanes found
            for (size_t i = 0; i < new_args.size(); i++) {
                new_args[i] = widen(new_args[i], max_lanes);
            }
            return Call::make(op->type.with_lanes(max_lanes), op->name, new_args,
                              op->call_type, op->func, op->value_index, op->image, op->param);
        }
    }

    Expr visit(const Let *op) override {

        // Vectorize the let value and check to see if it was vectorized by
        // this mutator. The type of the expression might already be vector
        // width.
        Expr mutated_value = mutate(op->value);
        bool was_vectorized = (!op->value.type().is_vector() &&
                               mutated_value.type().is_vector());

        // If the value was vectorized by this mutator, add a new name to
        // the scope for the vectorized value expression.
        string vectorized_name;
        if (was_vectorized) {
            vectorized_name = op->name + widening_suffix;
            scope.push(op->name, mutated_value);
            vector_scope.push(vectorized_name, mutated_value);
        }

        Expr mutated_body = mutate(op->body);

        InterleavedRamp ir;
        if (is_interleaved_ramp(mutated_value, vector_scope, &ir)) {
            return substitute(vectorized_name, mutated_value, mutated_body);
        } else if (mutated_value.same_as(op->value) &&
                   mutated_body.same_as(op->body)) {
            return op;
        } else if (was_vectorized) {
            scope.pop(op->name);
            vector_scope.pop(vectorized_name);
            return Let::make(vectorized_name, mutated_value, mutated_body);
        } else {
            return Let::make(op->name, mutated_value, mutated_body);
        }
    }

    Stmt visit(const LetStmt *op) override {
        Expr mutated_value = mutate(op->value);
        string mutated_name = op->name;

        // Check if the value was vectorized by this mutator.
        bool was_vectorized = (!op->value.type().is_vector() &&
                               mutated_value.type().is_vector());

        if (was_vectorized) {
            mutated_name += widening_suffix;
            scope.push(op->name, mutated_value);
            vector_scope.push(mutated_name, mutated_value);
            // Also keep track of the original let, in case inner code scalarizes.
            containing_lets.emplace_back(op->name, op->value);
        }

        Stmt mutated_body = mutate(op->body);

        if (was_vectorized) {
            containing_lets.pop_back();
            scope.pop(op->name);
            vector_scope.pop(mutated_name);

            // Inner code might have extracted my lanes using
            // extract_lane, which introduces a shuffle_vector. If
            // so we should define separate lets for the lanes and
            // get it to use those instead.
            mutated_body = ReplaceShuffleVectors(mutated_name).mutate(mutated_body);

            // Check if inner code wants my individual lanes.
            Type t = mutated_value.type();
            for (int i = 0; i < t.lanes(); i++) {
                string lane_name = mutated_name + ".lane." + std::to_string(i);
                if (stmt_uses_var(mutated_body, lane_name)) {
                    mutated_body =
                        LetStmt::make(lane_name, extract_lane(mutated_value, i), mutated_body);
                }
            }

            // Inner code may also have wanted my max or min lane
            bool uses_min_lane = stmt_uses_var(mutated_body, mutated_name + ".min_lane");
            bool uses_max_lane = stmt_uses_var(mutated_body, mutated_name + ".max_lane");

            if (uses_min_lane || uses_max_lane) {
                Interval i = bounds_of_lanes(mutated_value);

                if (uses_min_lane) {
                    mutated_body =
                        LetStmt::make(mutated_name + ".min_lane", i.min, mutated_body);
                }

                if (uses_max_lane) {
                    mutated_body =
                        LetStmt::make(mutated_name + ".max_lane", i.max, mutated_body);
                }
            }
        }

        InterleavedRamp ir;
        if (is_interleaved_ramp(mutated_value, vector_scope, &ir)) {
            return substitute(mutated_name, mutated_value, mutated_body);
        } else if (mutated_value.same_as(op->value) &&
                   mutated_body.same_as(op->body)) {
            return op;
        } else {
            return LetStmt::make(mutated_name, mutated_value, mutated_body);
        }
    }

    Stmt visit(const Provide *op) override {
        vector<Expr> new_args(op->args.size());
        vector<Expr> new_values(op->values.size());
        bool changed = false;

        // Mutate the args
        int max_lanes = 0;
        for (size_t i = 0; i < op->args.size(); i++) {
            Expr old_arg = op->args[i];
            Expr new_arg = mutate(old_arg);
            if (!new_arg.same_as(old_arg)) changed = true;
            new_args[i] = new_arg;
            max_lanes = std::max(new_arg.type().lanes(), max_lanes);
        }

        for (size_t i = 0; i < op->args.size(); i++) {
            Expr old_value = op->values[i];
            Expr new_value = mutate(old_value);
            if (!new_value.same_as(old_value)) changed = true;
            new_values[i] = new_value;
            max_lanes = std::max(new_value.type().lanes(), max_lanes);
        }

        if (!changed) {
            return op;
        } else {
            // Widen the args to have the same lanes as the max lanes found
            for (size_t i = 0; i < new_args.size(); i++) {
                new_args[i] = widen(new_args[i], max_lanes);
            }
            for (size_t i = 0; i < new_values.size(); i++) {
                new_values[i] = widen(new_values[i], max_lanes);
            }
            return Provide::make(op->name, new_values, new_args);
        }
    }

    Stmt visit(const Store *op) override {
        Expr predicate = mutate(op->predicate);
        Expr value = mutate(op->value);
        Expr index = mutate(op->index);

        if (predicate.same_as(op->predicate) && value.same_as(op->value) && index.same_as(op->index)) {
            return op;
        } else {
            int lanes = std::max(predicate.type().lanes(), std::max(value.type().lanes(), index.type().lanes()));
            return Store::make(op->name, widen(value, lanes), widen(index, lanes),
                               op->param, widen(predicate, lanes), op->alignment);
        }
    }

    Stmt visit(const AssertStmt *op) override {
        return (op->condition.type().lanes() > 1) ? scalarize(op) : op;
    }

    Stmt visit(const IfThenElse *op) override {
        Expr cond = mutate(op->condition);
        int lanes = cond.type().lanes();
        debug(3) << "Vectorizing over " << var << "\n"
                 << "Old: " << op->condition << "\n"
                 << "New: " << cond << "\n";

        Stmt then_case = mutate(op->then_case);
        Stmt else_case = mutate(op->else_case);

        if (lanes > 1) {
            // We have an if statement with a vector condition,
            // which would mean control flow divergence within the
            // SIMD lanes.

            bool vectorize_predicate = !uses_gpu_vars(cond);
            Stmt predicated_stmt;
            if (vectorize_predicate) {
                PredicateLoadStore p(var, cond, in_hexagon, target);
                predicated_stmt = p.mutate(then_case);
                vectorize_predicate = p.is_vectorized();
            }
            if (vectorize_predicate && else_case.defined()) {
                PredicateLoadStore p(var, !cond, in_hexagon, target);
                predicated_stmt = Block::make(predicated_stmt, p.mutate(else_case));
                vectorize_predicate = p.is_vectorized();
            }

            debug(4) << "IfThenElse should vectorize predicate over var " << var << "? " << vectorize_predicate << "; cond: " << cond << "\n";
            debug(4) << "Predicated stmt:\n"
                     << predicated_stmt << "\n";

            // First check if the condition is marked as likely.
            const Call *c = cond.as<Call>();
            if (c && (c->is_intrinsic(Call::likely) ||
                      c->is_intrinsic(Call::likely_if_innermost))) {

                // The meaning of the likely intrinsic is that
                // Halide should optimize for the case in which
                // *every* likely value is true. We can do that by
                // generating a scalar condition that checks if
                // the least-true lane is true.
                Expr all_true = bounds_of_lanes(c->args[0]).min;

                // Wrap it in the same flavor of likely
                all_true = Call::make(Bool(), c->name,
                                      {all_true}, Call::PureIntrinsic);

                if (!vectorize_predicate) {
                    // We should strip the likelies from the case
                    // that's going to scalarize, because it's no
                    // longer likely.
                    Stmt without_likelies =
                        IfThenElse::make(op->condition.as<Call>()->args[0],
                                         op->then_case, op->else_case);
                    Stmt stmt =
                        IfThenElse::make(all_true,
                                         then_case,
                                         scalarize(without_likelies));
                    debug(4) << "...With all_true likely: \n"
                             << stmt << "\n";
                    return stmt;
                } else {
                    Stmt stmt =
                        IfThenElse::make(all_true,
                                         then_case,
                                         predicated_stmt);
                    debug(4) << "...Predicated IfThenElse: \n"
                             << stmt << "\n";
                    return stmt;
                }
            } else {
                // It's some arbitrary vector condition.
                if (!vectorize_predicate) {
                    debug(4) << "...Scalarizing vector predicate: \n"
                             << Stmt(op) << "\n";
                    return scalarize(op);
                } else {
                    Stmt stmt = predicated_stmt;
                    debug(4) << "...Predicated IfThenElse: \n"
                             << stmt << "\n";
                    return stmt;
                }
            }
        } else {
            // It's an if statement on a scalar, we're ok to vectorize the innards.
            debug(3) << "Not scalarizing if then else\n";
            if (cond.same_as(op->condition) &&
                then_case.same_as(op->then_case) &&
                else_case.same_as(op->else_case)) {
                return op;
            } else {
                return IfThenElse::make(cond, then_case, else_case);
            }
        }
    }

    Stmt visit(const For *op) override {
        ForType for_type = op->for_type;
        if (for_type == ForType::Vectorized) {
            user_warning << "Warning: Encountered vector for loop over " << op->name
                         << " inside vector for loop over " << var << "."
                         << " Ignoring the vectorize directive for the inner for loop.\n";
            for_type = ForType::Serial;
        }

        Expr min = mutate(op->min);
        Expr extent = mutate(op->extent);

        Stmt body = op->body;

        if (min.type().is_vector()) {
            // Rebase the loop to zero and try again
            Expr var = Variable::make(Int(32), op->name);
            Stmt body = substitute(op->name, var + op->min, op->body);
            Stmt transformed = For::make(op->name, 0, op->extent, for_type, op->device_api, body);
            return mutate(transformed);
        }

        if (extent.type().is_vector()) {
            // We'll iterate up to the max over the lanes, but
            // inject an if statement inside the loop that stops
            // each lane from going too far.

            extent = bounds_of_lanes(extent).max;
            Expr var = Variable::make(Int(32), op->name);
            body = IfThenElse::make(likely(var < op->min + op->extent), body);
        }

        body = mutate(body);

        if (min.same_as(op->min) &&
            extent.same_as(op->extent) &&
            body.same_as(op->body) &&
            for_type == op->for_type) {
            return op;
        } else {
            return For::make(op->name, min, extent, for_type, op->device_api, body);
        }
    }

    Stmt visit(const Allocate *op) override {
        vector<Expr> new_extents;
        Expr new_expr;

        int lanes = replacement.type().lanes();

        // The new expanded dimension is innermost.
        new_extents.emplace_back(lanes);

        for (size_t i = 0; i < op->extents.size(); i++) {
            Expr extent = mutate(op->extents[i]);
            // For vector sizes, take the max over the lanes. Note
            // that we haven't changed the strides, which also may
            // vary per lane. This is a bit weird, but the way we
            // set up the vectorized memory means that lanes can't
            // clobber each others' memory, so it doesn't matter.
            if (extent.type().is_vector()) {
                extent = bounds_of_lanes(extent).max;
            }
            new_extents.push_back(extent);
        }

        if (op->new_expr.defined()) {
            new_expr = mutate(op->new_expr);
            user_assert(new_expr.type().is_scalar())
                << "Cannot vectorize an allocation with a varying new_expr per vector lane.\n";
        }

        Stmt body = op->body;

        // Rewrite loads and stores to this allocation like so:
        // foo[x] -> foo[x*lanes + v]
        string v = unique_name('v');
        body = RewriteAccessToVectorAlloc(v, op->name, lanes).mutate(body);

        scope.push(v, Ramp::make(0, 1, lanes));
        body = mutate(body);
        scope.pop(v);

        // Replace the widened 'v' with the actual ramp
        // foo[x*lanes + widened_v] -> foo[x*lanes + ramp(0, 1, lanes)]
        body = substitute(v + widening_suffix, Ramp::make(0, 1, lanes), body);

        // The variable itself could still exist inside an inner scalarized block.
        body = substitute(v, Variable::make(Int(32), var), body);

        return Allocate::make(op->name, op->type, op->memory_type, new_extents, op->condition, body, new_expr, op->free_function);
    }

    Stmt visit(const Atomic *op) override {
        // Recognize a few special cases that we can handle as within-vector reduction trees.
        do {
            if (!op->mutex_name.empty()) {
                // We can't vectorize over a mutex
                break;
            }

            // f[x] = f[x] <op> y
            const Store *store = op->body.as<Store>();
            if (!store) break;

            VectorReduce::Operator reduce_op = VectorReduce::Add;
            Expr a, b;
            if (const Add *add = store->value.as<Add>()) {
                a = add->a;
                b = add->b;
                reduce_op = VectorReduce::Add;
            } else if (const Mul *mul = store->value.as<Mul>()) {
                a = mul->a;
                b = mul->b;
                reduce_op = VectorReduce::Mul;
            } else if (const Min *min = store->value.as<Min>()) {
                a = min->a;
                b = min->b;
                reduce_op = VectorReduce::Min;
            } else if (const Max *max = store->value.as<Max>()) {
                a = max->a;
                b = max->b;
                reduce_op = VectorReduce::Max;
            } else if (const Cast *cast_op = store->value.as<Cast>()) {
                if (cast_op->type.element_of() == UInt(8) &&
                    cast_op->value.type().is_bool()) {
                    if (const And *and_op = cast_op->value.as<And>()) {
                        a = and_op->a;
                        b = and_op->b;
                        reduce_op = VectorReduce::And;
                    } else if (const Or *or_op = cast_op->value.as<Or>()) {
                        a = or_op->a;
                        b = or_op->b;
                        reduce_op = VectorReduce::Or;
                    }
                }
            }

            if (!a.defined() || !b.defined()) {
                break;
            }

            // Bools get cast to uint8 for storage. Strip off that
            // cast around any load.
            if (b.type().is_bool()) {
                const Cast *cast_op = b.as<Cast>();
                if (cast_op) {
                    b = cast_op->value;
                }
            }
            if (a.type().is_bool()) {
                const Cast *cast_op = b.as<Cast>();
                if (cast_op) {
                    a = cast_op->value;
                }
            }

            if (a.as<Variable>() && !b.as<Variable>()) {
                std::swap(a, b);
            }

            // We require b to be a var, because it should have been lifted.
            const Variable *var_b = b.as<Variable>();
            const Load *load_a = a.as<Load>();

            if (!var_b ||
                !scope.contains(var_b->name) ||
                !load_a ||
                load_a->name != store->name ||
                !is_one(load_a->predicate) ||
                !is_one(store->predicate)) {
                break;
            }

            b = scope.get(var_b->name);
            Expr store_index = mutate(store->index);
            Expr load_index = mutate(load_a->index);

            // The load and store indices must be the same interleaved
            // ramp (or the same scalar, in the total reduction case).
            InterleavedRamp store_ir, load_ir;
            Expr test;
            if (store_index.type().is_scalar()) {
                test = simplify(load_index == store_index);
            } else if (is_interleaved_ramp(store_index, vector_scope, &store_ir) &&
                       is_interleaved_ramp(load_index, vector_scope, &load_ir) &&
                       store_ir.repetitions == load_ir.repetitions &&
                       store_ir.lanes == load_ir.lanes) {
                test = simplify(store_ir.base == load_ir.base &&
                                store_ir.stride == load_ir.stride);
            }

            if (!test.defined()) {
                break;
            }

            if (is_zero(test)) {
                break;
            } else if (!is_one(test)) {
                // TODO: try harder by substituting in more things in scope
                break;
            }

            int output_lanes = 1;
            if (store_index.type().is_scalar()) {
                // The index doesn't depend on the value being
                // vectorized, so it's a total reduction.

                b = VectorReduce::make(reduce_op, b, 1);
            } else {

                output_lanes = store_index.type().lanes() / store_ir.repetitions;

                store_index = Ramp::make(store_ir.base, store_ir.stride, output_lanes);
                b = VectorReduce::make(reduce_op, b, output_lanes);
            }

            Expr new_load = Load::make(load_a->type.with_lanes(output_lanes),
                                       load_a->name, store_index, load_a->image,
                                       load_a->param, const_true(output_lanes),
                                       ModulusRemainder{});

            switch (reduce_op) {
            case VectorReduce::Add:
                b = new_load + b;
                break;
            case VectorReduce::Mul:
                b = new_load * b;
                break;
            case VectorReduce::Min:
                b = min(new_load, b);
                break;
            case VectorReduce::Max:
                b = max(new_load, b);
                break;
            case VectorReduce::And:
                b = cast(new_load.type(), cast(b.type(), new_load) && b);
                break;
            case VectorReduce::Or:
                b = cast(new_load.type(), cast(b.type(), new_load) || b);
                break;
            }

            Stmt s = Store::make(store->name, b, store_index, store->param,
                                 const_true(b.type().lanes()), store->alignment);

            // We may still need the atomic node, if there was more
            // parallelism than just the vectorization.
            s = Atomic::make(op->producer_name, op->mutex_name, s);

            return s;
        } while (0);

        // In the general case, if a whole stmt has to be done
        // atomically, we need to serialize.
        return scalarize(op);
    }

    Stmt scalarize(Stmt s) {
        // Wrap a serial loop around it. Maybe LLVM will have
        // better luck vectorizing it.

        // We'll need the original scalar versions of any containing lets.
        for (size_t i = containing_lets.size(); i > 0; i--) {
            const auto &l = containing_lets[i - 1];
            s = LetStmt::make(l.first, l.second, s);
        }

        const Ramp *r = replacement.as<Ramp>();
        internal_assert(r) << "Expected replacement in VectorSubs to be a ramp\n";
        return For::make(var, r->base, r->lanes, ForType::Serial, DeviceAPI::None, s);
    }

    Expr scalarize(Expr e) {
        // This method returns a select tree that produces a vector lanes
        // result expression

        Expr result;
        int lanes = replacement.type().lanes();

        for (int i = lanes - 1; i >= 0; --i) {
            // Hide all the vector let values in scope with a scalar version
            // in the appropriate lane.
            for (Scope<Expr>::const_iterator iter = scope.cbegin(); iter != scope.cend(); ++iter) {
                string name = iter.name() + ".lane." + std::to_string(i);
                Expr lane = extract_lane(iter.value(), i);
                e = substitute(iter.name(), Variable::make(lane.type(), name), e);
            }

            // Replace uses of the vectorized variable with the extracted
            // lane expression
            e = substitute(var, i, e);

            if (i == lanes - 1) {
                result = Broadcast::make(e, lanes);
            } else {
                Expr cond = (replacement == Broadcast::make(i, lanes));
                result = Select::make(cond, Broadcast::make(e, lanes), result);
            }
        }

        return result;
    }

public:
    VectorSubs(string v, Expr r, bool in_hexagon, const Target &t)
        : var(std::move(v)), replacement(std::move(r)), target(t), in_hexagon(in_hexagon) {
        widening_suffix = ".x" + std::to_string(replacement.type().lanes());
    }
};  // namespace

class FindVectorizableExprsInAtomicNode : public IRMutator {
    // An Atomic node protects all accesses to a given buffer. We
    // consider a name "poisoned" if it depends on an access to this
    // buffer. We can't lift or vectorize anything that has been
    // poisoned.
    Scope<> poisoned_names;
    bool poison = false;

    using IRMutator::visit;

    template<typename T>
    const T *visit_let(const T *op) {
        mutate(op->value);
        ScopedBinding<> bind_if(poison, poisoned_names, op->name);
        mutate(op->body);
        return op;
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Expr visit(const Load *op) override {
        // Even if the load is bad, maybe we can lift the index
        IRMutator::visit(op);

        poison |= poisoned_names.contains(op->name);
        return op;
    }

    Expr visit(const Variable *op) override {
        poison = poisoned_names.contains(op->name);
        return op;
    }

    Stmt visit(const Store *op) override {
        // A store poisons all subsequent loads, but loads before the
        // first store can be lifted.
        mutate(op->index);
        mutate(op->value);
        poisoned_names.push(op->name);
        return op;
    }

    Expr visit(const Call *op) override {
        IRMutator::visit(op);
        poison |= !op->is_pure();
        return op;
    }

public:
    using IRMutator::mutate;

    Expr mutate(const Expr &e) override {
        bool old_poison = poison;
        poison = false;
        IRMutator::mutate(e);
        if (!poison) {
            liftable.insert(e);
        }
        poison |= old_poison;
        // We're not actually mutating anything. This class is only a
        // mutator so that we can override a generic mutate() method.
        return e;
    }

    FindVectorizableExprsInAtomicNode(const string &buf, const map<string, Function> &env) {
        poisoned_names.push(buf);
        auto it = env.find(buf);
        if (it != env.end()) {
            // Handle tuples
            size_t n = it->second.values().size();
            if (n > 1) {
                for (size_t i = 0; i < n; i++) {
                    poisoned_names.push(buf + "." + std::to_string(i));
                }
            }
        }
    }

    std::set<Expr, ExprCompare> liftable;
};

class LiftVectorizableExprsOutOfSingleAtomicNode : public IRMutator {
    const std::set<Expr, ExprCompare> &liftable;

    using IRMutator::visit;

    template<typename StmtOrExpr, typename LetStmtOrLet>
    StmtOrExpr visit_let(const LetStmtOrLet *op) {
        if (liftable.count(op->value)) {
            // Lift it under its current name to avoid having to
            // rewrite the variables in other lifted exprs.
            // TODO: duplicate non-overlapping liftable let stmts due to unrolling.
            lifted.emplace_back(op->name, op->value);
            return mutate(op->body);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let<Stmt>(op);
    }

    Expr visit(const Let *op) override {
        return visit_let<Expr>(op);
    }

public:
    map<Expr, string, IRDeepCompare> already_lifted;
    vector<pair<string, Expr>> lifted;

    using IRMutator::mutate;

    Expr mutate(const Expr &e) override {
        if (liftable.count(e) && !is_const(e) && !e.as<Variable>()) {
            auto it = already_lifted.find(e);
            string name;
            if (it != already_lifted.end()) {
                name = it->second;
            } else {
                name = unique_name('t');
                lifted.emplace_back(name, e);
                already_lifted.emplace(e, name);
            }
            return Variable::make(e.type(), name);
        } else {
            return IRMutator::mutate(e);
        }
    }

    LiftVectorizableExprsOutOfSingleAtomicNode(const std::set<Expr, ExprCompare> &liftable)
        : liftable(liftable) {
    }
};

class LiftVectorizableExprsOutOfAllAtomicNodes : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const Atomic *op) override {
        FindVectorizableExprsInAtomicNode finder(op->producer_name, env);
        finder.mutate(op->body);
        LiftVectorizableExprsOutOfSingleAtomicNode lifter(finder.liftable);
        Stmt new_body = lifter.mutate(op->body);
        new_body = Atomic::make(op->producer_name, op->mutex_name, new_body);
        while (!lifter.lifted.empty()) {
            auto p = lifter.lifted.back();
            new_body = LetStmt::make(p.first, p.second, new_body);
            lifter.lifted.pop_back();
        }
        return new_body;
    }

    const map<string, Function> &env;

public:
    LiftVectorizableExprsOutOfAllAtomicNodes(const map<string, Function> &env)
        : env(env) {
    }
};

// Vectorize all loops marked as such in a Stmt
class VectorizeLoops : public IRMutator {
    const Target &target;
    bool in_hexagon;

    using IRMutator::visit;

    Stmt visit(const For *for_loop) override {
        bool old_in_hexagon = in_hexagon;
        if (for_loop->device_api == DeviceAPI::Hexagon) {
            in_hexagon = true;
        }

        Stmt stmt;
        if (for_loop->for_type == ForType::Vectorized) {
            const IntImm *extent = for_loop->extent.as<IntImm>();
            if (!extent || extent->value <= 1) {
                user_error << "Loop over " << for_loop->name
                           << " has extent " << for_loop->extent
                           << ". Can only vectorize loops over a "
                           << "constant extent > 1\n";
            }

            // Replace the var with a ramp within the body
            Expr for_var = Variable::make(Int(32), for_loop->name);
            Expr replacement = Ramp::make(for_loop->min, 1, extent->value);
            stmt = VectorSubs(for_loop->name, replacement, in_hexagon, target).mutate(for_loop->body);
        } else {
            stmt = IRMutator::visit(for_loop);
        }

        if (for_loop->device_api == DeviceAPI::Hexagon) {
            in_hexagon = old_in_hexagon;
        }

        return stmt;
    }

public:
    VectorizeLoops(const Target &t)
        : target(t), in_hexagon(false) {
    }
};

/** Check if all stores in a Stmt are to names in a given scope. Used
    by RemoveUnnecessaryAtomics below. */
class AllStoresInScope : public IRVisitor {
    using IRVisitor::visit;
    void visit(const Store *op) override {
        result = result && s.contains(op->name);
    }

public:
    bool result = true;
    const Scope<> &s;
    AllStoresInScope(const Scope<> &s)
        : s(s) {
    }
};
bool all_stores_in_scope(const Stmt &stmt, const Scope<> &scope) {
    AllStoresInScope checker(scope);
    stmt.accept(&checker);
    return checker.result;
}

/** Drop any atomic nodes protecting buffers that are only accessed
 * from a single thread. */
class RemoveUnnecessaryAtomics : public IRMutator {
    using IRMutator::visit;

    // Allocations made from within this same thread
    bool in_thread = false;
    Scope<> local_allocs;

    Stmt visit(const Allocate *op) override {
        ScopedBinding<> bind(local_allocs, op->name);
        return IRMutator::visit(op);
    }

    Stmt visit(const Atomic *op) override {
        if (!in_thread || all_stores_in_scope(op->body, local_allocs)) {
            return mutate(op->body);
        } else {
            return op;
        }
    }

    Stmt visit(const For *op) override {
        if (is_parallel(op->for_type)) {
            ScopedValue<bool> old_in_thread(in_thread, true);
            Scope<> old_local_allocs;
            old_local_allocs.swap(local_allocs);
            Stmt s = IRMutator::visit(op);
            old_local_allocs.swap(local_allocs);
            return s;
        } else {
            return IRMutator::visit(op);
        }
    }
};

}  // namespace

Stmt vectorize_loops(const Stmt &stmt, const map<string, Function> &env, const Target &t) {
    // Limit the scope of atomic nodes to just the necessary stuff.
    // TODO: Should this be an earlier pass? It's probably a good idea
    // for non-vectorizing stuff too.
    Stmt s = LiftVectorizableExprsOutOfAllAtomicNodes(env).mutate(stmt);
    s = VectorizeLoops(t).mutate(s);
    s = RemoveUnnecessaryAtomics().mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
