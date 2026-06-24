#ifndef HALIDE_CONCISE_CASTS_H
#define HALIDE_CONCISE_CASTS_H

#include "IROperator.h"

/** \file
 *
 * Defines concise cast and saturating cast operators to make it
 * easier to read cast-heavy code. Think carefully about the
 * readability implications before using these. They could make your
 * code better or worse. Often it's better to add extra Funcs to your
 * pipeline that do the upcasting and downcasting.
 */

namespace Halide {
namespace ConciseCasts {

/** Cast an expression to a 64-bit float.
 * This is a concise alternative to cast(Float(64), e). */
inline Expr f64(Expr e) {
    Type t = Float(64, e.type().lanes());
    return cast(t, std::move(e));
}

/** Cast an expression to a 32-bit float.
 * This is a concise alternative to cast(Float(32), e). */
inline Expr f32(Expr e) {
    Type t = Float(32, e.type().lanes());
    return cast(t, std::move(e));
}

/** Cast an expression to a 16-bit float.
 * This is a concise alternative to cast(Float(16), e). */
inline Expr f16(Expr e) {
    Type t = Float(16, e.type().lanes());
    return cast(t, std::move(e));
}

/** Cast an expression to a 16-bit bfloat.
 * This is a concise alternative to cast(BFloat(16), e). */
inline Expr bf16(Expr e) {
    Type t = BFloat(16, e.type().lanes());
    return cast(t, std::move(e));
}

/** Cast an expression to a 64-bit signed integer.
 * This is a concise alternative to cast(Int(64), e). */
inline Expr i64(Expr e) {
    Type t = Int(64, e.type().lanes());
    return cast(t, std::move(e));
}

/** Cast an expression to a 32-bit signed integer.
 * This is a concise alternative to cast(Int(32), e). */
inline Expr i32(Expr e) {
    Type t = Int(32, e.type().lanes());
    return cast(t, std::move(e));
}

/** Cast an expression to a 16-bit signed integer.
 * This is a concise alternative to cast(Int(16), e). */
inline Expr i16(Expr e) {
    Type t = Int(16, e.type().lanes());
    return cast(t, std::move(e));
}

/** Cast an expression to an 8-bit signed integer.
 * This is a concise alternative to cast(Int(8), e). */
inline Expr i8(Expr e) {
    Type t = Int(8, e.type().lanes());
    return cast(t, std::move(e));
}

/** Cast an expression to a 64-bit unsigned integer.
 * This is a concise alternative to cast(UInt(64), e). */
inline Expr u64(Expr e) {
    Type t = UInt(64, e.type().lanes());
    return cast(t, std::move(e));
}

/** Cast an expression to a 32-bit unsigned integer.
 * This is a concise alternative to cast(UInt(32), e). */
inline Expr u32(Expr e) {
    Type t = UInt(32, e.type().lanes());
    return cast(t, std::move(e));
}

/** Cast an expression to a 16-bit unsigned integer.
 * This is a concise alternative to cast(UInt(16), e). */
inline Expr u16(Expr e) {
    Type t = UInt(16, e.type().lanes());
    return cast(t, std::move(e));
}

/** Cast an expression to an 8-bit unsigned integer.
 * This is a concise alternative to cast(UInt(8), e). */
inline Expr u8(Expr e) {
    Type t = UInt(8, e.type().lanes());
    return cast(t, std::move(e));
}

/** Saturating cast an expression to an 8-bit signed integer.
 * Values that exceed the range of int8 are clamped to the minimum or maximum int8 value.
 * This is a concise alternative to saturating_cast(Int(8), e). */
inline Expr i8_sat(Expr e) {
    Type t = Int(8, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

/** Saturating cast an expression to an 8-bit unsigned integer.
 * Values that exceed the range of uint8 are clamped to 0 or 255.
 * This is a concise alternative to saturating_cast(UInt(8), e). */
inline Expr u8_sat(Expr e) {
    Type t = UInt(8, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

/** Saturating cast an expression to a 16-bit signed integer.
 * Values that exceed the range of int16 are clamped to the minimum or maximum int16 value.
 * This is a concise alternative to saturating_cast(Int(16), e). */
inline Expr i16_sat(Expr e) {
    Type t = Int(16, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

/** Saturating cast an expression to a 16-bit unsigned integer.
 * Values that exceed the range of uint16 are clamped to 0 or 65535.
 * This is a concise alternative to saturating_cast(UInt(16), e). */
inline Expr u16_sat(Expr e) {
    Type t = UInt(16, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

/** Saturating cast an expression to a 32-bit signed integer.
 * Values that exceed the range of int32 are clamped to the minimum or maximum int32 value.
 * This is a concise alternative to saturating_cast(Int(32), e). */
inline Expr i32_sat(Expr e) {
    Type t = Int(32, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

/** Saturating cast an expression to a 32-bit unsigned integer.
 * Values that exceed the range of uint32 are clamped to 0 or 4294967295.
 * This is a concise alternative to saturating_cast(UInt(32), e). */
inline Expr u32_sat(Expr e) {
    Type t = UInt(32, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

/** Saturating cast an expression to a 64-bit signed integer.
 * Values that exceed the range of int64 are clamped to the minimum or maximum int64 value.
 * This is a concise alternative to saturating_cast(Int(64), e). */
inline Expr i64_sat(Expr e) {
    Type t = Int(64, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

/** Saturating cast an expression to a 64-bit unsigned integer.
 * Values that are negative are clamped to 0.
 * This is a concise alternative to saturating_cast(UInt(64), e). */
inline Expr u64_sat(Expr e) {
    Type t = UInt(64, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

};  // namespace ConciseCasts
};  // namespace Halide

#endif
