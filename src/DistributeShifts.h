#ifndef HALIDE_DISTRIBUTE_SHIFTS_H
#define HALIDE_DISTRIBUTE_SHIFTS_H

/** \file
 * A tool to distribute shifts as multiplies, useful for some backends. (e.g. ARM, HVX).
 */

#include "IR.h"

namespace Halide {
namespace Internal {

// Distributes shifts as multiplies. If `multiply_adds` is set,
// then only distributes the patterns `a + widening_shl(b, c)` /
// `a - widening_shl(b, c)` and `a + b << c` / `a - b << c`, to
// produce `a (+/-) widening_mul(b, 1 << c)` and `a (+/-) b * (1 << c)`,
// respectively
Stmt distribute_shifts(const Stmt &stmt, bool multiply_adds);

}  // namespace Internal
}  // namespace Halide

#endif
