#include "Simplify_Internal.h"

#include "IRPrinter.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Cast *op, ExprInfo *info) {

    ExprInfo value_info;
    Expr value = mutate(op->value, &value_info);

    if (info && no_overflow(op->type) && !op->type.can_represent(value_info.bounds)) {
        // If there's overflow in a no-overflow type (e.g. due to casting
        // from a UInt(64) to an Int(32)), then forget everything we know
        // about the Expr. The expression may or may not overflow. We don't
        // know.
        *info = ExprInfo{};
    } else {
        value_info.cast_to(op->type);
        value_info.trim_bounds_using_alignment();
        if (info) {
            *info = value_info;
        }
        // It's possible we just reduced to a constant. E.g. if we cast an
        // even number to uint1 we get zero.
        if (value_info.bounds.is_single_point()) {
            return make_const(op->type, value_info.bounds.min, nullptr);
        }
    }

    const Cast *cast = value.as<Cast>();
    const Broadcast *broadcast_value = value.as<Broadcast>();
    const Ramp *ramp_value = value.as<Ramp>();
    std::optional<double> f;
    std::optional<int64_t> i;
    std::optional<uint64_t> u;
    if (Call::as_intrinsic(value, {Call::signed_integer_overflow})) {
        clear_expr_info(info);
        return make_signed_integer_overflow(op->type);
    } else if (value.type() == op->type) {
        if (info) {
            *info = value_info;
        }
        return value;
    } else if (op->type.is_int() &&
               (f = as_const_float(value)) &&
               std::isfinite(*f)) {
        // float -> int
        return make_const(op->type, safe_numeric_cast<int64_t>(*f), info);
    } else if (op->type.is_uint() &&
               (f = as_const_float(value)) &&
               std::isfinite(*f)) {
        // float -> uint
        return make_const(op->type, safe_numeric_cast<uint64_t>(*f), info);
    } else if (op->type.is_float() &&
               (f = as_const_float(value))) {
        // float -> float
        return make_const(op->type, *f, info);
    } else if (op->type.is_int() &&
               (i = as_const_int(value))) {
        // int -> int
        return make_const(op->type, *i, info);
    } else if (op->type.is_uint() &&
               (i = as_const_int(value))) {
        // int -> uint
        return make_const(op->type, safe_numeric_cast<uint64_t>(*i), info);
    } else if (op->type.is_float() &&
               (i = as_const_int(value))) {
        // int -> float
        return make_const(op->type, safe_numeric_cast<double>(*i), info);
    } else if (op->type.is_int() &&
               (u = as_const_uint(value))) {
        // uint -> int.
        return make_const(op->type, safe_numeric_cast<int64_t>(*u), info);
    } else if (op->type.is_uint() &&
               (u = as_const_uint(value))) {
        // uint -> uint
        return make_const(op->type, *u, info);
    } else if (op->type.is_float() &&
               (u = as_const_uint(value))) {
        // uint -> float
        return make_const(op->type, safe_numeric_cast<double>(*u), info);
    } else if (cast &&
               op->type.code() == cast->type.code() &&
               op->type.bits() < cast->type.bits()) {
        // If this is a cast of a cast of the same type, where the
        // outer cast is narrower, the inner cast can be
        // eliminated.
        return mutate(Cast::make(op->type, cast->value), info);
    } else if (cast &&
               op->type.is_int_or_uint() &&
               cast->type.is_int() &&
               cast->value.type().is_int() &&
               op->type.bits() >= cast->type.bits() &&
               cast->type.bits() >= cast->value.type().bits()) {
        // Casting from a signed type always sign-extends, so widening
        // partway to a signed type and the rest of the way to some other
        // integer type is the same as just widening to that integer type
        // directly.
        return mutate(Cast::make(op->type, cast->value), info);
    } else if (cast &&
               op->type.is_int_or_uint() &&
               cast->type.is_int_or_uint() &&
               op->type.bits() <= cast->type.bits() &&
               op->type.bits() <= op->value.type().bits()) {
        // If this is a cast between integer types, where the
        // outer cast is narrower than the inner cast and the
        // inner cast's argument, the inner cast can be
        // eliminated. The inner cast is either a sign extend
        // or a zero extend, and the outer cast truncates the extended bits
        if (op->type == cast->value.type()) {
            return mutate(cast->value, info);
        } else {
            return mutate(Cast::make(op->type, cast->value), info);
        }
    } else if (broadcast_value) {
        // cast(broadcast(x)) -> broadcast(cast(x))
        return mutate(Broadcast::make(Cast::make(op->type.with_lanes(broadcast_value->value.type().lanes()), broadcast_value->value), broadcast_value->lanes), info);
    } else if (ramp_value &&
               op->type.element_of() == Int(64) &&
               op->value.type().element_of() == Int(32)) {
        // cast(ramp(a, b, w)) -> ramp(cast(a), cast(b), w)
        return mutate(Ramp::make(Cast::make(op->type.with_lanes(ramp_value->base.type().lanes()),
                                            ramp_value->base),
                                 Cast::make(op->type.with_lanes(ramp_value->stride.type().lanes()),
                                            ramp_value->stride),
                                 ramp_value->lanes),
                      info);
    }

    if (value.same_as(op->value)) {
        return op;
    } else {
        return Cast::make(op->type, value);
    }
}

}  // namespace Internal
}  // namespace Halide
