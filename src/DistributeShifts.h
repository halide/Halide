#ifndef HALIDE_DISTRIBUTE_SHIFTS_H
#define HALIDE_DISTRIBUTE_SHIFTS_H

/** \file
 * A tool to distribute shifts as multiplies, useful for some backends. (e.g. ARM, HVX).
 */

#include "IR.h"

namespace Halide {
namespace Internal {

// Distributes shifts as multiplies. If `polynomials_only` is set,
// then only distributes the patterns `a + widening_shl(b, c)` /
// `a - widening_shl(b, c)` and `a + b << c` / `a - b << c`.
Stmt distribute_shifts(const Stmt &stmt, const bool polynomials_only = false);

}  // namespace Internal
}  // namespace Halide

#endif
