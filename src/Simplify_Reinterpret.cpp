#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Reinterpret *op, ExprInfo *info) {
    Expr a = mutate(op->value, nullptr);

    if (info) {
        // We don't track bounds and such through reinterprets, but we do know
        // things about the result, just based on its type, e.g. if we're
        // reinterpreting to a uint8, it's <= 255.
        info->cast_to(op->type);
    }

    bool vector = op->type.is_vector() || a.type().is_vector();
    if (op->type == a.type()) {
        return a;
    } else if (auto ia = as_const_int(a); ia && op->type.is_uint() && !vector) {
        // int -> uint
        return make_const(op->type, reinterpret_bits<uint64_t>(*ia), info);
    } else if (auto ua = as_const_uint(a); ua && op->type.is_int() && !vector) {
        // uint -> int
        return make_const(op->type, reinterpret_bits<int64_t>(*ua), info);
    } else if (const Reinterpret *as_r = a.as<Reinterpret>()) {
        // Fold double-reinterprets.
        return mutate(reinterpret(op->type, as_r->value), info);
    } else if ((op->type.bits() == a.type().bits()) &&
               op->type.is_int_or_uint() &&
               a.type().is_int_or_uint()) {
        // Normalize to casts for non-lane-changing reinterprets.
        return cast(op->type, a);
    } else if (a.same_as(op->value)) {
        return op;
    } else {
        return reinterpret(op->type, a);
    }
}

}  // namespace Internal
}  // namespace Halide
