#ifndef OPTIMIZE_SHUFFLES_H
#define OPTIMIZE_SHUFFLES_H

/** \file
 * Defines a lowering pass that replace indirect
 * loads with dynamic_shuffle intrinsics where possible.
 */

#include "Expr.h"
#include <functional>
#include <vector>

namespace Halide {
namespace Internal {

/* Replace indirect loads with dynamic_shuffle intrinsics where
possible. */
Stmt optimize_shuffles(Stmt s,
                       int lut_alignment,
                       int native_vector_bits,
                       std::function<std::vector<int>(const Type &)> get_max_span_sizes,
                       bool align_loads_with_native_vector);

}  // namespace Internal
}  // namespace Halide

#endif
