#include "HalideRuntime.h"
#include "multitarget.h"
#include "halide_image.h"

using namespace Halide::Tools;

#define USE_DEBUG_FEATURE 0

extern "C" int halide_can_use_target_features(uint64_t features) {
    if (features & (1ULL << halide_target_feature_debug)) {
#if USE_DEBUG_FEATURE        
        return 1;
#else
        return 0;
#endif
    }
    return 1;
}

int main(int argc, char **argv) {
    const int W = 32, H = 32;
    Image<uint32_t> output(W, H);

    buffer_t *o_buf = output;
    if (multitarget(o_buf) != 0) {
        printf("Error at multitarget\n");
    }

    // Verify output.
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
#if USE_DEBUG_FEATURE        
            const uint32_t expected = 0xdeadbeef;
#else
            const uint32_t expected = 0xf00dcafe;
#endif
            const uint32_t actual = output(x, y);
            if (actual != expected) {
                printf("Error at %d, %d: expected %x, got %x\n", x, y, expected, actual);
                return -1;
            }
        }
    }
    printf("Success: Saw %x for debug=%d\n", output(0, 0), USE_DEBUG_FEATURE);

    return 0;
}
