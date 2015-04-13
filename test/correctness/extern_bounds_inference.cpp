#include "Halide.h"
#include <stdio.h>

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

// An extern stage that translates.
extern "C" DLLEXPORT int translate(buffer_t *in, int dx, int dy, buffer_t *out) {

    if (in->host == NULL) {
        in->min[0] = out->min[0] + dx;
        in->min[1] = out->min[1] + dy;
        in->extent[0] = out->extent[0];
        in->extent[1] = out->extent[1];
    } else {
        assert(in->elem_size == 1);
        assert(out->elem_size == 1);
        for (int y = out->min[1]; y < out->min[1] + out->extent[1]; y++) {
            for (int x = out->min[0]; x < out->min[0] + out->extent[0]; x++) {
                int in_x = x + dx;
                int in_y = y + dy;
                uint8_t *in_ptr = in->host + (in_x - in->min[0])*in->stride[0] + (in_y - in->min[1])*in->stride[1];
                uint8_t *out_ptr = out->host + (x - out->min[0])*out->stride[0] + (y - out->min[1])*out->stride[1];
                *out_ptr = *in_ptr;
            }
        }
    }

    return 0;
}

using namespace Halide;

void check(ImageParam im, int x, int w, int y, int h) {
    Buffer buf = im.get();
    if (!buf.defined()) {
        printf("Bounds inference didn't occur!\n");
        abort();
    }
    if (buf.min(0) != x || buf.extent(0) != w ||
        buf.min(1) != y || buf.extent(1) != h) {
        printf("Incorrect bounds inference result:\n"
               "Result: %d %d %d %d\n"
               "Correct: %d %d %d %d\n",
               buf.min(0), buf.extent(0), buf.min(1), buf.extent(1),
               x, w, y, h);
        abort();
    }
}

int main(int argc, char **argv) {
    Var x, y;

    const int W = 30, H = 20;

    // Define a pipeline that uses an input image in an extern stage
    // only and do bounds queries.
    {
        ImageParam input(UInt(8), 2);
        Func f;

        std::vector<ExternFuncArgument> args(3);
        args[0] = input;
        args[1] = Expr(3);
        args[2] = Expr(7);

        f.define_extern("translate", args, UInt(8), 2);

        f.infer_input_bounds(W, H);

        // Evaluating the output over [0, 29] x [0, 19] requires the input over [3, 32] x [7, 26]
        check(input, 3, W, 7, H);
    }

    // Define a pipeline that uses an input image in two extern stages
    // with different bounds required for each.
    {
        ImageParam input(UInt(8), 2);
        Func f1, f2, g;

        std::vector<ExternFuncArgument> args(3);
        args[0] = input;
        args[1] = Expr(3);
        args[2] = Expr(7);

        f1.define_extern("translate", args, UInt(8), 2);

        args[1] = Expr(8);
        args[2] = Expr(17);
        f2.define_extern("translate", args, UInt(8), 2);

        g(x, y) = f1(x, y) + f2(x, y);

        // Some schedule.
        f1.compute_root();
        f2.compute_at(g, y);
        Var xi, yi;
        g.tile(x, y, xi, yi, 2, 4);

        g.infer_input_bounds(W, H);

        check(input, 3, W + 5, 7, H + 10);
    }


    // Define a pipeline that uses an input image in an extern stage
    // and an internal stage with different bounds required for each.
    {
        ImageParam input(UInt(8), 2);
        Func f1, f2, g;

        std::vector<ExternFuncArgument> args(3);
        args[0] = input;
        args[1] = Expr(3);
        args[2] = Expr(7);

        f1.define_extern("translate", args, UInt(8), 2);

        f2(x, y) = input(x + 8, y + 17);

        g(x, y) = f1(x, y);
        g(x, y) += f2(x, y);

        f1.compute_at(g, y);
        f2.compute_at(g, x);
        g.reorder(y, x).vectorize(y, 4);

        g.infer_input_bounds(W, H);

        check(input, 3, W + 5, 7, H + 10);
    }

    printf("Success!\n");
    return 0;
}
