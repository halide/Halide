#include "FlattenNestedRamps.h"
#include "Bounds.h"
#include "CSE.h"
#include "Deinterleave.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"

using std::vector;

namespace Halide {
namespace Internal {
namespace {

class FlattenRamps : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Ramp *op) override {
        if (op->base.type().is_vector()) {
            Expr base = mutate(op->base);
            Expr stride = mutate(op->stride);
            std::vector<Expr> ramp_elems;
            for (int ix = 0; ix < op->lanes; ix++) {
                ramp_elems.push_back(base + ix * stride);
            }

            return Shuffle::make_concat(ramp_elems);
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
                const int64_t *i = as_const_int(idx);
                if (i) {
                    const_indices.push_back((int)(*i));
                    max_constant_offset = std::max((int)(*i), max_constant_offset);
                } else {
                    break;
                }
            }

            // If they are, we'll have a full vector of const_indices
            if ((int)const_indices.size() == lanes) {

                // Compute the stride for the underlying strided load
                int stride = 0;
                for (int c : const_indices) {
                    stride = (int)gcd(stride, c);
                }
                for (int &c : const_indices) {
                    c /= stride;
                }

                // Compute the number of elements loaded
                int extent = (int)((max_constant_offset / stride) + 1);

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
                    Expr dense_index = Ramp::make(min_lane, make_const(min_lane.type(), stride), extent);
                    Expr dense_load =
                        Load::make(op->type.with_lanes(extent), op->name, dense_index,
                                   op->image, op->param,
                                   const_true(extent), ModulusRemainder{});
                    return Shuffle::make({dense_load}, const_indices);
                }
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
    return LowerConcatBits().mutate(FlattenRamps().mutate(s));
}

Expr flatten_nested_ramps(const Expr &e) {
    return LowerConcatBits().mutate(FlattenRamps().mutate(e));
}

}  // namespace Internal
}  // namespace Halide
