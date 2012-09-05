#include "architecture.posix.stdlib.cpp"

#include <immintrin.h>

extern "C" {

INLINE __v16qi abs_i8x16(__v16qi x) {
    return __builtin_ia32_pabsb128(x);
}

INLINE __v8hi abs_i16x8(__v8hi x) {
    return __builtin_ia32_pabsw128(x);
}

INLINE __v4si abs_i32x4(__v4si x) {
    return __builtin_ia32_pabsd128(x);
}

INLINE __m128 sqrt_f32x4(__m128 x) {
    return _mm_sqrt_ps(x);
}

INLINE __m128d sqrt_f64x2(__m128d x) {
    return _mm_sqrt_pd(x);
}

INLINE __m128 floor_f32x4(__m128 x) {
    return _mm_floor_ps(x);
}

INLINE __m128d floor_f64x2(__m128d x) {
    return _mm_floor_pd(x);
}

INLINE __m128 ceil_f32x4(__m128 x) {
    return _mm_ceil_ps(x);
}

INLINE __m128d ceil_f64x2(__m128d x) {
    return _mm_ceil_pd(x);
}

INLINE __m128 round_f32x4(__m128 x) {
    return _mm_round_ps(x, _MM_FROUND_TO_NEAREST_INT);
}

INLINE __m128d round_f64x2(__m128d x) {
    return _mm_round_pd(x, _MM_FROUND_TO_NEAREST_INT);
}

/* TODO: figure out how to selectively enable avx
INLINE __m256 sqrt_f32x8(__m256 x) {
    return _mm256_sqrt_ps(x);
}

INLINE __m256 sqrt_f64x4(__m256 x) {
    return _mm256_sqrt_pd(x);
}
*/

}


