#include "Halide.h"
#include <cstdio>

using namespace Halide;

// A Func stored in GPUSharedAsync is staged into shared memory by the copy
// engine rather than by loading it into registers and storing it back out.
int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::CUDA)) {
        printf("[SKIP] No CUDA target enabled.\n");
        return 0;
    }
    if (target.get_cuda_capability_lower_bound() < 80) {
        printf("[SKIP] Asynchronous copies need compute capability 8.0 or above.\n");
        return 0;
    }

    const int W = 256, H = 64;

    Buffer<float> input(W, H);
    input.fill([](int x, int y) { return (float)(x + y * 3); });

    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func stage("stage"), out("out");

    // The staged Func has to be a plain copy - the hardware moves the bytes
    // untouched - so any arithmetic goes in the consumer.
    stage(x, y) = input(x, y);
    out(x, y) = stage(x, y) * 2.f + 1.f;

    out.gpu_tile(x, y, xi, yi, 64, 8);
    stage.compute_at(out, x)
        .store_in(MemoryType::GPUSharedAsync)
        .gpu_threads(y)
        .vectorize(x, 4);

    Buffer<float> result(W, H);
    out.realize(result);
    result.copy_to_host();

    for (int j = 0; j < H; j++) {
        for (int i = 0; i < W; i++) {
            const float correct = (float)(i + j * 3) * 2.f + 1.f;
            if (result(i, j) != correct) {
                printf("result(%d, %d) = %f instead of %f\n",
                       i, j, result(i, j), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
