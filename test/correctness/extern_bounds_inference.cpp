#include "Halide.h"
#include <stdio.h>

// An extern stage that translates.
extern "C" HALIDE_EXPORT_SYMBOL int translate(halide_buffer_t *in, int dx, int dy, halide_buffer_t *out) {

    if (in->is_bounds_query()) {
        in->dim[0].min = out->dim[0].min + dx;
        in->dim[1].min = out->dim[1].min + dy;
        in->dim[0].extent = out->dim[0].extent;
        in->dim[1].extent = out->dim[1].extent;
    } else {
        Halide::Runtime::Buffer<uint8_t> out_buf(*out);
        out_buf.translate(dx, dy);
        out_buf.copy_from(Halide::Runtime::Buffer<uint8_t>(*in));
    }

    return 0;
}

using namespace Halide;

void check(ImageParam im, int x, int w, int y, int h) {
    Buffer<uint8_t> buf = im.get();
    if (!buf.data()) {
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

        f.infer_input_bounds({W, H});

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

        g.infer_input_bounds({W, H});

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
        g.update().unscheduled();

        g.infer_input_bounds({W, H});

        check(input, 3, W + 5, 7, H + 10);
    }

    printf("Success!\n");
    return 0;
}
