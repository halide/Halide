#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x;
    ImageParam in(Float(32), 1);
    Buffer<float> output(1024);

    {
        // Check that we can infer that a func has a limited range
        Func f("f"), g("g");

        f(x) = clamp(x, 10, 20);
        f.compute_root();

        // 'in' will only be read from 10 to 20, but we'll only deduce this
        // if we look inside of f.
        g(x) = in(f(x));

        g.infer_input_bounds(output);

        Buffer<float> in_buf = in.get();

        assert(in_buf.min(0) == 10 && in_buf.extent(0) == 11);
    }

    {
        // Check that we can depend on an input parameter
        Func f("f"), g("g");
        Param<int> p;

        f(x) = clamp(x, 10, p);
        f.compute_root();

        // 'in' will only be read from 10 to 20, but we'll only deduce this
        // if we look inside of f.
        g(x) = in(f(x));

        p.set(20);
        g.infer_input_bounds(output);

        Buffer<float> in_buf = in.get();

        assert(in_buf.min(0) == 10 && in_buf.extent(0) == 11);
    }

    {
        // Check that this works transitively
        Func f, g, h;

        f(x) = min(x, 100);
        f.compute_root();

        g(x) = max(f(x) - 10, 0);
        g.compute_root();

        h(x) = in(g(g(g(x))));

        in.reset();
        h.infer_input_bounds(output);
        Buffer<float> in_buf = in.get();

        assert(in_buf.min(0) == 0 && in_buf.extent(0) == 91);
    }

    {
        // Check that it doesn't have horrible complexity, and works across tuple elements

        std::vector<Func> fs;

        Func f;
        f(x) = Tuple(clamp(x, 0, 2), clamp(x, 1, 3));
        f.compute_root();
        fs.push_back(f);

        for (int i = 1; i < 20; i++) {
            f = Func();
            f(x) = Tuple(fs[i - 1](x)[0] + fs[i - 1](x)[1],
                         fs[i - 1](x)[1] - fs[i - 1](x)[0]);
            f.compute_root();
            fs.push_back(f);
        }

        Func h;
        h(x) = in(fs[19](x)[0] + fs[19](x)[1]);

        in.reset();
        h.infer_input_bounds(output);
        Buffer<float> in_buf = in.get();

        assert(in_buf.min(0) == -1049600 && in_buf.extent(0) == 2097153);
    }

    printf("Success!\n");
    return 0;
}
