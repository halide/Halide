#include "Halide.h"
#include "halide_benchmark.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Tools;

Expr expensive(Expr x, int c) {
    if (c <= 0) {
        return x;
    } else {
        return expensive(x * (x + 1), c - 1);
    }
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    if (target.has_feature(Target::Vulkan)) {
        printf("[SKIP] Skipping test for Vulkan. Async performance needs to be improved before this test will pass.\n");
        return 0;
    }

    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    struct Variant {
        Func cpu;
        Buffer<uint32_t> out;
    };

    auto build = [&](bool use_async) {
        Var x, y, t, xi, yi;

        ImageParam in(UInt(32), 3);
        Func cpu("cpu"), gpu("gpu");

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
        gpu.compute_at(gpu.in(), Var::outermost()).store_root().gpu_tile(x, y, xi, yi, 8, 8);

        // Stage the copy-back of the GPU result into a host-side
        // double-buffer.
        gpu.in().copy_to_host().compute_at(cpu, t).store_root().fold_storage(t, 2);

        if (use_async) {
            // gpu.async();
            gpu.in().async();
        }

        Buffer<uint32_t> in_buf(800, 800, 16);
        in_buf.fill(17);
        in.set(in_buf);

        Variant v;
        v.out = Buffer<uint32_t>(800, 800, 16);
        v.cpu = cpu;
        cpu.compile_jit();
        return v;
    };

    Variant without_async = build(false);
    Variant with_async = build(true);

    auto [r_without, r_with] = benchmark_comparison(
        BenchmarkConfig{},
        [&]() { without_async.cpu.realize(without_async.out); },
        [&]() { with_async.cpu.realize(with_async.out); });

    double times[2] = {r_without.wall_time, r_with.wall_time};

    uint32_t correct = without_async.out(0, 0, 0);
    for (int t = 0; t < with_async.out.dim(2).extent(); t++) {
        for (int y = 0; y < with_async.out.dim(1).extent(); y++) {
            for (int x = 0; x < with_async.out.dim(0).extent(); x++) {
                if (with_async.out(x, y, t) != correct) {
                    printf("Async output at (%d, %d, %d) is %u instead of %u\n",
                           x, y, t, with_async.out(x, y, t), correct);
                    return 1;
                }
            }
        }
    }

    printf("without async: %f\n", times[0]);
    printf("with async: %f\n", times[1]);

    if (times[1] > 1.2 * times[0]) {
        printf("Using async should have been faster\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
