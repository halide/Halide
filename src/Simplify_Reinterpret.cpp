#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr Simplify::visit(const Reinterpret *op, ExprInfo *bounds) {
    Expr a = mutate(op->value, nullptr);

    int64_t ia;
    uint64_t ua;
    bool vector = op->type.is_vector() || a.type().is_vector();
    if (op->type == a.type()) {
        return a;
    } else if (const_int(a, &ia) && op->type.is_uint() && !vector) {
        // int -> uint
        return make_const(op->type, (uint64_t)ia);
    } else if (const_uint(a, &ua) && op->type.is_int() && !vector) {
        // uint -> int
        return make_const(op->type, (int64_t)ua);
    } else if (a.same_as(op->value)) {
        return op;
    } else {
        return reinterpret(op->type, a);
    }
}

}  // namespace Internal
}  // namespace Halide
