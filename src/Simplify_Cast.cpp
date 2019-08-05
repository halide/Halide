#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Cast *op, ExprInfo *bounds) {
    // We generally don't track bounds through casts, with the
    // exception of casts that constant-fold to a signed integer, so
    // we don't need the bounds of the value.
    Expr value = mutate(op->value, nullptr);

    if (may_simplify(op->type) && may_simplify(op->value.type())) {
        const Call *call = value.as<Call>();
        const Cast *cast = value.as<Cast>();
        const Broadcast *broadcast_value = value.as<Broadcast>();
        const Ramp *ramp_value = value.as<Ramp>();
        double f = 0.0;
        int64_t i = 0;
        uint64_t u = 0;
        if (call && (call->is_intrinsic(Call::indeterminate_expression) ||
                     call->is_intrinsic(Call::signed_integer_overflow))) {
            if (call->is_intrinsic(Call::indeterminate_expression)) {
                return make_indeterminate_expression(op->type);
            } else {
                return make_signed_integer_overflow(op->type);
            }
        } else if (value.type() == op->type) {
            return value;
        } else if (op->type.is_int() &&
                   const_float(value, &f) &&
                   std::isfinite(f)) {
            // float -> int
            // Recursively call mutate just to set the bounds
            return mutate(IntImm::make(op->type, safe_numeric_cast<int64_t>(f)), bounds);
        } else if (op->type.is_uint() &&
                   const_float(value, &f) &&
                   std::isfinite(f)) {
            // float -> uint
            return UIntImm::make(op->type, safe_numeric_cast<uint64_t>(f));
        } else if (op->type.is_float() &&
                   const_float(value, &f)) {
            // float -> float
            return FloatImm::make(op->type, f);
        } else if (op->type.is_int() &&
                   const_int(value, &i)) {
            // int -> int
            // Recursively call mutate just to set the bounds
            return mutate(IntImm::make(op->type, i), bounds);
        } else if (op->type.is_uint() &&
                   const_int(value, &i)) {
            // int -> uint
            return UIntImm::make(op->type, safe_numeric_cast<uint64_t>(i));
        } else if (op->type.is_float() &&
                   const_int(value, &i)) {
            // int -> float
            return FloatImm::make(op->type, safe_numeric_cast<double>(i));
        } else if (op->type.is_int() &&
                   const_uint(value, &u)) {
            // uint -> int
            // Recursively call mutate just to set the bounds
            return mutate(IntImm::make(op->type, safe_numeric_cast<int64_t>(u)), bounds);
        } else if (op->type.is_uint() &&
                   const_uint(value, &u)) {
            // uint -> uint
            return UIntImm::make(op->type, u);
        } else if (op->type.is_float() &&
                   const_uint(value, &u)) {
            // uint -> float
            return FloatImm::make(op->type, safe_numeric_cast<double>(u));
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
            return mutate(Broadcast::make(Cast::make(op->type.element_of(), broadcast_value->value), broadcast_value->lanes), bounds);
        } else if (ramp_value &&
                   op->type.element_of() == Int(64) &&
                   op->value.type().element_of() == Int(32)) {
            // cast(ramp(a, b, w)) -> ramp(cast(a), cast(b), w)
            return mutate(Ramp::make(Cast::make(op->type.element_of(), ramp_value->base),
                                     Cast::make(op->type.element_of(), ramp_value->stride),
                                     ramp_value->lanes), bounds);
        }
    }

    if (value.same_as(op->value)) {
        return op;
    } else {
        return Cast::make(op->type, value);
    }
}

}
}
