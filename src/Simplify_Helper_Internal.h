#ifndef HALIDE_SIMPLIFY_HELPER_INTERNAL_H
#define HALIDE_SIMPLIFY_HELPER_INTERNAL_H

#include "Expr.h"
#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

Expr ramp(const Expr &base, const Expr &stride, int lanes);

Expr broadcast(const Expr &value, int lanes);
Expr broadcast(const Expr &value, const Expr &lanes);

Expr fold(const Expr &expr, Simplify *simplify);
Expr fold(bool value, Simplify *simplify);
bool evaluate_predicate(const Expr &expr);
Expr _can_prove(Simplify *simplifier, const Expr &expr);
bool _is_const(const Expr &expr);

} // namespace Internal
} // namespace Halide

#endif  // HALIDE_SIMPLIFY_HELPER_INTERNAL_H
