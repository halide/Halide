#include "Halide.h"
#include "halide_benchmark.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Tools;

Expr expensive(Expr x, int c) {
    if (c <= 0) {
        return x;
    } else {
        return expensive(fast_pow(x, x + 1), c - 1);
    }
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    double times[2];
    for (int use_async = 0; use_async < 2; use_async++) {
        Var x, y, t, xi, yi;

        ImageParam in(Float(32), 3);
        Func cpu, gpu;

        // We have a two-stage pipeline that processes frames. We want
        // to run the first stage on the GPU and the second stage on
        // the CPU. We'd like to get the CPU and GPU running at the
        // same time using async. The amount of math we do here
        // doesn't matter much - the important thing is that we
        // overlap CPU computation with the GPU buffer copies.
        gpu(x, y, t) = expensive(in(x, y, t), 16);
        cpu(x, y, t) = expensive(gpu(x, y, t), 16);

        cpu.parallel(y, 16).vectorize(x, 8);

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

        in.set(Buffer<float>(800, 800, 16));
        Buffer<float> out(800, 800, 16);

        cpu.compile_jit();

        times[use_async] = benchmark(10, 1, [&]() {
            cpu.realize(out);
        });

        printf("%s: %f\n",
               use_async ? "with async" : "without async",
               times[use_async]);
    }

    if (times[1] > 1.2 * times[0]) {
        printf("Using async should have been faster\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
