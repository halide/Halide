#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Relative-error comparison for floating point outputs (dequantize, vec_dot,
// gemv/gemm results). Quantize/quantize_mat outputs are compared with an
// exact memcmp instead (see bench_quantize.cpp) since those algorithms are
// specified to be bit-identical across implementations.
inline bool floats_match(const float *a, const float *b, int64_t n, float rel_tol = 1e-2f) {
    for (int64_t i = 0; i < n; ++i) {
        const float diff = std::fabs(a[i] - b[i]);
        const float scale = std::max(std::fabs(a[i]), 1e-6f);
        if (diff / scale > rel_tol) {
            return false;
        }
    }
    return true;
}
