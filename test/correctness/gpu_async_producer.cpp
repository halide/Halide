#include "Halide.h"
#include <cstdio>

using namespace Halide;

// An async producer inside a GPU block becomes warp specialization: the
// producer and the consumer run at the same time on different threads of the
// block, and the semaphores that sequence them become barriers.
int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    const int W = 256, H = 64, K = 64;

    Var x("x"), y("y"), xo("xo"), xi("xi"), yo("yo"), yi("yi");
    Func producer("producer"), consumer("consumer");
    RDom r(0, K, "r");

    producer(x, y) = cast<float>(x + y);
    consumer(x, y) = 0.f;
    consumer(x, y) += producer(x, r);

    consumer.compute_root()
        .bound(x, 0, W)
        .bound(y, 0, H)
        .gpu_tile(x, y, xi, yi, 32, 8);

    RVar ro("ro"), ri("ri");
    consumer.update()
        .split(x, xo, xi, 32)
        .split(y, yo, yi, 8)
        .split(r, ro, ri, 8)
        .reorder(xi, yi, ri, ro, xo, yo)
        .gpu_blocks(xo, yo)
        .gpu_threads(xi, yi);

    // The producer stages a tile into shared memory for each step of the
    // reduction, one step ahead of the consumer.
    producer.compute_at(consumer, ro)
        .store_in(MemoryType::GPUShared)
        .hoist_storage(consumer, xo)
        .ring_buffer(2)
        .async()
        .gpu_threads(x, y);

    Buffer<float> result(W, H);
    consumer.realize(result);
    result.copy_to_host();

    for (int j = 0; j < H; j++) {
        for (int i = 0; i < W; i++) {
            float ref = 0.f;
            for (int l = 0; l < K; l++) {
                ref += (float)(i + l);
            }
            if (result(i, j) != ref) {
                printf("result(%d, %d) = %f instead of %f\n", i, j, result(i, j), ref);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
