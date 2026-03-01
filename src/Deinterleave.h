#ifndef DEINTERLEAVE_H
#define DEINTERLEAVE_H

/** \file
 *
 * Defines methods for splitting up a vector into the even lanes and
 * the odd lanes. Useful for optimizing expressions such as select(x %
 * 2, f(x/2), g(x/2))
 */

#include "Expr.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

struct VectorSlice {
    int start, stride, count;
    std::string variable_name;
};

/* Extract lanes and relying on the fact that the caller will provide new variables in Lets or LetStmts which correspond to slices of the original variable. */
Expr extract_lanes(Expr e, int starting_lane, int lane_stride, int new_lanes, const Scope<> &sliceable_lets, Scope<std::vector<VectorSlice>> &requested_sliced_lets);

/* Extract lanes without requesting any extra slices from variables. */
Expr extract_lanes(Expr e, int starting_lane, int lane_stride, int new_lanes);

/** Extract the nth lane of a vector */
Expr extract_lane(const Expr &vec, int lane);

/** Look through a statement for expressions of the form select(ramp %
 * 2 == 0, a, b) and replace them with calls to an interleave
 * intrinsic */
Stmt rewrite_interleavings(const Stmt &s);

void deinterleave_vector_test();

}  // namespace Internal
}  // namespace Halide

#endif
