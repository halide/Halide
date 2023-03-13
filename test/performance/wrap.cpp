#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

/* Both 'build' and 'build_wrap' run the same stencil algorithm, albeit with different
 * schedules. 'build(true)' stages the input data (the compute_root() 'host' Func) into
 * the GPU shared memory in tiles before being used for the stencil computation.
 * 'build(false)', on the other hand, forgoes the staging of the input data into the
 * GPU shared memory; the data is loaded per compute. To do the staging, we need to
 * create a dummy Func 'staged', and have 'staged' computed as needed per GPU tile,
 * which loads the input data from 'host' into the GPU shared memory.
 *
 * 'build_wrap' run on the same schedule as 'build(true)', however, instead of
 * creating a dummy Func to stage the input data from 'host', we take advantage
 * of the 'in()' scheduling directive. Calling 'host.in()' returns a global
 * wrapper Func for 'host', which then can be scheduled as appropriate. The global
 * wrapper is essentialy the same as the dummy Func 'staged' in 'build(true)'.
 * The 'in()' scheduling directive provides an easy way to schedule one Func in
 * different ways.
 */

Func build(bool use_shared) {
    Func host;
    Var x, y;
    host(x, y) = x + y;
    host.compute_root();

    // We'll either inline this (and hopefully use the GPU's L1 cache)
    // or stage it into shared.
    Func staged;
    staged(x, y) = host(x, y);

    // Now we just need to access the Func staged a bunch.
    const int stages = 10;
    Func f[stages];
    for (int i = 0; i < stages; i++) {
        Expr prev = (i == 0) ? Expr(0) : Expr(f[i - 1](x, y));
        Expr stencil = 0;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                stencil += staged(select(prev > 0, x, x + dx),
                                  select(prev > 0, y, y + dy));
            }
        }
        if (i == 0) {
            f[i](x, y) = stencil;
        } else {
            f[i](x, y) = f[i - 1](x, y) + stencil;
        }
    }

    Func final = f[stages - 1];

    Var xo, yo, xi, yi;
    final.compute_root().gpu_tile(x, y, xo, yo, xi, yi, 8, 8);
    for (int i = 0; i < stages - 1; i++) {
        f[i].compute_at(final, xo).gpu_threads(x, y);
    }

    if (use_shared) {
        // If we allow staged to use one thread per value loaded, then
        // it forces up the total number of threads used by the
        // kernel, because stencils. So we unroll.
        staged.compute_at(final, xo).unroll(x, 2).unroll(y, 2).gpu_threads(x, y);
    }

    return final;
}

// Same schedule as in 'build(true)', but with using a wrapper instead of a dummy Func.
Func build_wrap() {
    Func host;
    Var x, y;
    host(x, y) = x + y;
    host.compute_root();

    const int stages = 10;
    Func f[stages];
    for (int i = 0; i < stages; i++) {
        Expr prev = (i == 0) ? Expr(0) : Expr(f[i - 1](x, y));
        Expr stencil = 0;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                stencil += host(select(prev > 0, x, x + dx),
                                select(prev > 0, y, y + dy));
            }
        }
        if (i == 0) {
            f[i](x, y) = stencil;
        } else {
            f[i](x, y) = f[i - 1](x, y) + stencil;
        }
    }

    Func final = f[stages - 1];

    Var xo, yo, xi, yi;
    final.compute_root().gpu_tile(x, y, xo, yo, xi, yi, 8, 8);
    for (int i = 0; i < stages - 1; i++) {
        f[i].compute_at(final, xo).gpu_threads(x, y);
    }

    // Create a global wrapper for the input data 'host' and schedule it to load
    // the data into the GPU shared memory as needed per GPU tile.
    host.in().compute_at(final, xo).unroll(x, 2).unroll(y, 2).gpu_threads(x, y);

    return final;
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

    Func use_shared = build(true);
    Func use_l1 = build(false);
    Func use_wrap_for_shared = build_wrap();

    use_shared.compile_jit();
    use_l1.compile_jit();
    use_wrap_for_shared.compile_jit();

    Buffer<int> out1(1000, 1000);
    Buffer<int> out2(1000, 1000);
    Buffer<int> out3(1000, 1000);

    double shared_time = benchmark([&]() {
        use_shared.realize(out1);
        out1.device_sync();
    });

    double l1_time = benchmark([&]() {
        use_l1.realize(out2);
        out2.device_sync();
    });

    double wrap_time = benchmark([&]() {
        use_wrap_for_shared.realize(out3);
        out3.device_sync();
    });

    out1.copy_to_host();
    out2.copy_to_host();
    out3.copy_to_host();

    // Check correctness of the wrapper version
    for (int y = 0; y < out3.height(); y++) {
        for (int x = 0; x < out3.width(); x++) {
            if (out3(x, y) != out1(x, y)) {
                printf("wrapper(%d, %d) = %d instead of %d\n",
                       x, y, out3(x, y), out1(x, y));
                return 1;
            }
        }
    }
    for (int y = 0; y < out3.height(); y++) {
        for (int x = 0; x < out3.width(); x++) {
            if (out3(x, y) != out2(x, y)) {
                printf("wrapper(%d, %d) = %d instead of %d\n",
                       x, y, out3(x, y), out2(x, y));
                return 1;
            }
        }
    }

    printf("using shared: %f\n"
           "using l1: %f\n"
           "using wrap for shared: %f\n",
           shared_time, l1_time, wrap_time);

    printf("Success!\n");
    return 0;
}
