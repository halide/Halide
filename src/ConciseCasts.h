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
    return cast(Int(8, e.type().lanes()), clamp(e, -128, 127));
}

inline Expr u8_sat(Expr e) {
    if (e.type().is_uint()) {
        return cast(UInt(8, e.type().lanes()), min(e, 255));
    } else {
        return cast(UInt(8, e.type().lanes()), clamp(e, 0, 255));
    }
}

inline Expr i16_sat(Expr e) {
    return cast(Int(16, e.type().lanes()), clamp(e, -32768, 32767));
}

inline Expr u16_sat(Expr e) {
    if (e.type().is_uint()) {
        return cast(UInt(16, e.type().lanes()), min(e, 65535));
    } else {
        return cast(UInt(16, e.type().lanes()), clamp(e, 0, 65535));
    }
}

inline Expr i32_sat(Expr e) {
    return cast(Int(32, e.type().lanes()), clamp(e, Int(32).min(), Int(32).max()));
}

inline Expr u32_sat(Expr e) {
    if (e.type().is_uint()) {
        return cast(UInt(32, e.type().lanes()), min(e, UInt(32).max()));
    } else {
        return cast(UInt(32, e.type().lanes()), clamp(e, 0, UInt(32).max()));
    }
}


};
};

#endif
