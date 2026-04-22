#include "FlattenNestedRamps.h"
#include "Bounds.h"
#include "CSE.h"
#include "Deinterleave.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "MultiRamp.h"
#include "Simplify.h"

using std::vector;

namespace Halide {
namespace Internal {
namespace {

class FlattenRamps : public IRMutator {
    using IRMutator::visit;

    // Visit the scalar base and strides of a multiramp. They are scalars,
    // but technically could contain total reductions of nested vectors, so
    // we need to walk them.
    void mutate_multiramp_scalars(MultiRamp &mr) {
        mr.base = mutate(mr.base);
        for (Expr &s : mr.strides) {
            s = mutate(s);
        }
    }

    Expr visit(const Ramp *op) override {
        if (op->base.type().is_vector()) {
            if (MultiRamp mr;
                is_multiramp(op, Scope<Expr>::empty_scope(), &mr)) {
                // Flatten multiramps entirely in one go, instead of recursively
                // with the general case below, so that we get one big concat
                // instead of a concat-of-concats. The innermost dimension is
                // left as a Ramp.
                mutate_multiramp_scalars(mr);
                return Shuffle::make_concat(mr.flatten());
            } else {
                Expr base = mutate(op->base);
                Expr stride = mutate(op->stride);
                std::vector<Expr> ramp_elems;
                ramp_elems.reserve(op->lanes);
                for (int ix = 0; ix < op->lanes; ix++) {
                    ramp_elems.push_back(base + ix * stride);
                }

                return Shuffle::make_concat(ramp_elems);
            }
        }

        return IRMutator::visit(op);
    }

    Expr visit(const Broadcast *op) override {
        if (op->value.type().is_vector()) {
            Expr value = mutate(op->value);
            return Shuffle::make_broadcast(value, op->lanes);
        }

        return IRMutator::visit(op);
    }

    // Slice `v` down to `inner_lanes` starting at output lane `n*inner_lanes`,
    // matching the slicing done to the flattened index. Broadcasts of scalars
    // pass through unchanged (as a fresh broadcast of `inner_lanes`).
    static Expr slice_per_inner_ramp(const Expr &v, int n, int inner_lanes) {
        if (const Broadcast *b = v.as<Broadcast>()) {
            if (b->value.type().is_scalar()) {
                return Broadcast::make(b->value, inner_lanes);
            }
        }
        return Shuffle::make_slice(v, n * inner_lanes, 1, inner_lanes);
    }

    Expr visit(const Load *op) override {
        // Convert a load of a bounded span of indices into a shuffle
        // of a dense or strided load if possible.
        const int lanes = op->type.lanes();
        // This is about converting *to* a dense ramp, so we don't
        // want to do this if it's already a dense ramp.
        const Ramp *ramp = op->index.as<Ramp>();
        if (lanes > 1 &&
            is_const_one(op->predicate) &&
            (ramp == nullptr || ramp->lanes < lanes)) {

            Interval bounds_of_lanes = bounds_of_expr_in_scope(op->index, Scope<Interval>::empty_scope());
            Expr min_lane;
            if (!bounds_of_lanes.has_lower_bound()) {
                return IRMutator::visit(op);
            } else {
                min_lane = bounds_of_lanes.min;
            }

            // Extract each index as a scalar
            vector<Expr> indices(lanes);
            for (int i = 0; i < lanes; i++) {
                indices[i] = extract_lane(op->index, i);
            }

            // Check if the other indices are just the min index plus a constant
            vector<int> const_indices;
            const_indices.reserve(lanes);
            int max_constant_offset = 0;
            for (Expr &idx : indices) {
                idx = simplify(common_subexpression_elimination(idx - min_lane));
                if (auto i = as_const_int(idx)) {
                    const_indices.push_back((int)(*i));
                    max_constant_offset = std::max((int)(*i), max_constant_offset);
                } else {
                    break;
                }
            }

            // If they are, we'll have a full vector of const_indices
            if ((int)const_indices.size() == lanes) {
                // Compute the stride for the underlying strided load
                int stride = 0, extent = 1;
                if (max_constant_offset > 0) {
                    for (int c : const_indices) {
                        stride = (int)gcd(stride, c);
                    }
                    for (int &c : const_indices) {
                        c /= stride;
                    }
                    // Compute the number of elements loaded
                    extent = (max_constant_offset / stride) + 1;
                }

                // If we're gathering from a very large range, it
                // might be better to just do the gather rather than
                // doing a big dense load and then shuffling. We
                // currently do the big-load-and-shuffle if we're
                // going to use at least a quarter of the values
                // loaded.
                //
                // TODO: It would be good to be able to control this
                // in the schedule somehow.
                const int max_unused_lane_factor = 4;
                if (extent < max_unused_lane_factor * lanes) {
                    if (max_constant_offset == 0) {
                        // It's a load of a broadcast. Convert it to a broadcast of a load
                        Expr load = Load::make(op->type.element_of(), op->name, min_lane,
                                               op->image, op->param,
                                               const_true(), ModulusRemainder{});
                        return Broadcast::make(load, lanes);
                    } else {
                        // Turn it into a dense load and a shuffle
                        Expr dense_index =
                            Ramp::make(min_lane, make_const(min_lane.type(), stride), extent);
                        Expr dense_load =
                            Load::make(op->type.with_lanes(extent), op->name, dense_index,
                                       op->image, op->param,
                                       const_true(extent), ModulusRemainder{});
                        return Shuffle::make({dense_load}, const_indices);
                    }
                }
            }
        }

        // If the index is a multiramp, emit a concat of per-inner-ramp
        // dense/strided loads. This handles the case where the bounded-span
        // conversion above didn't fire (e.g. symbolic strides, or the
        // access range is too large for a single dense load). Doing the
        // concat directly (rather than letting the Ramp visitor flatten
        // the nested ramp into a big scalar-index load + a subtracted
        // broadcast offset) makes the structure visible to downstream
        // shuffle simplification rules.
        if (op->type.is_vector()) {
            if (MultiRamp mr;
                is_multiramp(op->index, Scope<Expr>::empty_scope(), &mr) &&
                mr.dimensions() >= 2) {

                Expr predicate = mutate(op->predicate);
                mutate_multiramp_scalars(mr);
                std::vector<Expr> sub_indices = mr.flatten();
                int inner_lanes = mr.lanes[0];
                Type elem_type = op->type.with_lanes(inner_lanes);
                std::vector<Expr> loads;
                loads.reserve(sub_indices.size());
                for (size_t n = 0; n < sub_indices.size(); n++) {
                    Expr p = slice_per_inner_ramp(predicate, (int)n, inner_lanes);
                    ModulusRemainder align = (n == 0) ? op->alignment : ModulusRemainder{};
                    loads.push_back(Load::make(elem_type, op->name, sub_indices[n],
                                               op->image, op->param, p, align));
                }
                return Shuffle::make_concat(loads);
            }
        }

        return IRMutator::visit(op);
    }

