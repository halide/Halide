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
    Type t = Float(64, e.type().lanes());
    return cast(t, std::move(e));
}

inline Expr f32(Expr e) {
    Type t = Float(32, e.type().lanes());
    return cast(t, std::move(e));
}

inline Expr i64(Expr e) {
    Type t = Int(64, e.type().lanes());
    return cast(t, std::move(e));
}

inline Expr i32(Expr e) {
    Type t = Int(32, e.type().lanes());
    return cast(t, std::move(e));
}

inline Expr i16(Expr e) {
    Type t = Int(16, e.type().lanes());
    return cast(t, std::move(e));
}

inline Expr i8(Expr e) {
    Type t = Int(8, e.type().lanes());
    return cast(t, std::move(e));
}

inline Expr u64(Expr e) {
    Type t = UInt(64, e.type().lanes());
    return cast(t, std::move(e));
}

inline Expr u32(Expr e) {
    Type t = UInt(32, e.type().lanes());
    return cast(t, std::move(e));
}

inline Expr u16(Expr e) {
    Type t = UInt(16, e.type().lanes());
    return cast(t, std::move(e));
}

inline Expr u8(Expr e) {
    Type t = UInt(8, e.type().lanes());
    return cast(t, std::move(e));
}

inline Expr i8_sat(Expr e) {
    Type t = Int(8, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

inline Expr u8_sat(Expr e) {
    Type t = UInt(8, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

inline Expr i16_sat(Expr e) {
    Type t = Int(16, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

inline Expr u16_sat(Expr e) {
    Type t = UInt(16, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

inline Expr i32_sat(Expr e) {
    Type t = Int(32, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

inline Expr u32_sat(Expr e) {
    Type t = UInt(32, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

inline Expr i64_sat(Expr e) {
    Type t = Int(64, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

inline Expr u64_sat(Expr e) {
    Type t = UInt(64, e.type().lanes());
    return saturating_cast(t, std::move(e));
}

};  // namespace ConciseCasts
};  // namespace Halide

#endif
