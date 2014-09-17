#ifndef DEINTERLEAVE_H
#define DEINTERLEAVE_H

/** \file
 *
 * Defines methods for splitting up a vector into the even lanes and
 * the odd lanes. Useful for optimizing expressions such as select(x %
 * 2, f(x/2), g(x/2))
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Extract the odd-numbered lanes in a vector */
EXPORT Expr extract_odd_lanes(Expr a);

/** Extract the even-numbered lanes in a vector */
EXPORT Expr extract_even_lanes(Expr a);

/** Extract the nth lane of a vector */
EXPORT Expr extract_lane(Expr vec, int lane);

/** Look through a statement for expressions of the form select(ramp %
 * 2 == 0, a, b) and replace them with calls to an interleave
 * intrinsic */
Stmt rewrite_interleavings(Stmt s);

EXPORT void deinterleave_vector_test();

}
}

#endif
