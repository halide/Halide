#include "FlattenNestedRamps.h"
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
        // of a dense load if possible.
        const int lanes = op->type.lanes();
        // This is about converting *to* a dense ramp, so we don't
        // want to do this if it's already a dense ramp.
        const Ramp *ramp = op->index.as<Ramp>();
        if (lanes > 1 &&
            is_one(op->predicate) &&
            (ramp == nullptr || ramp->lanes < lanes)) {
            vector<Expr> indices(lanes);
            for (int i = 0; i < lanes; i++) {
                indices[i] = extract_lane(op->index, i);
            }
            Expr min_lane = indices[0];
            for (Expr &idx : indices) {
                idx = simplify(idx);
                min_lane = min(idx, min_lane);
            }
            min_lane = simplify(min_lane);
            vector<int> const_indices;
            const_indices.reserve(lanes);
            int max_constant_offset = 0;
            for (Expr &idx : indices) {
                idx = simplify(idx - min_lane);
                const int64_t *i = as_const_int(idx);
                if (i) {
                    const_indices.push_back((int)(*i));
                    max_constant_offset = std::max((int)(*i), max_constant_offset);
                } else {
                    break;
                }
            }
            if ((int)const_indices.size() == lanes) {
                int stride = 0;
                for (int c : const_indices) {
                    stride = (int)gcd(stride, c);
                }
                for (int &c : const_indices) {
                    c /= stride;
                }

                int extent = (int)((max_constant_offset / stride) + 1);

                // TODO: justify this constant
                const int max_unused_lane_factor = 4;
                if (extent < max_unused_lane_factor * lanes) {
                    Expr dense_index = Ramp::make(min_lane, stride, extent);
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

}  // namespace

Stmt flatten_nested_ramps(const Stmt &s) {
    FlattenRamps flatten_ramps;
    return flatten_ramps.mutate(s);
}

Expr flatten_nested_ramps(const Expr &e) {
    FlattenRamps flatten_ramps;
    return flatten_ramps.mutate(e);
}

}  // namespace Internal
}  // namespace Halide
