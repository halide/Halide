#ifndef HALIDE_FAST_INTEGER_DIVIDE_H
#define HALIDE_FAST_INTEGER_DIVIDE_H

#include "Buffer.h"
#include "Expr.h"

namespace Halide {

/** Integer division by small values can be done exactly as multiplies
 * and shifts. This function does integer division for numerators of
 * various integer types (8, 16, 32 bit signed and unsigned)
 * numerators and uint8 denominators. The type of the result is the
 * type of the numerator. The unsigned version is faster than the
 * signed version, so cast the numerator to an unsigned int if you
 * know it's positive.
 *
 * If your divisor is compile-time constant, Halide performs a
 * slightly better optimization automatically, so there's no need to
 * use this function (but it won't hurt).
 *
 * This function vectorizes well on arm, and well on x86 for 16 and 8
 * bit vectors. For 32-bit vectors on x86 you're better off using
 * native integer division.
 *
 * Also, this routine treats division by zero as division by
 * 256. I.e. it interprets the uint8 divisor as a number from 1 to 256
 * inclusive.
 */
Expr fast_integer_divide(const Expr &numerator, const Expr &denominator);

/** A variant of the above which rounds towards zero instead of rounding towards
 * negative infinity. */
Expr fast_integer_divide_round_to_zero(const Expr &numerator, const Expr &denominator);

/** Use the fast integer division tables to implement a modulo
 * operation via the Euclidean identity: a%b = a - (a/b)*b
 */
Expr fast_integer_modulo(const Expr &numerator, const Expr &denominator);

}  // namespace Halide

#endif
