#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;

// A test that attempts to measure the benefits of keeping weights
// resident in L1 across multiple parallel loop launches that reuse
// the same weights.

int main(int argc, char **argv) {
    const int num_threads = 20;
    const int slice_size = (512 * 1024) / sizeof(float); // 512k of weights per task, if sliced correctly
    Buffer<float> weights(slice_size, num_threads);
    weights.fill(13.0f);

    for (int trial = 1; trial < 2; trial++) {

        Func f;
        Var x, y, z;

        RDom r(0, 4096, 0, 256);
        f(y, z) += sqrt(weights((y * 123 + z * 405 + r.x * 170707 + r.y) % slice_size, y));

        // We're going to launch lots of parallel loops over y. If the
        // thread -> task assignment is consistent, and the tasks stay
        // pinned to cores, we should hit in L1 most of the time.
        if (trial == 0) {
            // Every task uses all the weights. A baseline for worst-case behavior.
            f.update().reorder(r.x, y, z, r.y).parallel(z);
        } else if (trial == 1) {
            // Each task uses one particular slice of the weights, depending on the assignment of threads to tasks
            f.update().reorder(r.x, z, y, r.y).parallel(y);
        } else if (trial == 2) {
            // Each task uses the same single slice of the weights
            f.update().reorder(r.x, z, r.y, y).parallel(y);
        }

        Target target = get_jit_target_from_environment();
        target.set_feature(Target::DisableLLVMLoopVectorize);
        target.set_feature(Target::DisableLLVMLoopUnroll);

        Buffer<float> out(num_threads, num_threads);

        f.realize(out, target);

        double mean = 0, stddev = 0;

        const int iters = 16;
        for (int i = 0; i < iters; i++) {
            const double t = Tools::benchmark(1, 1, [&]() {f.realize(out, target);});
            mean += t;
            stddev += t*t;
        }

        mean /= iters;
        stddev /= iters;

        stddev -= mean * mean;
        stddev = std::sqrt(stddev);

        printf("Mean runtime: %f stddev: %f\n", mean, stddev);

    }


    return 0;
}
