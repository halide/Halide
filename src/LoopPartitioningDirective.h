#ifndef HALIDE_LOOP_PARTITIONING_DIRECTIVE_H
#define HALIDE_LOOP_PARTITIONING_DIRECTIVE_H

/** \file
 * Defines the Partition enum.
 */

#include <string>

#include "Expr.h"
#include "Parameter.h"

namespace Halide {

/** Different ways to handle loops with a potentially optimizable boundary conditions. */
enum class Partition {
    /** Automatically let Halide decide on Loop Parititioning. */
    Auto,

    /** Disallow loop partitioning. */
    Never,

    /** Force partitioning of the loop. If Halide can't find a way to partition this loop,
     * it will raise an error. */
    Always
};

}  // namespace Halide

#endif  // HALIDE_LOOP_PARTITIONING_DIRECTIVE_H
