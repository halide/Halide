#include "Halide.h"
#include <stdio.h>

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

// An extern stage that translates.
extern "C" DLLEXPORT int translate(buffer_t *in, int dx, int dy, buffer_t *out) {
    if (in->host == nullptr) {
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

int main(int argc, char **argv) {
    ImageParam input(UInt(8), 2);
    Var x("x"), y("y");
    Func f0("f0");
    f0(x, y) = input(x, y) * 2;

    Func f1("f1"), f2("f2");
    std::vector<ExternFuncArgument> args(3);
    args[0] = f0;
    args[1] = Expr(3);
    args[2] = Expr(7);

    f1.define_extern("translate", args, UInt(8), 2);

    args[1] = Expr(8);
    args[2] = Expr(17);
    f2.define_extern("translate", args, UInt(8), 2);

    Func g("g");
    g(x, y) = f1(x, y) + f2(x, y);

    // Provide estimates on the pipeline output
    g.estimate(x, 0, 1000).estimate(y, 0, 1000);

    // Provide estimates on the ImageParam
    input.dim(0).set_bounds_estimate(0, 1000);
    input.dim(1).set_bounds_estimate(0, 1000);

    // Auto-schedule the pipeline
    Target target = get_jit_target_from_environment();
    Pipeline p(g);

    p.auto_schedule(target);

    // Inspect the schedule
    g.print_loop_nest();

    printf("Success!\n");
    return 0;
}
