#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "approximation_codec.h"
#include "approximation_codec_decode.h"

using Halide::Runtime::Buffer;

namespace {

// Must match the generator's default block_size.
constexpr int kBlockSize = 8;
constexpr int kBlocks = 16;
constexpr int kSize = kBlockSize * kBlocks;

}  // namespace

int main(int argc, char **argv) {
    Buffer<float, 1> values(kSize);
    values.for_each_element([&](int k) {
        values(k) = std::cos(k * 0.37f) * (1.0f + (k / kBlockSize));
    });
    // An all-zero block exercises the zero-scale path.
    for (int k = 0; k < kBlockSize; k++) {
        values(k) = 0.0f;
    }

    // Encode: values -> codes (within, block) as offset bytes, scale (block).
    Buffer<uint8_t, 2> codes(kBlockSize, kBlocks);
    Buffer<float, 1> scale(kBlocks);
    if (int result = approximation_codec(values, codes, scale); result != 0) {
        fprintf(stderr, "approximation_codec failed: %d\n", result);
        return 1;
    }

    // Decode: codes, scale -> decoded.
    Buffer<float, 1> decoded(kSize);
    if (int result = approximation_codec_decode(codes, scale, decoded); result != 0) {
        fprintf(stderr, "approximation_codec_decode failed: %d\n", result);
        return 1;
    }

    for (int b = 0; b < kBlocks; b++) {
        float amax = 0.0f;
        for (int i = 0; i < kBlockSize; i++) {
            amax = std::max(amax, std::fabs(values(b * kBlockSize + i)));
        }
        float expected_scale = amax / 127.0f;
        if (scale(b) != expected_scale) {
            fprintf(stderr, "scale(%d) = %f, expected %f\n", b, scale(b), expected_scale);
            return 1;
        }
        float divisor = expected_scale != 0.0f ? expected_scale : 1.0f;
        for (int i = 0; i < kBlockSize; i++) {
            int k = b * kBlockSize + i;
            // Halide's round() is round-half-to-even, like nearbyint() in the
            // default rounding mode.
            int expected_code = (int)std::nearbyint(values(k) / divisor) + 128;
            if (codes(i, b) != expected_code) {
                fprintf(stderr, "codes(%d, %d) = %d, expected %d\n", i, b, codes(i, b), expected_code);
                return 1;
            }
            float expected = (float)(codes(i, b) - 128) * scale(b);
            if (decoded(k) != expected || std::fabs(decoded(k) - values(k)) > expected_scale * 0.5f + 1e-6f) {
                fprintf(stderr, "decoded(%d) = %f, expected %f (value %f)\n", k, decoded(k), expected, values(k));
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
