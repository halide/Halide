#ifndef HALIDE_FAST_INTEGER_DIVIDE_H
#define HALIDE_FAST_INTEGER_DIVIDE_H

#include "IR.h"
#include "Image.h"

namespace Halide {

/** Built-in images used for fast_integer_divide below. Use of
 * fast_integer_divide will automatically embed the appropriate tables
 * in your object file. They are declared here in case you want to do
 * something non-default with them. */
namespace IntegerDivideTable {
EXPORT Image<uint8_t> integer_divide_table_u8();
EXPORT Image<uint8_t> integer_divide_table_s8();
EXPORT Image<uint16_t> integer_divide_table_u16();
EXPORT Image<uint16_t> integer_divide_table_s16();
EXPORT Image<uint32_t> integer_divide_table_u32();
EXPORT Image<uint32_t> integer_divide_table_s32();
}


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
EXPORT Expr fast_integer_divide(Expr numerator, Expr denominator);

}

#endif
