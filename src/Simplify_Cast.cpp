#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Cast *op, ExprInfo *bounds) {
    Expr value = mutate(op->value, bounds);

    if (bounds) {
        // If either the min value or the max value can't be represented
        // in the destination type, or the min/max value is undefined,
        // the bounds need to be cleared (one-sided for no_overflow,
        // both sides for overflow types).
        if ((bounds->min_defined && !op->type.can_represent(bounds->min)) || !bounds->min_defined) {
            bounds->min_defined = false;
            if (!no_overflow(op->type)) {
                // If the type overflows, this invalidates the max too.
                bounds->max_defined = false;
            }
        }
        if ((bounds->max_defined && !op->type.can_represent(bounds->max)) || !bounds->max_defined) {
            if (!no_overflow(op->type)) {
                // If the type overflows, this invalidates the min too.
                bounds->min_defined = false;
            }
            bounds->max_defined = false;
        }
        if (!op->type.can_represent(bounds->alignment.modulus) ||
            !op->type.can_represent(bounds->alignment.remainder)) {
            bounds->alignment = ModulusRemainder();
        }
    }

    if (may_simplify(op->type) && may_simplify(op->value.type())) {
        const Cast *cast = value.as<Cast>();
        const Broadcast *broadcast_value = value.as<Broadcast>();
        const Ramp *ramp_value = value.as<Ramp>();
        double f = 0.0;
        int64_t i = 0;
        uint64_t u = 0;
        if (Call::as_intrinsic(value, {Call::signed_integer_overflow})) {
            clear_bounds_info(bounds);
            return make_signed_integer_overflow(op->type);
        } else if (value.type() == op->type) {
            return value;
        } else if (op->type.is_int() &&
                   const_float(value, &f) &&
                   std::isfinite(f)) {
            // float -> int
            // Recursively call mutate just to set the bounds
            return mutate(make_const(op->type, safe_numeric_cast<int64_t>(f)), bounds);
        } else if (op->type.is_uint() &&
                   const_float(value, &f) &&
                   std::isfinite(f)) {
            // float -> uint
            return make_const(op->type, safe_numeric_cast<uint64_t>(f));
        } else if (op->type.is_float() &&
                   const_float(value, &f)) {
            // float -> float
            return make_const(op->type, f);
        } else if (op->type.is_int() &&
                   const_int(value, &i)) {
            // int -> int
            // Recursively call mutate just to set the bounds
            return mutate(make_const(op->type, i), bounds);
        } else if (op->type.is_uint() &&
                   const_int(value, &i)) {
            // int -> uint
            return make_const(op->type, safe_numeric_cast<uint64_t>(i));
        } else if (op->type.is_float() &&
                   const_int(value, &i)) {
            // int -> float
            return make_const(op->type, safe_numeric_cast<double>(i));
        } else if (op->type.is_int() &&
                   const_uint(value, &u) &&
                   op->type.bits() < value.type().bits()) {
            // uint -> int
            // Recursively call mutate just to set the bounds
            return mutate(make_const(op->type, safe_numeric_cast<int64_t>(u)), bounds);
        } else if (op->type.is_int() &&
                   const_uint(value, &u) &&
                   op->type.bits() >= value.type().bits()) {
            // uint -> int with less than or equal to the number of bits
            if (op->type.can_represent(u) || op->type.bits() < 32) {
                // Recursively call mutate just to set the bounds
                return mutate(make_const(op->type, safe_numeric_cast<int64_t>(u)), bounds);
            } else {
                return make_signed_integer_overflow(op->type);
            }
        } else if (op->type.is_uint() &&
                   const_uint(value, &u)) {
            // uint -> uint
            return make_const(op->type, u);
        } else if (op->type.is_float() &&
                   const_uint(value, &u)) {
            // uint -> float
            return make_const(op->type, safe_numeric_cast<double>(u));
        } else if (cast &&
                   op->type.code() == cast->type.code() &&
                   op->type.bits() < cast->type.bits()) {
            // If this is a cast of a cast of the same type, where the
            // outer cast is narrower, the inner cast can be
            // eliminated.
            return mutate(Cast::make(op->type, cast->value), bounds);
        } else if (cast &&
                   (op->type.is_int() || op->type.is_uint()) &&
                   (cast->type.is_int() || cast->type.is_uint()) &&
                   op->type.bits() <= cast->type.bits() &&
                   op->type.bits() <= op->value.type().bits()) {
            // If this is a cast between integer types, where the
            // outer cast is narrower than the inner cast and the
            // inner cast's argument, the inner cast can be
            // eliminated. The inner cast is either a sign extend
            // or a zero extend, and the outer cast truncates the extended bits
            return mutate(Cast::make(op->type, cast->value), bounds);
        } else if (broadcast_value) {
            // cast(broadcast(x)) -> broadcast(cast(x))
            return mutate(Broadcast::make(Cast::make(op->type.with_lanes(broadcast_value->value.type().lanes()), broadcast_value->value), broadcast_value->lanes), bounds);
        } else if (ramp_value &&
                   op->type.element_of() == Int(64) &&
                   op->value.type().element_of() == Int(32)) {
            // cast(ramp(a, b, w)) -> ramp(cast(a), cast(b), w)
            return mutate(Ramp::make(Cast::make(op->type.with_lanes(ramp_value->base.type().lanes()),
                                                ramp_value->base),
                                     Cast::make(op->type.with_lanes(ramp_value->stride.type().lanes()),
                                                ramp_value->stride),
                                     ramp_value->lanes),
                          bounds);
        }
    }

    if (value.same_as(op->value)) {
        return op;
    } else {
        return Cast::make(op->type, value);
    }
}

}  // namespace Internal
}  // namespace Halide
