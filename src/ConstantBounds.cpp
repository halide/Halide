#include "ConstantBounds.h"
#include "IR.h"
#include "IROperator.h"
#include "IRPrinter.h"

namespace Halide {
namespace Internal {

ConstantInterval constant_integer_bounds(const Expr &e,
                                         const Scope<ConstantInterval> &scope,
                                         std::map<Expr, ConstantInterval, ExprCompare> *cache) {
    internal_assert(e.defined());

    auto get_bounds = [&]() {
        // Compute the bounds of each IR node from the bounds of its args. Math
        // on ConstantInterval is in terms of infinite integers, so any op that
        // can overflow needs to cast the resulting interval back to the output
        // type.
        if (const UIntImm *op = e.as<UIntImm>()) {
            if (Int(64).can_represent(op->value)) {
                return ConstantInterval::single_point((int64_t)(op->value));
            } else {
                return ConstantInterval::everything();
            }
        } else if (const IntImm *op = e.as<IntImm>()) {
            return ConstantInterval::single_point(op->value);
        } else if (const Variable *op = e.as<Variable>()) {
            if (const auto *in = scope.find(op->name)) {
                return *in;
            }
        } else if (const Add *op = e.as<Add>()) {
            return cast(op->type, constant_integer_bounds(op->a) + constant_integer_bounds(op->b));
        } else if (const Sub *op = e.as<Sub>()) {
            return cast(op->type, constant_integer_bounds(op->a) - constant_integer_bounds(op->b));
        } else if (const Mul *op = e.as<Mul>()) {
            return cast(op->type, constant_integer_bounds(op->a) * constant_integer_bounds(op->b));
        } else if (const Div *op = e.as<Div>()) {
            // Can overflow when dividing type.min() by -1
            return cast(op->type, constant_integer_bounds(op->a) / constant_integer_bounds(op->b));
        } else if (const Mod *op = e.as<Mod>()) {
            return cast(op->type, constant_integer_bounds(op->a) % constant_integer_bounds(op->b));
        } else if (const Min *op = e.as<Min>()) {
            return min(constant_integer_bounds(op->a), constant_integer_bounds(op->b));
        } else if (const Max *op = e.as<Max>()) {
            return max(constant_integer_bounds(op->a), constant_integer_bounds(op->b));
        } else if (const Cast *op = e.as<Cast>()) {
            return cast(op->type, constant_integer_bounds(op->value));
        } else if (const Broadcast *op = e.as<Broadcast>()) {
            return constant_integer_bounds(op->value);
        } else if (const VectorReduce *op = e.as<VectorReduce>()) {
            int f = op->value.type().lanes() / op->type.lanes();
            ConstantInterval factor(f, f);
            ConstantInterval arg_bounds = constant_integer_bounds(op->value);
            switch (op->op) {
            case VectorReduce::Add:
                return cast(op->type, arg_bounds * factor);
            case VectorReduce::SaturatingAdd:
                return saturating_cast(op->type, arg_bounds * factor);
            case VectorReduce::Min:
            case VectorReduce::Max:
            case VectorReduce::And:
            case VectorReduce::Or:
                return arg_bounds;
            default:;
            }
        } else if (const Shuffle *op = e.as<Shuffle>()) {
            ConstantInterval arg_bounds = constant_integer_bounds(op->vectors[0]);
            for (size_t i = 1; i < op->vectors.size(); i++) {
                arg_bounds.include(constant_integer_bounds(op->vectors[i]));
            }
            return arg_bounds;
        } else if (const Call *op = e.as<Call>()) {
            // For all intrinsics that can't possibly overflow, we don't need the
            // final cast.
            if (op->is_intrinsic(Call::abs)) {
                return abs(constant_integer_bounds(op->args[0]));
            } else if (op->is_intrinsic(Call::absd)) {
                return abs(constant_integer_bounds(op->args[0]) -
                           constant_integer_bounds(op->args[1]));
            } else if (op->is_intrinsic(Call::count_leading_zeros) ||
                       op->is_intrinsic(Call::count_trailing_zeros)) {
                // Conservatively just say it's the potential number of zeros in the type.
                return ConstantInterval(0, op->args[0].type().bits());
            } else if (op->is_intrinsic(Call::halving_add)) {
                return (constant_integer_bounds(op->args[0]) +
                        constant_integer_bounds(op->args[1])) /
                       2;
            } else if (op->is_intrinsic(Call::halving_sub)) {
                return cast(op->type, (constant_integer_bounds(op->args[0]) -
                                       constant_integer_bounds(op->args[1])) /
                                          2);
            } else if (op->is_intrinsic(Call::rounding_halving_add)) {
                return (constant_integer_bounds(op->args[0]) +
                        constant_integer_bounds(op->args[1]) +
                        1) /
                       2;
            } else if (op->is_intrinsic(Call::saturating_add)) {
                return saturating_cast(op->type,
                                       (constant_integer_bounds(op->args[0]) +
                                        constant_integer_bounds(op->args[1])));
            } else if (op->is_intrinsic(Call::saturating_sub)) {
                return saturating_cast(op->type,
                                       (constant_integer_bounds(op->args[0]) -
                                        constant_integer_bounds(op->args[1])));
            } else if (op->is_intrinsic(Call::widening_add)) {
                return constant_integer_bounds(op->args[0]) +
                       constant_integer_bounds(op->args[1]);
            } else if (op->is_intrinsic(Call::widening_sub)) {
                // widening ops can't overflow ...
                return constant_integer_bounds(op->args[0]) -
                       constant_integer_bounds(op->args[1]);
            } else if (op->is_intrinsic(Call::widening_mul)) {
                return constant_integer_bounds(op->args[0]) *
                       constant_integer_bounds(op->args[1]);
            } else if (op->is_intrinsic(Call::widen_right_add)) {
                // but the widen_right versions can overflow
                return cast(op->type, (constant_integer_bounds(op->args[0]) +
                                       constant_integer_bounds(op->args[1])));
            } else if (op->is_intrinsic(Call::widen_right_sub)) {
                return cast(op->type, (constant_integer_bounds(op->args[0]) -
                                       constant_integer_bounds(op->args[1])));
            } else if (op->is_intrinsic(Call::widen_right_mul)) {
                return cast(op->type, (constant_integer_bounds(op->args[0]) *
                                       constant_integer_bounds(op->args[1])));
            } else if (op->is_intrinsic(Call::shift_right)) {
                return cast(op->type, constant_integer_bounds(op->args[0]) >> constant_integer_bounds(op->args[1]));
            } else if (op->is_intrinsic(Call::shift_left)) {
                return cast(op->type, constant_integer_bounds(op->args[0]) << constant_integer_bounds(op->args[1]));
            } else if (op->is_intrinsic(Call::rounding_shift_right)) {
                ConstantInterval ca = constant_integer_bounds(op->args[0]);
                ConstantInterval cb = constant_integer_bounds(op->args[1]);
                ConstantInterval rounding_term;
                if (cb.min_defined && cb.min > 0) {
                    auto rounding_term = ConstantInterval(1, 1) << (cb - ConstantInterval(1, 1));
                    // rounding shift right with a positive RHS can't overflow,
                    // so no cast required.
                    return (ca + rounding_term) >> cb;
                } else if (cb.max_defined && cb.max <= 0) {
                    return cast(op->type, ca << (-cb));
                } else {
                    auto rounding_term = ConstantInterval(0, 1) << max(cb - 1, 0);
                    return cast(op->type, (ca + rounding_term) >> cb);
                }
            }
            // If you add a new intrinsic here, also add it to the expression
            // generator in test/correctness/lossless_cast.cpp

            // TODO: mul_shift_right, rounding_mul_shift_right, widening_shift_left/right, rounding_shift_left
        }

        return ConstantInterval::bounds_of_type(e.type());
    };

    ConstantInterval ret;
    if (cache) {
        auto [it, cache_miss] = cache->try_emplace(e);
        if (cache_miss) {
            it->second = get_bounds();
        }
        ret = it->second;
    } else {
        ret = get_bounds();
    }

    internal_assert((!ret.min_defined || e.type().can_represent(ret.min)) &&
                    (!ret.max_defined || e.type().can_represent(ret.max)))
        << "constant_bounds returned defined bounds that are not representable in "
        << "the type of the Expr passed in.\n Expr: " << e << "\n Bounds: " << ret;

    return ret;
}

}  // namespace Internal
}  // namespace Halide
