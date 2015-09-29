#include "runtime_internal.h"

#define INLINE inline __attribute__((weak)) __attribute__((used)) __attribute__((always_inline)) __attribute__((nothrow)) __attribute__((pure))

extern "C" {

INLINE float float_from_bits(uint32_t bits) {
    union {
        uint32_t as_uint;
        float as_float;
    } u;
    u.as_uint = bits;
    return u.as_float;
}

INLINE float nan_f32() {
    return float_from_bits(0x7fc00000);
}

INLINE float neg_inf_f32() {
    return float_from_bits(0xff800000);
}

INLINE float inf_f32() {
    return float_from_bits(0x7f800000);
}

INLINE float maxval_f32() {
    return float_from_bits(0x7f7fffff);
}

INLINE float minval_f32() {
    return float_from_bits(0xff7fffff);
}

}
