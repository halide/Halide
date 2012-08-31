#include "architecture.posix.stdlib.cpp"

#include <immintrin.h>

extern "C" {

INLINE __m128 sqrt_f32x4(__m128 x) {
    return _mm_sqrt_ps(x);
}

}


