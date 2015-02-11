#include "runtime_internal.h"

#define INLINE inline __attribute__((weak)) __attribute__((used)) __attribute__((always_inline)) __attribute__((nothrow)) __attribute__((pure))

extern "C" {

INLINE uint8_t maxval_u8() {return 0xff;}
INLINE uint8_t minval_u8() {return 0;}
INLINE uint16_t maxval_u16() {return 0xffff;}
INLINE uint16_t minval_u16() {return 0;}
INLINE uint32_t maxval_u32() {return 0xffffffff;}
INLINE uint32_t minval_u32() {return 0;}
INLINE uint64_t maxval_u64() {return UINT64_C(0xffffffffffffffff);}
INLINE uint64_t minval_u64() {return 0;}
INLINE int8_t maxval_s8() {return 0x7f;}
INLINE int8_t minval_s8() {return 0x80;}
INLINE int16_t maxval_s16() {return 0x7fff;}
INLINE int16_t minval_s16() {return 0x8000;}
INLINE int32_t maxval_s32() {return 0x7fffffff;}
INLINE int32_t minval_s32() {return 0x80000000;}
INLINE int64_t maxval_s64() {return INT64_C(0x7fffffffffffffff);}
INLINE int64_t minval_s64() {return INT64_C(0x8000000000000000);}

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

INLINE double double_from_bits(uint64_t bits) {
    union {
        uint64_t as_uint;
        double as_double;
    } u;
    u.as_uint = bits;
    return u.as_double;
}

INLINE double maxval_f64() {
    return double_from_bits(UINT64_C(0x7fefffffffffffff));
}

INLINE double minval_f64() {
    return double_from_bits(UINT64_C(0xffefffffffffffff));
}

}
