#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "sever.h"
#include "sever_dequantize.h"

using Halide::Runtime::Buffer;

namespace {

// Must match the generator's default block_size.
constexpr int kBlockSize = 8;
constexpr int kBlocks = 16;
constexpr int kSize = kBlockSize * kBlocks;

}  // namespace

int main(int argc, char **argv) {
    Buffer<float, 1> x(kSize);
    x.for_each_element([&](int k) {
        x(k) = std::cos(k * 0.37f) * (1.0f + (k / kBlockSize));
    });
    // An all-zero block exercises the zero-scale path.
    for (int k = 0; k < kBlockSize; k++) {
        x(k) = 0.0f;
    }

    // Quantize: Input x adopted from a plain ImageParam; Outputs q and scale
    // adopted from sever()'s offline Pipeline.
    Buffer<int8_t, 1> q(kSize);
    Buffer<float, 1> scale(kBlocks);
    if (int result = sever(x, q, scale); result != 0) {
        fprintf(stderr, "sever failed: %d\n", result);
        return 1;
    }

    for (int b = 0; b < kBlocks; b++) {
        float amax = 0.0f;
        for (int i = 0; i < kBlockSize; i++) {
            amax = std::max(amax, std::fabs(x(b * kBlockSize + i)));
        }
        float expected_scale = amax / 127.0f;
        if (scale(b) != expected_scale) {
            fprintf(stderr, "scale(%d) = %f, expected %f\n", b, scale(b), expected_scale);
            return 1;
        }
        float inv = expected_scale != 0.0f ? 1.0f / expected_scale : 0.0f;
        for (int i = 0; i < kBlockSize; i++) {
            int k = b * kBlockSize + i;
            // Halide's round() is round-half-to-even, like nearbyint() in the
            // default rounding mode.
            int expected_q = std::clamp((int)std::nearbyint(x(k) * inv), -127, 127);
            if (q(k) != expected_q) {
                fprintf(stderr, "q(%d) = %d, expected %d\n", k, q(k), expected_q);
                return 1;
            }
        }
    }

    // Dequantize: Inputs adopted from the ImageParams sever() bound
    // the severed Funcs to; Output y adopted from the rewritten online Func.
    Buffer<float, 1> y(kSize);
    if (int result = sever_dequantize(q, scale, y); result != 0) {
        fprintf(stderr, "sever_dequantize failed: %d\n", result);
        return 1;
    }

    for (int k = 0; k < kSize; k++) {
        float expected = (float)q(k) * scale(k / kBlockSize);
        if (y(k) != expected) {
            fprintf(stderr, "y(%d) = %f, expected %f\n", k, y(k), expected);
            return 1;
        }
        // The round trip should also be close to the original input.
        float tolerance = scale(k / kBlockSize) * 0.5f + 1e-6f;
        if (std::fabs(y(k) - x(k)) > tolerance) {
            fprintf(stderr, "y(%d) = %f too far from x(%d) = %f\n", k, y(k), k, x(k));
            return 1;
        }
    }

    // Dequantizing arbitrary data must use it, not recompute from x.
    q.fill(3);
    scale.fill(0.5f);
    if (int result = sever_dequantize(q, scale, y); result != 0) {
        fprintf(stderr, "sever_dequantize failed: %d\n", result);
        return 1;
    }
    for (int k = 0; k < kSize; k++) {
        if (y(k) != 1.5f) {
            fprintf(stderr, "y(%d) = %f with q=3, scale=0.5; expected 1.5\n", k, y(k));
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
