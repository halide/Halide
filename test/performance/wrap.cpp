#include "Halide.h"
#include "benchmark.h"

using namespace Halide;

Func build_wrap() {
    Func staged;
    Var x, y;
    staged(x, y) = x + y;
    staged.compute_root();

    // Now we just need to access the Func staged a bunch.
    const int stages = 10;
    Func f[stages];
    for (int i = 0; i < stages; i++) {
        Expr prev = (i == 0) ? Expr(0) : Expr(f[i-1](x, y));
        Expr stencil = 0;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                stencil += staged(select(prev > 0, x, x+dx),
                                  select(prev > 0, y, y+dy));
            }
        }
        if (i == 0) {
            f[i](x, y) = stencil;
        } else {
            f[i](x, y) = f[i-1](x, y) + stencil;
        }
    }

    Func final = f[stages-1];

    final.compute_root().gpu_tile(x, y, 8, 8);
    for (int i = 0; i < stages-1; i++) {
        f[i].compute_at(final, Var::gpu_blocks()).gpu_threads(x, y);
    }

    // If we allow staged to use one thread per value loaded, then
    // it forces up the total number of threads used by the
    // kernel, because stencils. So we unroll.
    final.wrap(staged).compute_at(final, Var::gpu_blocks()).unroll(x, 2).unroll(y, 2).gpu_threads(x, y);

    return final;
}

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
        Expr prev = (i == 0) ? Expr(0) : Expr(f[i-1](x, y));
        Expr stencil = 0;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                stencil += staged(select(prev > 0, x, x+dx),
                                  select(prev > 0, y, y+dy));
            }
        }
        if (i == 0) {
            f[i](x, y) = stencil;
        } else {
            f[i](x, y) = f[i-1](x, y) + stencil;
        }
    }

    Func final = f[stages-1];

    final.compute_root().gpu_tile(x, y, 8, 8);
    for (int i = 0; i < stages-1; i++) {
        f[i].compute_at(final, Var::gpu_blocks()).gpu_threads(x, y);
    }

    if (use_shared) {
        // If we allow staged to use one thread per value loaded, then
        // it forces up the total number of threads used by the
        // kernel, because stencils. So we unroll.
        staged.compute_at(final, Var::gpu_blocks()).unroll(x, 2).unroll(y, 2).gpu_threads(x, y);
    }

    return final;
}


int main(int argc, char **argv) {
    Func use_shared = build(true);
    Func use_l1 = build(false);
    Func use_wrap_for_shared = build_wrap();

    use_shared.compile_jit();
    use_l1.compile_jit();

    Image<int> out(1000, 1000);
    Buffer buf(out);

    double shared_time = benchmark(5, 5, [&]() {
            use_shared.realize(buf);
            buf.device_sync();
        });

    double l1_time = benchmark(5, 5, [&]() {
            use_l1.realize(buf);
            buf.device_sync();
        });

    double wrap_time = benchmark(5, 5, [&]() {
            use_wrap_for_shared.realize(buf);
            buf.device_sync();
        });

    printf("using shared: %f\n"
           "using l1: %f\n"
           "using wrap for shared: %f\n",
           shared_time, l1_time, wrap_time);

    return 0;
}