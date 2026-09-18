#pragma once

// Deterministic synthetic data + aligned buffers, following the conventions
// of tests/test-quantize-perf.cpp (same generator, same rationale: a fixed
// seedless formula so every implementation under comparison sees byte-identical
// input without carrying a PRNG dependency).

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

inline void generate_synthetic_data(float *dst, size_t n, float offset = 0.0f) {
    for (size_t i = 0; i < n; ++i) {
        dst[i] = 0.1f + 2.0f * cosf(static_cast<float>(i) + offset);
    }
}

// 64-byte aligned heap buffer (covers every SIMD width in use: SSE/AVX/AVX-512/NEON/SVE).
class AlignedBuffer {
public:
    explicit AlignedBuffer(size_t bytes) : size_(bytes) {
        constexpr size_t alignment = 64;
        size_t padded = (bytes + alignment - 1) / alignment * alignment;
        if (padded == 0) {
            padded = alignment;
        }
        ptr_ = nullptr;
        posix_memalign(&ptr_, alignment, padded);
    }
    ~AlignedBuffer() {
        std::free(ptr_);
    }

    AlignedBuffer(const AlignedBuffer &) = delete;
    AlignedBuffer &operator=(const AlignedBuffer &) = delete;

    void *data() {
        return ptr_;
    }
    const void *data() const {
        return ptr_;
    }
    template<typename T>
    T *as() {
        return static_cast<T *>(ptr_);
    }
    template<typename T>
    const T *as() const {
        return static_cast<const T *>(ptr_);
    }
    size_t size() const {
        return size_;
    }

private:
    void *ptr_;
    size_t size_;
};
