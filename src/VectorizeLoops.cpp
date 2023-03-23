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

Expr get_lane(const Expr &e, int l) {
    return Shuffle::make_slice(e, l, 0, 1);
}

/** Find the exact max and min lanes of a vector expression. Not
 * conservative like bounds_of_expr, but uses similar rules for some
 * common node types where it can be exact. If e is a nested vector,
 * the result will be the bounds of the vectors in each lane. */
Interval bounds_of_nested_lanes(const Expr &e) {
    if (const Add *add = e.as<Add>()) {
        if (const Broadcast *b = add->b.as<Broadcast>()) {
            Interval ia = bounds_of_nested_lanes(add->a);
            return {ia.min + b->value, ia.max + b->value};
        } else if (const Broadcast *b = add->a.as<Broadcast>()) {
            Interval ia = bounds_of_nested_lanes(add->b);
            return {b->value + ia.min, b->value + ia.max};
        }
    } else if (const Sub *sub = e.as<Sub>()) {
        if (const Broadcast *b = sub->b.as<Broadcast>()) {
            Interval ia = bounds_of_nested_lanes(sub->a);
            return {ia.min - b->value, ia.max - b->value};
        } else if (const Broadcast *b = sub->a.as<Broadcast>()) {
            Interval ia = bounds_of_nested_lanes(sub->b);
            return {b->value - ia.max, b->value - ia.max};
        }
    } else if (const Mul *mul = e.as<Mul>()) {
        if (const Broadcast *b = mul->b.as<Broadcast>()) {
            if (is_positive_const(b->value)) {
                Interval ia = bounds_of_nested_lanes(mul->a);
                return {ia.min * b->value, ia.max * b->value};
            } else if (is_negative_const(b->value)) {
                Interval ia = bounds_of_nested_lanes(mul->a);
                return {ia.max * b->value, ia.min * b->value};
            }
        } else if (const Broadcast *b = mul->a.as<Broadcast>()) {
            if (is_positive_const(b->value)) {
                Interval ia = bounds_of_nested_lanes(mul->b);
                return {b->value * ia.min, b->value * ia.max};
            } else if (is_negative_const(b->value)) {
                Interval ia = bounds_of_nested_lanes(mul->b);
                return {b->value * ia.max, b->value * ia.min};
            }
        }
    } else if (const Div *div = e.as<Div>()) {
        if (const Broadcast *b = div->b.as<Broadcast>()) {
            if (is_positive_const(b->value)) {
                Interval ia = bounds_of_nested_lanes(div->a);
                return {ia.min / b->value, ia.max / b->value};
            } else if (is_negative_const(b->value)) {
                Interval ia = bounds_of_nested_lanes(div->a);
                return {ia.max / b->value, ia.min / b->value};
            }
        }
    } else if (const And *and_ = e.as<And>()) {
        if (const Broadcast *b = and_->b.as<Broadcast>()) {
            Interval ia = bounds_of_nested_lanes(and_->a);
            return {ia.min && b->value, ia.max && b->value};
        } else if (const Broadcast *b = and_->a.as<Broadcast>()) {
            Interval ia = bounds_of_nested_lanes(and_->b);
            return {ia.min && b->value, ia.max && b->value};
        }
    } else if (const Or *or_ = e.as<Or>()) {
        if (const Broadcast *b = or_->b.as<Broadcast>()) {
            Interval ia = bounds_of_nested_lanes(or_->a);
            return {ia.min && b->value, ia.max && b->value};
        } else if (const Broadcast *b = or_->a.as<Broadcast>()) {
            Interval ia = bounds_of_nested_lanes(or_->b);
            return {ia.min && b->value, ia.max && b->value};
        }
    } else if (const Min *min = e.as<Min>()) {
        if (const Broadcast *b = min->b.as<Broadcast>()) {
            Interval ia = bounds_of_nested_lanes(min->a);
            return {Min::make(ia.min, b->value), Min::make(ia.max, b->value)};
        } else if (const Broadcast *b = min->a.as<Broadcast>()) {
            Interval ia = bounds_of_nested_lanes(min->b);
            return {Min::make(ia.min, b->value), Min::make(ia.max, b->value)};
        }
    } else if (const Max *max = e.as<Max>()) {
        if (const Broadcast *b = max->b.as<Broadcast>()) {
            Interval ia = bounds_of_nested_lanes(max->a);
            return {Max::make(ia.min, b->value), Max::make(ia.max, b->value)};
        } else if (const Broadcast *b = max->a.as<Broadcast>()) {
            Interval ia = bounds_of_nested_lanes(max->b);
            return {Max::make(ia.min, b->value), Max::make(ia.max, b->value)};
        }
    } else if (const Not *not_ = e.as<Not>()) {
        Interval ia = bounds_of_nested_lanes(not_->a);
        return {!ia.max, !ia.min};
    } else if (const Ramp *r = e.as<Ramp>()) {
        Expr last_lane_idx = make_const(r->base.type(), r->lanes - 1);
        if (is_positive_const(r->stride)) {
            return {r->base, r->base + last_lane_idx * r->stride};
        } else if (is_negative_const(r->stride)) {
            return {r->base + last_lane_idx * r->stride, r->base};
        }
    } else if (const LE *le = e.as<LE>()) {
        // The least true this can be is if we maximize the LHS and minimize the RHS.
        // The most true this can be is if we minimize the LHS and maximize the RHS.
        // This is only exact if one of the two sides is a Broadcast.
        Interval ia = bounds_of_nested_lanes(le->a);
        Interval ib = bounds_of_nested_lanes(le->b);
        if (ia.is_single_point() || ib.is_single_point()) {
            return {ia.max <= ib.min, ia.min <= ib.max};
        }
    } else if (const LT *lt = e.as<LT>()) {
        // The least true this can be is if we maximize the LHS and minimize the RHS.
        // The most true this can be is if we minimize the LHS and maximize the RHS.
        // This is only exact if one of the two sides is a Broadcast.
        Interval ia = bounds_of_nested_lanes(lt->a);
        Interval ib = bounds_of_nested_lanes(lt->b);
        if (ia.is_single_point() || ib.is_single_point()) {
            return {ia.max < ib.min, ia.min < ib.max};
        }

    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return {b->value, b->value};
    } else if (const Let *let = e.as<Let>()) {
        Interval ia = bounds_of_nested_lanes(let->value);
        Interval ib = bounds_of_nested_lanes(let->body);
        if (expr_uses_var(ib.min, let->name)) {
            ib.min = Let::make(let->name, let->value, ib.min);
        }
        if (expr_uses_var(ib.max, let->name)) {
            ib.max = Let::make(let->name, let->value, ib.max);
        }
        return ib;
    }

    // If all else fails, just take the explicit min and max over the
    // lanes
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

/** Similar to bounds_of_nested_lanes, but it recursively reduces
 * the bounds of nested vectors to scalars. */
Interval bounds_of_lanes(const Expr &e) {
    Interval bounds = bounds_of_nested_lanes(e);
    if (!bounds.min.type().is_scalar()) {
        bounds.min = bounds_of_lanes(bounds.min).min;
    }
    if (!bounds.max.type().is_scalar()) {
        bounds.max = bounds_of_lanes(bounds.max).max;
    }
    return bounds;
}

// A ramp with the lanes repeated inner_repetitions times, and then
// the whole vector repeated outer_repetitions times.
// E.g: <0 0 2 2 4 4 6 6 0 0 2 2 4 4 6 6>.
struct InterleavedRamp {
    Expr base, stride;
    int lanes, inner_repetitions, outer_repetitions;
};

bool equal_or_zero(int a, int b) {
    return a == 0 || b == 0 || a == b;
}

bool is_interleaved_ramp(const Expr &e, const Scope<Expr> &scope, InterleavedRamp *result) {
    if (const Ramp *r = e.as<Ramp>()) {
        const Broadcast *b_base = r->base.as<Broadcast>();
        const Broadcast *b_stride = r->stride.as<Broadcast>();
        if (r->base.type().is_scalar()) {
            result->base = r->base;
            result->stride = r->stride;
            result->lanes = r->lanes;
            result->inner_repetitions = 1;
            result->outer_repetitions = 1;
            return true;
        } else if (b_base && b_stride && b_base->lanes == b_stride->lanes) {
            // Ramp of broadcast
            result->base = b_base->value;
            result->stride = b_stride->value;
            result->lanes = r->lanes;
            result->inner_repetitions = b_base->lanes;
            result->outer_repetitions = 1;
            return true;
        }
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        if (b->value.type().is_scalar()) {
            result->base = b->value;
            result->stride = 0;
            result->lanes = b->lanes;
            result->inner_repetitions = 0;
            result->outer_repetitions = 0;
            return true;
        } else if (is_interleaved_ramp(b->value, scope, result)) {
            // Broadcast of interleaved ramp
            result->outer_repetitions *= b->lanes;
            return true;
        }
    } else if (const Add *add = e.as<Add>()) {
        InterleavedRamp ra;
        if (is_interleaved_ramp(add->a, scope, &ra) &&
            is_interleaved_ramp(add->b, scope, result) &&
            equal_or_zero(ra.inner_repetitions, result->inner_repetitions) &&
            equal_or_zero(ra.outer_repetitions, result->outer_repetitions)) {
            result->base = simplify(result->base + ra.base);
            result->stride = simplify(result->stride + ra.stride);
            result->inner_repetitions = std::max(result->inner_repetitions, ra.inner_repetitions);
            result->outer_repetitions = std::max(result->outer_repetitions, ra.outer_repetitions);
            return true;
        }
    } else if (const Sub *sub = e.as<Sub>()) {
        InterleavedRamp ra;
        if (is_interleaved_ramp(sub->a, scope, &ra) &&
            is_interleaved_ramp(sub->b, scope, result) &&
            equal_or_zero(ra.inner_repetitions, result->inner_repetitions) &&
            equal_or_zero(ra.outer_repetitions, result->outer_repetitions)) {
            result->base = simplify(ra.base - result->base);
            result->stride = simplify(ra.stride - result->stride);
            result->inner_repetitions = std::max(result->inner_repetitions, ra.inner_repetitions);
            result->outer_repetitions = std::max(result->outer_repetitions, ra.outer_repetitions);
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
            is_const_one(result->stride) &&
            (result->inner_repetitions == 1 ||
             result->inner_repetitions == 0) &&
            can_prove((result->base % (int)(*b)) == 0)) {
            // TODO: Generalize this. Currently only matches
            // ramp(base*b, 1, lanes) / b
            // broadcast(base * b, lanes) / b
            result->base = simplify(result->base / (int)(*b));
            result->inner_repetitions *= (int)(*b);
            return true;
        }
    } else if (const Mod *mod = e.as<Mod>()) {
        const int64_t *b = nullptr;
        if (is_interleaved_ramp(mod->a, scope, result) &&
            (b = as_const_int(mod->b)) &&
            (result->outer_repetitions == 1 ||
             result->outer_repetitions == 0) &&
            can_prove(((int)(*b) % result->stride) == 0)) {
            // ramp(base, 2, lanes) % 8
            result->base = simplify(result->base % (int)(*b));
            result->stride = simplify(result->stride % (int)(*b));
            result->outer_repetitions *= (int)(*b);
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

class SerializeLoops : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        if (op->for_type == ForType::Vectorized) {
            return For::make(op->name, op->min, op->extent,
                             ForType::Serial, op->device_api, mutate(op->body));
        }

        return IRMutator::visit(op);
    }
};

// Wrap a vectorized predicate around a Load/Store node.
class PredicateLoadStore : public IRMutator {
    string var;
    Expr vector_predicate;
    int lanes;
    bool valid;
    bool vectorized;

    using IRMutator::visit;

    Expr merge_predicate(Expr pred, const Expr &new_pred) {
        if (pred.type().lanes() == new_pred.type().lanes()) {
            Expr res = simplify(pred && new_pred);
            return res;
        }
        valid = false;
        return pred;
    }

    Expr visit(const Load *op) override {
        valid = valid && ((op->predicate.type().lanes() == lanes) || (op->predicate.type().is_scalar() && !expr_uses_var(op->index, var)));
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
        valid = valid && ((op->predicate.type().lanes() == lanes) || (op->predicate.type().is_scalar() && !expr_uses_var(op->index, var)));
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

    Expr visit(const VectorReduce *op) override {
        // We can't predicate vector reductions.
        valid = valid && is_const_one(vector_predicate);
        return op;
    }

public:
    PredicateLoadStore(string v, const Expr &vpred)
        : var(std::move(v)), vector_predicate(vpred), lanes(vpred.type().lanes()), valid(true), vectorized(false) {
        internal_assert(lanes > 1);
    }

    bool is_vectorized() const {
        return valid && vectorized;
    }
};

Stmt vectorize_statement(const Stmt &stmt);

struct VectorizedVar {
    string name;
    Expr min;
    int lanes;
};

// Substitutes a vector for a scalar var in a Stmt. Used on the
// body of every vectorized loop.
class VectorSubs : public IRMutator {
    // A list of vectorized loop vars encountered so far. The last
    // element corresponds to the most inner vectorized loop.
    std::vector<VectorizedVar> vectorized_vars;

    // What we're replacing it with. Usually a combination of ramps
    // and broadcast. It depends on the current loop level and
    // is updated when vectorized_vars list is updated.
    std::map<string, Expr> replacements;

    // A scope containing lets and letstmts whose values became
    // vectors. Contains are original, non-vectorized expressions.
    Scope<Expr> scope;

    // Based on the same set of Exprs, but indexed by the vectorized
    // var name and holding vectorized expression.
    Scope<Expr> vector_scope;

    // A stack of all containing lets. We need to reinject the scalar
    // version of them if we scalarize inner code.
    vector<pair<string, Expr>> containing_lets;

    // Widen an expression to the given number of lanes.
    Expr widen(Expr e, int lanes) {
        if (e.type().lanes() == lanes) {
            return e;
        } else if (lanes % e.type().lanes() == 0) {
            return Broadcast::make(e, lanes / e.type().lanes());
        } else {
            internal_error << "Mismatched vector lanes in VectorSubs " << e.type().lanes()
                           << " " << lanes << "\n";
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

    Expr visit(const Reinterpret *op) override {
        Expr value = mutate(op->value);
        if (value.same_as(op->value)) {
            return op;
        } else {
            Type t = op->type.with_lanes(value.type().lanes());
            return Reinterpret::make(t, value);
        }
    }

    string get_widened_var_name(const string &name) {
        return name + ".widened." + vectorized_vars.back().name;
    }

    Expr visit(const Variable *op) override {
        if (replacements.count(op->name) > 0) {
            return replacements[op->name];
        } else if (scope.contains(op->name)) {
            string widened_name = get_widened_var_name(op->name);
            return Variable::make(vector_scope.get(widened_name).type(), widened_name);
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

        // Mutate the args
        auto [new_args, changed] = mutate_with_changes(op->args);
        int max_lanes = 0;
        for (const auto &new_arg : new_args) {
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
                    const Call *make_struct = Call::as_intrinsic(new_args[i], {Call::make_struct});
                    internal_assert(make_struct);
                    if (i == 1) {
                        // values should always be empty for these events
                        internal_assert(make_struct->args.empty());
                        continue;
                    }
                    vector<Expr> call_args(make_struct->args.size());
                    for (size_t j = 0; j < call_args.size(); j += 2) {
                        Expr min_v = widen(make_struct->args[j], max_lanes);
                        Expr extent_v = widen(make_struct->args[j + 1], max_lanes);
                        Expr min_scalar = get_lane(min_v, 0);
                        Expr max_scalar = min_scalar + get_lane(extent_v, 0);
                        for (int k = 1; k < max_lanes; ++k) {
                            Expr min_k = get_lane(min_v, k);
                            Expr extent_k = get_lane(extent_v, k);
                            min_scalar = min(min_scalar, min_k);
                            max_scalar = max(max_scalar, min_k + extent_k);
                        }
                        call_args[j] = min_scalar;
                        call_args[j + 1] = max_scalar - min_scalar;
                    }
                    new_args[i] = Call::make(make_struct->type.element_of(), Call::make_struct, call_args, Call::Intrinsic);
                }
            } else {
                // Call::trace vectorizes uniquely, because we want a
                // single trace call for the entire vector, instead of
                // scalarizing the call and tracing each element.
                for (size_t i = 1; i <= 2; i++) {
                    // Each struct should be a struct-of-vectors, not a
                    // vector of distinct structs.
                    const Call *make_struct = Call::as_intrinsic(new_args[i], {Call::make_struct});
                    internal_assert(make_struct);
                    // Widen the call args to have the same lanes as the max lanes found
                    vector<Expr> call_args(make_struct->args.size());
                    for (size_t j = 0; j < call_args.size(); j++) {
                        call_args[j] = widen(make_struct->args[j], max_lanes);
                    }
                    new_args[i] = Call::make(make_struct->type.element_of(), Call::make_struct,
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
        } else if (op->is_intrinsic(Call::if_then_else) && op->args.size() == 2) {
            Expr cond = widen(new_args[0], max_lanes);
            Expr true_value = widen(new_args[1], max_lanes);

            const Load *load = true_value.as<Load>();
            if (load) {
                return Load::make(op->type.with_lanes(max_lanes), load->name, load->index, load->image, load->param, cond, load->alignment);
            }
        }

        // Widen the args to have the same lanes as the max lanes found
        for (auto &arg : new_args) {
            arg = widen(arg, max_lanes);
        }
        Type new_op_type = op->type.with_lanes(max_lanes);

        if (op->is_intrinsic(Call::prefetch)) {
            // We don't want prefetch args to ve vectorized, but we can't just skip the mutation
            // (otherwise we can end up with dead loop variables. Instead, use extract_lane() on each arg
            // to scalarize it again.
            for (auto &arg : new_args) {
                if (arg.type().is_vector()) {
                    arg = extract_lane(arg, 0);
                }
            }
            new_op_type = op->type;
        }

        return Call::make(new_op_type, op->name, new_args,
                          op->call_type, op->func, op->value_index, op->image, op->param);
    }

    Expr visit(const Let *op) override {
        // Vectorize the let value and check to see if it was vectorized by
        // this mutator. The type of the expression might already be vector
        // width.
        Expr mutated_value = simplify(mutate(op->value));
        bool was_vectorized = (!op->value.type().is_vector() &&
                               mutated_value.type().is_vector());

        // If the value was vectorized by this mutator, add a new name to
        // the scope for the vectorized value expression.
        string vectorized_name;
        if (was_vectorized) {
            vectorized_name = get_widened_var_name(op->name);
            scope.push(op->name, op->value);
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
        Expr mutated_value = simplify(mutate(op->value));
        string vectorized_name = op->name;

        // Check if the value was vectorized by this mutator.
        bool was_vectorized = (!op->value.type().is_vector() &&
                               mutated_value.type().is_vector());

        if (was_vectorized) {
            vectorized_name = get_widened_var_name(op->name);
            scope.push(op->name, op->value);
            vector_scope.push(vectorized_name, mutated_value);
            // Also keep track of the original let, in case inner code scalarizes.
            containing_lets.emplace_back(op->name, op->value);
        }

        Stmt mutated_body = mutate(op->body);

        if (was_vectorized) {
            containing_lets.pop_back();
            scope.pop(op->name);
            vector_scope.pop(vectorized_name);
        }

        InterleavedRamp ir;
        if (is_interleaved_ramp(mutated_value, vector_scope, &ir)) {
            return substitute(vectorized_name, mutated_value, mutated_body);
        } else if (mutated_value.same_as(op->value) &&
                   mutated_body.same_as(op->body)) {
            return op;
        } else {
            return LetStmt::make(vectorized_name, mutated_value, mutated_body);
        }
    }

    Stmt visit(const Provide *op) override {
        internal_error << "Vectorizing a Provide node is unimplemented. "
                       << "Vectorization usually runs after storage flattening.\n";
        return Stmt();
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

        debug(3) << "Vectorizing \n"
                 << "Old: " << op->condition << "\n"
                 << "New: " << cond << "\n";

        Stmt then_case = mutate(op->then_case);
        Stmt else_case = mutate(op->else_case);

        if (lanes > 1) {
            // We have an if statement with a vector condition,
            // which would mean control flow divergence within the
            // SIMD lanes.

            bool vectorize_predicate = true;

            Stmt predicated_stmt;
            if (vectorize_predicate) {
                PredicateLoadStore p(vectorized_vars.front().name, cond);
                predicated_stmt = p.mutate(then_case);
                vectorize_predicate = p.is_vectorized();
            }
            if (vectorize_predicate && else_case.defined()) {
                PredicateLoadStore p(vectorized_vars.front().name, !cond);
                predicated_stmt = Block::make(predicated_stmt, p.mutate(else_case));
                vectorize_predicate = p.is_vectorized();
            }

            debug(4) << "IfThenElse should vectorize predicate "
                     << "? " << vectorize_predicate << "; cond: " << cond << "\n";
            debug(4) << "Predicated stmt:\n"
                     << predicated_stmt << "\n";

            // First check if the condition is marked as likely.
            if (const Call *likely = Call::as_intrinsic(cond, {Call::likely, Call::likely_if_innermost})) {

                // The meaning of the likely intrinsic is that
                // Halide should optimize for the case in which
                // *every* likely value is true. We can do that by
                // generating a scalar condition that checks if
                // the least-true lane is true.
                Expr all_true = bounds_of_lanes(likely->args[0]).min;
                // Wrap it in the same flavor of likely
                all_true = Call::make(Bool(), likely->name,
                                      {all_true}, Call::PureIntrinsic);

                if (!vectorize_predicate) {
                    // We should strip the likelies from the case
                    // that's going to scalarize, because it's no
                    // longer likely.
                    Stmt without_likelies =
                        IfThenElse::make(unwrap_tags(op->condition),
                                         op->then_case, op->else_case);

                    // scalarize() will put back all vectorized loops around the statement as serial,
                    // but it still may happen that there are vectorized loops inside of the statement
                    // itself which we may want to handle. All the context is invalid though, so
                    // we just start anew for this specific statement.
                    Stmt scalarized = scalarize(without_likelies, false);
                    scalarized = vectorize_statement(scalarized);
                    Stmt stmt =
                        IfThenElse::make(all_true,
                                         then_case,
                                         scalarized);
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

        if (op->for_type == ForType::Vectorized) {
            const IntImm *extent_int = extent.as<IntImm>();
            if (!extent_int || extent_int->value <= 1) {
                user_error << "Loop over " << op->name
                           << " has extent " << extent
                           << ". Can only vectorize loops over a "
                           << "constant extent > 1\n";
            }

            vectorized_vars.push_back({op->name, min, (int)extent_int->value});
            update_replacements();
            // Go over lets which were vectorized and update them according to the current
            // loop level.
            for (auto it = scope.cbegin(); it != scope.cend(); ++it) {
                string vectorized_name = get_widened_var_name(it.name());
                Expr vectorized_value = mutate(it.value());
                vector_scope.push(vectorized_name, vectorized_value);
            }

            body = mutate(body);

            // Append vectorized lets for this loop level.
            for (auto it = scope.cbegin(); it != scope.cend(); ++it) {
                string vectorized_name = get_widened_var_name(it.name());
                Expr vectorized_value = vector_scope.get(vectorized_name);
                vector_scope.pop(vectorized_name);
                InterleavedRamp ir;
                if (is_interleaved_ramp(vectorized_value, vector_scope, &ir)) {
                    body = substitute(vectorized_name, vectorized_value, body);
                } else {
                    body = LetStmt::make(vectorized_name, vectorized_value, body);
                }
            }
            vectorized_vars.pop_back();
            update_replacements();
            return body;
        } else {
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
    }

    Stmt visit(const Allocate *op) override {
        vector<Expr> new_extents;
        Expr new_expr;

        // The new expanded dimensions are innermost.
        for (const auto &vv : vectorized_vars) {
            new_extents.emplace_back(vv.lanes);
        }

        for (const auto &e : op->extents) {
            Expr extent = mutate(e);
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
        for (const auto &vv : vectorized_vars) {
            body = RewriteAccessToVectorAlloc(vv.name + ".from_zero", op->name, vv.lanes).mutate(body);
        }

        body = mutate(body);

        for (const auto &vv : vectorized_vars) {
            // The variable itself could still exist inside an inner scalarized block.
            body = substitute(vv.name + ".from_zero", Variable::make(Int(32), vv.name), body);
        }

        // Difficult to tell how the padding should grow when vectorizing an
        // allocation. It's not currently an issue, because vectorization
        // happens before the only source of padding (lowering strided
        // loads). Add an assert to enforce it.
        internal_assert(op->padding == 0) << "Vectorization of padded allocations not yet implemented";

        return Allocate::make(op->name, op->type, op->memory_type, new_extents, op->condition, body, new_expr, op->free_function);
    }

    Stmt visit(const Atomic *op) override {
        // Recognize a few special cases that we can handle as within-vector reduction trees.
        do {
            if (!op->mutex_name.empty()) {
                // We can't vectorize over a mutex
                break;
            }

            const Store *store = op->body.as<Store>();
            if (!store) {
                break;
            }

            // f[x] = y
            if (!expr_uses_var(store->value, store->name) &&
                !expr_uses_var(store->predicate, store->name)) {
                // This can be naively vectorized just fine. If there are
                // repeated values in the vectorized store index, the ordering
                // of writes may be undetermined and backend-dependent, but
                // they'll be atomic.
                Stmt s = mutate(store);

                // We may still need the atomic node, if there was more
                // parallelism than just the vectorization.
                s = Atomic::make(op->producer_name, op->mutex_name, s);
                return s;
            }

            // f[x] = f[x] <op> y
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
            } else if (const Call *call_op = store->value.as<Call>()) {
                if (call_op->is_intrinsic(Call::saturating_add)) {
                    a = call_op->args[0];
                    b = call_op->args[1];
                    reduce_op = VectorReduce::SaturatingAdd;
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
                !is_const_one(load_a->predicate) ||
                !is_const_one(store->predicate)) {
                break;
            }

            b = vector_scope.get(get_widened_var_name(var_b->name));
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
                       store_ir.inner_repetitions == load_ir.inner_repetitions &&
                       store_ir.outer_repetitions == load_ir.outer_repetitions &&
                       store_ir.lanes == load_ir.lanes) {
                test = simplify(store_ir.base == load_ir.base &&
                                store_ir.stride == load_ir.stride);
            }

            if (!test.defined()) {
                break;
            }

            if (is_const_zero(test)) {
                break;
            } else if (!is_const_one(test)) {
                // TODO: try harder by substituting in more things in scope
                break;
            }

            auto binop = [=](const Expr &a, const Expr &b) {
                switch (reduce_op) {
                case VectorReduce::Add:
                    return a + b;
                case VectorReduce::Mul:
                    return a * b;
                case VectorReduce::Min:
                    return min(a, b);
                case VectorReduce::Max:
                    return max(a, b);
                case VectorReduce::And:
                    return a && b;
                case VectorReduce::Or:
                    return a || b;
                case VectorReduce::SaturatingAdd:
                    return saturating_add(a, b);
                }
                return Expr();
            };

            int output_lanes = 1;
            if (store_index.type().is_scalar()) {
                // The index doesn't depend on the value being
                // vectorized, so it's a total reduction.

                b = VectorReduce::make(reduce_op, b, 1);
            } else {

                output_lanes = store_index.type().lanes() / (store_ir.inner_repetitions * store_ir.outer_repetitions);

                store_index = Ramp::make(store_ir.base, store_ir.stride, output_lanes / store_ir.base.type().lanes());
                if (store_ir.inner_repetitions > 1) {
                    b = VectorReduce::make(reduce_op, b, output_lanes * store_ir.outer_repetitions);
                }

                // Handle outer repetitions by unrolling the reduction
                // over slices.
                if (store_ir.outer_repetitions > 1) {
                    // First remove all powers of two with a binary reduction tree.
                    int reps = store_ir.outer_repetitions;
                    while (reps % 2 == 0) {
                        int l = b.type().lanes() / 2;
                        Expr b0 = Shuffle::make_slice(b, 0, 1, l);
                        Expr b1 = Shuffle::make_slice(b, l, 1, l);
                        b = binop(b0, b1);
                        reps /= 2;
                    }

                    // Then reduce linearly over slices for the rest.
                    if (reps > 1) {
                        Expr v = Shuffle::make_slice(b, 0, 1, output_lanes);
                        for (int i = 1; i < reps; i++) {
                            Expr slice = simplify(Shuffle::make_slice(b, i * output_lanes, 1, output_lanes));
                            v = binop(v, slice);
                        }
                        b = v;
                    }
                }
            }

            Expr new_load = Load::make(load_a->type.with_lanes(output_lanes),
                                       load_a->name, store_index, load_a->image,
                                       load_a->param, const_true(output_lanes),
                                       ModulusRemainder{});

            Expr lhs = cast(b.type(), new_load);
            b = binop(lhs, b);
            b = cast(new_load.type(), b);

            Stmt s = Store::make(store->name, b, store_index, store->param,
                                 const_true(b.type().lanes()), store->alignment);

            // We may still need the atomic node, if there was more
            // parallelism than just the vectorization.
            s = Atomic::make(op->producer_name, op->mutex_name, s);

            return s;
        } while (false);

        // In the general case, if a whole stmt has to be done
        // atomically, we need to serialize.
        return scalarize(op);
    }

    Stmt scalarize(Stmt s, bool serialize_inner_loops = true) {
        // Wrap a serial loop around it. Maybe LLVM will have
        // better luck vectorizing it.

        if (serialize_inner_loops) {
            s = SerializeLoops().mutate(s);
        }
        // We'll need the original scalar versions of any containing lets.
        for (size_t i = containing_lets.size(); i > 0; i--) {
            const auto &l = containing_lets[i - 1];
            s = LetStmt::make(l.first, l.second, s);
        }

        for (int ix = vectorized_vars.size() - 1; ix >= 0; ix--) {
            s = For::make(vectorized_vars[ix].name, vectorized_vars[ix].min,
                          vectorized_vars[ix].lanes, ForType::Serial, DeviceAPI::None, s);
        }

        return s;
    }

    Expr scalarize(Expr e) {
        // This method returns a select tree that produces a vector lanes
        // result expression
        user_assert(replacements.size() == 1) << "Can't scalarize nested vectorization\n";
        string var = replacements.begin()->first;
        Expr replacement = replacements.begin()->second;

        Expr result;
        int lanes = replacement.type().lanes();

        for (int i = lanes - 1; i >= 0; --i) {
            // Hide all the vector let values in scope with a scalar version
            // in the appropriate lane.
            for (Scope<Expr>::const_iterator iter = scope.cbegin(); iter != scope.cend(); ++iter) {
                e = substitute(iter.name(),
                               get_lane(Variable::make(iter.value().type(), iter.name()), i),
                               e);
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

    // Recompute all replacements for vectorized vars based on
    // the current stack of vectorized loops.
    void update_replacements() {
        replacements.clear();

        for (const auto &var : vectorized_vars) {
            // Two different replacements are needed for each loop var
            // one starting from zero and another starting from loop.min.
            replacements[var.name] = var.min;
            replacements[var.name + ".from_zero"] = 0;
        }

        Expr strided_ones = 1;
        for (int ix = vectorized_vars.size() - 1; ix >= 0; ix--) {
            for (int ik = 0; ik < (int)vectorized_vars.size(); ik++) {
                if (ix == ik) {
                    replacements[vectorized_vars[ik].name] =
                        Ramp::make(replacements[vectorized_vars[ik].name],
                                   strided_ones,
                                   vectorized_vars[ix].lanes);
                    replacements[vectorized_vars[ik].name + ".from_zero"] =
                        Ramp::make(replacements[vectorized_vars[ik].name + ".from_zero"],
                                   strided_ones,
                                   vectorized_vars[ix].lanes);
                } else {
                    replacements[vectorized_vars[ik].name] =
                        Broadcast::make(replacements[vectorized_vars[ik].name],
                                        vectorized_vars[ix].lanes);
                    replacements[vectorized_vars[ik].name + ".from_zero"] =
                        Broadcast::make(replacements[vectorized_vars[ik].name + ".from_zero"],
                                        vectorized_vars[ix].lanes);
                }
            }

            strided_ones = Broadcast::make(strided_ones, vectorized_vars[ix].lanes);
        }
    }

public:
    VectorSubs(const VectorizedVar &vv) {
        vectorized_vars.push_back(vv);
        update_replacements();
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
        // unsafe_promise_clamped and similar isn't pure because it's
        // not safe to lift it out of if statements. If *is* safe to
        // lift it out of atomic nodes though.
        poison |= !(op->is_pure() ||
                    op->is_intrinsic(Call::unsafe_promise_clamped) ||
                    op->is_intrinsic(Call::promise_clamped));
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
    using IRMutator::visit;

    Stmt visit(const For *for_loop) override {
        Stmt stmt;
        if (for_loop->for_type == ForType::Vectorized) {
            const IntImm *extent = for_loop->extent.as<IntImm>();
            if (!extent || extent->value <= 1) {
                user_error << "Loop over " << for_loop->name
                           << " has extent " << for_loop->extent
                           << ". Can only vectorize loops over a "
                           << "constant extent > 1\n";
            }

            VectorizedVar vectorized_var = {for_loop->name, for_loop->min, (int)extent->value};
            stmt = VectorSubs(vectorized_var).mutate(for_loop->body);
        } else {
            stmt = IRMutator::visit(for_loop);
        }

        return stmt;
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

Stmt vectorize_statement(const Stmt &stmt) {
    return VectorizeLoops().mutate(stmt);
}

}  // namespace
Stmt vectorize_loops(const Stmt &stmt, const map<string, Function> &env) {
    // Limit the scope of atomic nodes to just the necessary stuff.
    // TODO: Should this be an earlier pass? It's probably a good idea
    // for non-vectorizing stuff too.
    Stmt s = LiftVectorizableExprsOutOfAllAtomicNodes(env).mutate(stmt);
    s = vectorize_statement(s);
    s = RemoveUnnecessaryAtomics().mutate(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
