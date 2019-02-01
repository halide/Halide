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

inline Expr f64(Expr e) {
    return cast(Float(64, e.type().lanes()), e);
}

inline Expr f32(Expr e) {
    return cast(Float(32, e.type().lanes()), e);
}

inline Expr i64(Expr e) {
    return cast(Int(64, e.type().lanes()), e);
}

inline Expr i32(Expr e) {
    return cast(Int(32, e.type().lanes()), e);
}

inline Expr i16(Expr e) {
    return cast(Int(16, e.type().lanes()), e);
}

inline Expr i8(Expr e) {
    return cast(Int(8, e.type().lanes()), e);
}

inline Expr u64(Expr e) {
    return cast(UInt(64, e.type().lanes()), e);
}

inline Expr u32(Expr e) {
    return cast(UInt(32, e.type().lanes()), e);
}

inline Expr u16(Expr e) {
    return cast(UInt(16, e.type().lanes()), e);
}

inline Expr u8(Expr e) {
    return cast(UInt(8, e.type().lanes()), e);
}

inline Expr i8_sat(Expr e) {
    return saturating_cast(Int(8, e.type().lanes()), e);
}

inline Expr u8_sat(Expr e) {
    return saturating_cast(UInt(8, e.type().lanes()), e);
}

inline Expr i16_sat(Expr e) {
    return saturating_cast(Int(16, e.type().lanes()), e);
}

inline Expr u16_sat(Expr e) {
    return saturating_cast(UInt(16, e.type().lanes()), e);
}

inline Expr i32_sat(Expr e) {
    return saturating_cast(Int(32, e.type().lanes()), e);
}

inline Expr u32_sat(Expr e) {
    return saturating_cast(UInt(32, e.type().lanes()), e);
}

inline Expr i64_sat(Expr e) {
    return saturating_cast(Int(64, e.type().lanes()), e);
}

inline Expr u64_sat(Expr e) {
    return saturating_cast(UInt(64, e.type().lanes()), e);
}

};  // namespace ConciseCasts
};  // namespace Halide

#endif
