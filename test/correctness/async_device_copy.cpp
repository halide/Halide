#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;

// Return zero, slowly
Expr expensive_zero(Expr x, Expr y, Expr t, int n) {
    // Count how many Fermat's last theorem counterexamples we can find using n trials.
    RDom r(0, n);
    Func a, b, c;
    Var z;
    a(x, y, t, z) = random_int() % 1024 + 5;
    b(x, y, t, z) = random_int() % 1024 + 5;
    c(x, y, t, z) = random_int() % 1024 + 5;
    return sum(select(pow(a(x, y, t, r), 3) + pow(b(x, y, t, r), 3) == pow(c(x, y, t, r), 3), 1, 0));
}

int main(int argc, char **argv) {

    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    if (get_jit_target_from_environment().has_feature(Target::OpenGLCompute)) {
        printf("Skipping test for OpenGLCompute as it does not support copy_to_host/device() yet"
               " (halide_buffer_copy is unimplemented in that backend).\n");
        return 0;
    }

    // Compute frames on GPU/CPU, and then sum then on
    // CPU/GPU. async() lets us overlap the CPU computation with the
    // copies.
    Var x, y, t, xo, yo;
    const int N = 16;
    RDom r(0, N);

    for (int i = 0; i < 4; i++) {
        Func frames;
        frames(x, y, t) = expensive_zero(x, y, t, 1) + ((x + y) % 8) + t;

        Func avg;
        avg(x, y) += frames(x, y, r);

        if (i == 0) {
            // Synchronously GPU -> CPU
            avg.compute_root().update().reorder(x, y, r).vectorize(x, 8);
            frames.store_root().compute_at(frames.in(), Var::outermost()).gpu_tile(x, y, xo, yo, x, y, 16, 16);
            frames.in().store_root().compute_at(avg, r).copy_to_host();
        } else if (i == 1) {
            // Asynchronously GPU -> CPU, via a double-buffer
            avg.compute_root().update().reorder(x, y, r).vectorize(x, 8);
            frames.store_root().compute_at(frames.in(), Var::outermost()).gpu_tile(x, y, xo, yo, x, y, 16, 16);
            frames.in().store_root().compute_at(avg, r).copy_to_host().fold_storage(t, 2).async();
        } else if (i == 2) {
            // Synchronously CPU -> GPU
            avg.compute_root().gpu_tile(x, y, xo, yo, x, y, 16, 16).update().reorder(x, y, r).gpu_tile(x, y, xo, yo, x, y, 16, 16);

            frames.store_root().compute_at(avg, r).vectorize(x, 8).fold_storage(t, 2).parallel(y);

            frames.in().store_root().compute_at(avg, r).copy_to_device();
        } else if (i == 3) {
            // Asynchronously CPU -> GPU, via a double-buffer
            avg.compute_root().gpu_tile(x, y, xo, yo, x, y, 16, 16).update().reorder(x, y, r).gpu_tile(x, y, xo, yo, x, y, 16, 16);

            frames.store_root().compute_at(avg, r).vectorize(x, 8).fold_storage(t, 2).async().parallel(y);

            frames.in().store_root().compute_at(avg, r).copy_to_device();
        }

        Buffer<int> out = avg.realize({1024, 1024});

        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                int correct = ((x + y) % 8) * N + (N * (N - 1)) / 2;
                int actual = out(x, y);
                if (correct != actual) {
                    printf("out(%d, %d) = %d instead of %d\n",
                           x, y, actual, correct);
                    return 1;
                }
            }
        }

        // Report a benchmark, but don't assert anything about it. Not
        // sure how to tune the relative cost of the two stages to
        // make the async version reliably better than the non-async
        // version.
        double t = Halide::Tools::benchmark(3, 3, [&]() {avg.realize(out); out.device_sync(); });
        printf("Case %d: %f\n", i, t);
    }

    printf("Success!\n");
    return 0;
}
