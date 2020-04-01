#ifndef HALIDE_EMULATE_FLOAT16_MATH_H
#define HALIDE_EMULATE_FLOAT16_MATH_H

#include "Expr.h"
/** \file
 * Methods for dealing with float16 arithmetic using float32 math, by
 * casting back and forth with bit tricks.
 */

namespace Halide {
namespace Internal {
struct Call;
struct Cast;

/** Check if a call is a float16 transcendental (e.g. sqrt_f16) */
bool is_float16_transcendental(const Call *);

/** Implement a float16 transcendental using the float32 equivalent. */
Expr lower_float16_transcendental_to_float32_equivalent(const Call *);

/** Cast to/from float and bfloat using bitwise math. */
//@{
Expr float32_to_bfloat16(Expr e);
Expr float32_to_float16(Expr e);
Expr float16_to_float32(Expr e);
Expr bfloat16_to_float32(Expr e);
Expr lower_float16_cast(const Cast *op);
//@}

}  // namespace Internal
}  // namespace Halide

#endif
