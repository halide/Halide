#include <vector>

#include "halide_benchmark.h"
#include <Halide.h>

using namespace Halide;
using namespace Halide::Tools;

double test(const Target &target, std::vector<int> order, std::vector<int> shape) {
    Buffer<float> buf(shape, order);
    for (int t = 0; t < shape[3]; t++) {
        for (int c = 0; c < shape[2]; c++) {
            for (int y = 0; y < shape[1]; y++) {
                for (int x = 0; x < shape[0]; x++) {
                    buf(x, y, c, t) = x * .5f + y * 2.f + c * 4.f + t * .8f;
                }
            }
        }
    }

    buf.set_host_dirty();

    buf.device_malloc();

    double time = benchmark([&]() {
        buf.set_host_dirty();
        buf.copy_to_device(target);
        buf.device_sync();
    });

    // Nuke the host side data so we can check the data transferred back and forth OK.
    buf.set_device_dirty(false);
    buf.fill(0.f);
    buf.set_host_dirty(false);
    buf.set_device_dirty(true);
    buf.copy_to_host();

    for (int t = 0; t < shape[3]; t++) {
        for (int c = 0; c < shape[2]; c++) {
            for (int y = 0; y < shape[1]; y++) {
                for (int x = 0; x < shape[0]; x++) {
                    float correct = x * .5f + y * 2.f + c * 4.f + t * .8f;
                    if (buf(x, y, c, t) != correct) {
                        printf("buf(%d, %d, %d, %d) = %f instead of %f\n",
                               x, y, c, t, buf(x, y, c, t), correct);
                        exit(-1);
                    }
                }
            }
        }
    }

    return time;
}

int main() {
    auto target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("This test requires a GPU target.\n");
        return 0;
    }

    // These copies are all the same size and dense, but are in different memory
    // orderings and some of them have some extent=1 dimensions. (See
    // https://github.com/halide/Halide/issues/8956)

    double t1 = test(target, {3, 2, 0, 1}, {1024, 1024, 3, 2});
    double t2 = test(target, {3, 2, 0, 1}, {1024, 1024, 6, 1});
    double t3 = test(target, {0, 1, 2, 3}, {1024, 1024, 3, 2});
    double t4 = test(target, {0, 1, 2, 3}, {1024, 1024, 6, 1});

    double slowest = std::max({t1, t2, t3, t4});
    double fastest = std::min({t1, t2, t3, t4});

    printf("Timings: %f %f %f %f\n", t1, t2, t3, t4);

    // If one of these gets broken into a large number of separate copies, it
    // will be a lot more than 10x slower.
    if (slowest > 10 * fastest) {
        printf("Suspiciously large variation in timings for what should "
               "be a dense host->device copy.\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
