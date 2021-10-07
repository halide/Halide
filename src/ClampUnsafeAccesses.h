#ifndef HALIDE_CLAMPUNSAFEACCESSES_H
#define HALIDE_CLAMPUNSAFEACCESSES_H

/** \file
 * Defines the clamp_unsafe_accesses lowering pass.
 */

#include "Bounds.h"
#include "Expr.h"
#include "Function.h"

namespace Halide::Internal {

/** Inject clamps around func calls h(...) when all the following conditions hold:
 * 1. The call is in an indexing context, such as: f(x) = g(h(x));
 * 2. The FuncValueBounds of h are smaller than those of its type
 * 3. The schedule for f uses RoundUp or ShiftInwards
 * 4. h is not compute_at within f's produce node
 */
Stmt clamp_unsafe_accesses(const Stmt& s, const std::map<std::string, Function> &env, FuncValueBounds &func_bounds);

}

#endif  // HALIDE_CLAMPUNSAFEACCESSES_H
