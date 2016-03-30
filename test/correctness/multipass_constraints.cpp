#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam in(Float(32), 2, "in");

    Func out("out");
    Var x("x"), y("y");

    out(x, y) = in(x+1, y+1) + in(x-1, y-1);
    out(x, y) += 3.0f;
    out.update().vectorize(x, 4);

    OutputImageParam o = out.output_buffer();

    // Now make some hard-to-resolve constraints
    in.set_bounds(0, in.min(1) - 5, in.extent(1) + o.extent(0));

    o.set_bounds(0, 0, select(o.extent(0) < 22, o.extent(0) + 1, o.extent(0)));

    // The only way to build a query buffer for output bounds right
    // now is to make a buffer_t manually.
    buffer_t out_buffer_t = {0};
    out_buffer_t.min[0] = 2;
    out_buffer_t.extent[0] = 7;
    out_buffer_t.min[1] = 2;
    out_buffer_t.extent[1] = 8;

    Buffer out_buf(Float(32), &out_buffer_t, "out_buf");

    out.infer_input_bounds(out_buf);

    if (in.get().min(0) != -4 ||
        in.get().extent(0) != 34 ||
        in.get().min(1) != 1 ||
        in.get().extent(1) != 10 ||
        out_buf.min(0) != 0 ||
        out_buf.extent(0) != 24 ||
        out_buf.min(1) != 2 ||
        out_buf.extent(1) != 8) {

        printf("Constraints not correctly satisfied:\n"
               "in: %d %d %d %d\n"
               "out: %d %d %d %d\n",
               in.get().min(0), in.get().extent(0),
               in.get().min(1), in.get().extent(1),
               out_buf.min(0), out_buf.extent(0),
               out_buf.min(1), out_buf.extent(1));

        return -1;
    }

    return 0;
}