    Stmt visit(const Store *op) override {
        // If the index is a multiramp, unroll into a sequence of per-inner-ramp
        // stores, for the same reason as the Load visitor above.
        if (op->index.type().is_vector()) {
            if (MultiRamp mr;
                is_multiramp(op->index, Scope<Expr>::empty_scope(), &mr) &&
                mr.dimensions() >= 2) {

                Expr predicate = mutate(op->predicate);
                Expr value = mutate(op->value);
                mutate_multiramp_scalars(mr);
                std::vector<Expr> sub_indices = mr.flatten();
                int inner_lanes = mr.lanes[0];

                // The value and/or predicate may load from the buffer being
                // stored to, so they must be fully evaluated before any of
                // the stores run. Hoist non-trivial ones into LetStmts that
                // wrap the block of stores. Skip the hoisting if the expr
                // is already a Variable or a constant.
                auto needs_hoist = [](const Expr &e) {
                    return !is_const(e) && !e.as<Variable>();
                };
                std::string value_name, predicate_name;
                Expr value_ref = value, predicate_ref = predicate;
                if (needs_hoist(value)) {
                    value_name = unique_name('t');
                    value_ref = Variable::make(value.type(), value_name);
                }
                if (needs_hoist(predicate)) {
                    predicate_name = unique_name('t');
                    predicate_ref = Variable::make(predicate.type(), predicate_name);
                }

                std::vector<Stmt> stores;
                stores.reserve(sub_indices.size());
                for (size_t n = 0; n < sub_indices.size(); n++) {
                    Expr p = slice_per_inner_ramp(predicate_ref, (int)n, inner_lanes);
                    Expr v = slice_per_inner_ramp(value_ref, (int)n, inner_lanes);
                    ModulusRemainder align = (n == 0) ? op->alignment : ModulusRemainder{};
                    stores.push_back(Store::make(op->name, v, sub_indices[n],
                                                 op->param, p, align));
                }
                Stmt result = Block::make(stores);
                if (!predicate_name.empty()) {
                    result = LetStmt::make(predicate_name, predicate, result);
                }
                if (!value_name.empty()) {
                    result = LetStmt::make(value_name, value, result);
                }
                return result;
            }
        }
        return IRMutator::visit(op);
    }
};

/** Lower bit concatenation into vector interleaving followed by a vector
 * reinterpret. */
class LowerConcatBits : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::concat_bits)) {
            // Rewrite concat_bits into a shuffle followed by a vector reinterpret.
            Expr shuf = simplify(Shuffle::make_interleave(op->args));
            Expr e = Reinterpret::make(op->type, shuf);
            return mutate(e);
        }

        return IRMutator::visit(op);
    }
};

}  // namespace

Stmt flatten_nested_ramps(const Stmt &s) {
    return LowerConcatBits()(FlattenRamps()(s));
}

Expr flatten_nested_ramps(const Expr &e) {
    return LowerConcatBits()(FlattenRamps()(e));
}

}  // namespace Internal
}  // namespace Halide
