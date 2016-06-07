#include "Halide.h"

#include "../performance/benchmark.h"

using namespace Halide;

void dump_asm(Func f) {
    Target t("host-no_runtime-no_asserts-no_bounds_query");
    f.compile_to_assembly("/dev/stdout", {}, t);
}

int main(int argc, char **argv) {
    {
        Func f, g;
        Var x, y;
        f(x, y) = x + y;
        f.compute_root();
        g(x, y) = f(x-1, y) + f(x, y) + f(x+1, y);

        g.realize(100, 100);
    }


    {
        Func f, g;
        Var x, y;
        f(x, y) = x + y;
        f.compute_root();
        g(x, y) = f(x-4, y) + f(x, y) + f(x+4, y);

        g.vectorize(x, 4);

        g.realize(100, 100);
    }

    {
        Func f, g, h;
        Var x, y;
        f(x, y) = x + y;
        h(x, y) = x + y;
        g(x, y) = f(x-1, y) + f(x, y) + f(x+1, y) + h(x, y);

        f.compute_root();
        h.compute_at(g, x);

        dump_asm(g);

        g.realize(100, 100);
    }

    {
        Func f, g;
        Var x, y;
        f(x, y) = x + y;
        f.compute_root();
        g(x, y) = f(min(100, x-1), y) + f(min(100, x), y) + f(min(100, x+1), y);

        g.realize(100, 100);
    }

    {
        Func f, g;
        Var x, y;
        f(x, y) = x + y;
        f.compute_root();
        g(x, y) = f(clamp(f(x-1, y), 0, 100), y) + f(clamp(f(x, y), 0, 100), y);

        g.realize(100, 100);
    }

    {
        // A case where the index is lifted out into a let
        Func f, g;
        Var x, y;
        f(x, y) = x + y;
        f.compute_root();
        g(x, y) = f((x-y*2), y) + f((x-y*2)+1, y) + f((x-y*2)+2, y);

        g.realize(100, 100);
    }

    {
        // A case where the index and a load are both lifted out into a let
        Func f, g;
        Var x, y;
        f(x, y) = x + y;
        f.compute_root();
        g(x, y) = f((x-y*2), y) + f((x-y*2)+1, y) + f((x-y*2)+2, y) + f((x-y*2)+2, y);

        g.realize(100, 100);
    }

    if (0) {
        // A case where there's a combinatorially large Expr going on.
        Func f, g;
        Var x, y;

        Expr idx = 0;
        for (int i = 0; i < 20; i++) {
            idx += y;
            idx *= idx;
        }

        f(x, y) = x + y;
        f.compute_root();
        g(x, y) = f((x-idx), y) + f((x-idx)+1, y) + f((x-idx)+2, y);

        // Don't send it to LLVM. LLVM has its own problems with these Exprs.
        g.compile_to_module({}, "g");
    }

    {
        // A case with an inner loop.
        Func f, g;
        Var x, y, c;

        f(x, y) = x + y;
        f.compute_root();
        g(c, x, y) = f(x, y) + f(x+1, y) + f(x+2, y) + c;
        g.bound(c, 0, 3).unroll(c).unroll(x, 2);

        g.realize(3, 100, 100);
    }


    {
        // A case with weirdly-spaced taps
        Func f, g;
        Var x, y;

        f(x, y) = x + y;
        f.compute_root();
        g(x, y) = f(x, y) + f(x+1, y) + f(x+3, y);

        g.realize(100, 100);
    }

    {
        // A case with far too many entries to keep around
        Func f, g, h;
        Var x, y, c;

        f(c, x, y) = c + x + y;
        f.compute_root();

        g(c, x, y) = f(c, x-2, y) + f(c, x-1, y) + f(c, x, y) + f(c, x+1, y) + f(c, x+2, y);
        h(c, x, y) = g(c, x, y-2) + g(c, x, y-1) + g(c, x, y) + g(c, x, y+1) + g(c, x, y+2) + f(c, x-3, y);

        h.bound(c, 0, 4).vectorize(c);

        dump_asm(h);

        h.realize(4, 100, 100);

        Image<int> out(4, 1000, 1000);
        double t = benchmark(10, 10, [&]{h.realize(out);});
        printf("%f\n", t);
    }

    if (get_jit_target_from_environment().has_gpu_feature()) {
        // Reusing values from local memory on the GPU is much better
        // than reloading from shared or global.
        Func f, g, h;
        Var x, y;

        f(x, y) = cast<float>(x + y);
        f.compute_root();

        g(x, y) = f(x-2, y) + f(x-1, y) + f(x, y) + f(x+1, y) + f(x+2, y);
        Var xo, xi;
        g.split(x, xo, xi, 16).gpu_tile(xo, y, 8, 8);

        g.realize(160, 100);
    }


    return 0;
}
