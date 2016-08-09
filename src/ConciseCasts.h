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

namespace Internal {

inline Expr signed_saturating_cast(Expr e, int bits) {
    if (e.type().element_of() == Int(bits)) {
        return e;
    }
    if (bits <= e.type().bits()) {
        if (e.type().is_uint()) {
            e = min(e, cast(e.type(), Int(bits).max()));
        } else {
            e = clamp(e, Int(bits).min(), Int(bits).max());
        }
    }
    return cast(Int(bits, e.type().lanes()), e);
}

inline Expr unsigned_saturating_cast(Expr e, int bits) {
    if (e.type().element_of() == UInt(bits)) {
        return e;
    }
    if (bits < e.type().bits()) {
        if (e.type().is_uint()) {
            e = min(e, UInt(bits).max());
        } else {
            e = clamp(e, 0, UInt(bits).max());
        }
    } else if (e.type().is_int()) {
        e = max(e, 0);
    }
    return cast(UInt(bits, e.type().lanes()), e);
}

} // namepspace Internal

inline Expr i8_sat(Expr e) {
    return Internal::signed_saturating_cast(e, 8);
}

inline Expr u8_sat(Expr e) {
    return Internal::unsigned_saturating_cast(e, 8);
}

inline Expr i16_sat(Expr e) {
    return Internal::signed_saturating_cast(e, 16);
}

inline Expr u16_sat(Expr e) {
    return Internal::unsigned_saturating_cast(e, 16);
}

inline Expr i32_sat(Expr e) {
    return Internal::signed_saturating_cast(e, 32);
}

inline Expr u32_sat(Expr e) {
    return Internal::unsigned_saturating_cast(e, 32);
}

inline Expr i64_sat(Expr e) {
    return Internal::signed_saturating_cast(e, 64);
}

inline Expr u64_sat(Expr e) {
    return Internal::unsigned_saturating_cast(e, 64);
}

};
};

#endif
