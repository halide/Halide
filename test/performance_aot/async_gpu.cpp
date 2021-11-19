#ifdef HALIDE_COMPILING_GENERATOR

#include "Halide.h"

namespace {

Halide::Expr expensive(Halide::Expr x, int c) {
    if (c <= 0) {
        return x;
    } else {
        return expensive(Halide::fast_pow(x, x + 1), c - 1);
    }
}

class AsyncGpu : public Halide::Generator<AsyncGpu> {
public:
    GeneratorParam<bool> use_async{"use_async", true};
    Input<Buffer<float>> input{"input", 3};
    Output<Buffer<float>> output{"output", 3};

    void generate() {
        Var x, y, t, xi, yi;

        Func cpu, gpu;

        // We have a two-stage pipeline that processes frames. We want
        // to run the first stage on the GPU and the second stage on
        // the CPU. We'd like to get the CPU and GPU running at the
        // same time using async. The amount of math we do here
        // doesn't matter much - the important thing is that we
        // overlap CPU computation with the GPU buffer copies.
        gpu(x, y, t) = expensive(input(x, y, t), 16);
        cpu(x, y, t) = expensive(gpu(x, y, t), 16);

        cpu.parallel(y, 16).vectorize(x, 8);

        if (get_target().has_gpu_feature()) {
            // Assume GPU memory is limited, and compute the GPU stage one
            // frame at a time. Hoist the allocation to the top level.
            gpu.compute_at(cpu, t).store_root().gpu_tile(x, y, xi, yi, 8, 8);

            // Stage the copy-back of the GPU result into a host-side
            // double-buffer.
            gpu.in().copy_to_host().compute_at(cpu, t).store_root().fold_storage(t, 2);

            if (use_async) {
                gpu.in().async();
                gpu.async();
            }
        } else {
            // Just quietly compile (we'll skip usage at runtime)
            gpu.compute_root();
        }

        output = cpu;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(AsyncGpu, with_async)
HALIDE_REGISTER_GENERATOR_ALIAS(without_async, with_async, {{"use_async", "false"}})

#else

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "halide_benchmark.h"
#include "with_async.h"
#include "without_async.h"

#include <stdio.h>

static bool has_gpu_feature(const char *t) {
    return (strstr(t, "cuda") ||
            strstr(t, "opencl") ||
            strstr(t, "metal") ||
            strstr(t, "d3d12compute") ||
            strstr(t, "openglcompute"));
}

int main(int argc, char **argv) {
    const char *const target = with_async_metadata()->target;
    printf("Compiled with target: %s\n", target);

    if (!has_gpu_feature(target)) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    // Issue https://github.com/halide/Halide/issues/3586 -- failing
    // on Windows; disabling pending a fix
    if (strstr(target, "d3d12compute")) {
        printf("[SKIP] D3D12Compute broken; see https://github.com/halide/Halide/issues/3586\n");
        return 0;
    }

    Halide::Runtime::Buffer<float> in(800, 800, 16), out(800, 800, 16);
    in.fill(0);

    double times[2];
    for (int use_async = 0; use_async < 2; use_async++) {
        auto f = use_async ? with_async : without_async;
        times[use_async] = Halide::Tools::benchmark(10, 1, [&]() {
            f(in, out);
        });

        printf("%s: %f\n",
               use_async ? "with async" : "without async",
               times[use_async]);
    }

    if (times[1] > 1.2 * times[0]) {
        printf("Using async should have been faster\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
#endif
