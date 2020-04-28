#include "FlattenNestedRamps.h"
#include "IRMutator.h"
#include "IROperator.h"

#include <numeric>

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
            std::vector<int> indices(op->lanes * value.type().lanes());
            for (int ix = 0; ix < op->lanes; ix++) {
                std::iota(indices.begin() + ix * value.type().lanes(),
                          indices.begin() + (ix + 1) * value.type().lanes(), 0);
            }

            return Shuffle::make({value}, indices);
        }

        return IRMutator::visit(op);
    }

public:
    FlattenRamps() {
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
