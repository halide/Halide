#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    {
        Param<bool> param;

        Func f;
        Var x;
        f(x) = select(param, x*0.3f, x*17.0f);
        f.vectorize(x, 4);

        // Vectorize when the output is small
        Expr w = f.output_buffer().width();
        f.specialize(w == 4).specialize(param);

        param.set(true);
        f.realize(100);
    }

    {
        Func f1, f2, g1, g2;
        Var x;

        // Define pipeline A
        f1(x) = x + 7;
        g1(x) = f1(x) + f1(x + 1);

        // Define pipeline B
        f2(x) = x * 34;
        g2(x) = f2(x) + f2(x - 1);

        // Switch between them based on a boolean param
        Param<bool> param;
        Func out;
        out(x) = select(param, g1(x), g2(x));

        // These will be outside the condition that specializes out,
        // but skip stages will nuke their allocation and computation
        // for us.
        f1.compute_root();
        f2.compute_root();

        out.specialize(param);

        param.set(true);
        out.realize(100);
    }

    {
        // Specialize for interleaved vs planar inputs
        ImageParam im(Float(32), 1);
        im.set_stride(0, Expr()); // unconstrain the stride

        Func f;
        Var x;

        f(x) = im(x);
        f.vectorize(x, 8);

        // This would create a gather. We can densify it in the common case:
        f.specialize(im.stride(0) == 1);

        Image<float> image(100);
        im.set(image);

        f.realize(100);
    }

    printf("Success!\n");
    return 0;

}
