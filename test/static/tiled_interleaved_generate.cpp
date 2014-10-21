#include <Halide.h>
#include <stdio.h>

using std::vector;

using namespace Halide;

int main(int argc, char **argv) {

    Var x("x"), y("y"), c("c");

    // We're going to define two pipelines. The second will use the first in a tiled fashion.

    // First define a blur.
    {
        // The input tile.
        ImageParam input(Float(32), 3);

        // We pass in parameters to tell us where the boundary
        // condition kicks in. This is decoupled from the size of the
        // input tile.
        Param<int> width, height;

        // In fact, clamping accesses to lie within the input tile
        // using input.min() and input.extent() would tell the calling
        // kernel we can cope with any size input, so it would always
        // pass us 1x1 tiles.

        Func blur("blur");
        blur(x, y, c) = (input(clamp(x-1, 0, width-1), y, c) +
                         input(clamp(x+1, 0, width-1), y, c) +
                         input(x, clamp(y-1, 0, height-1), c) +
                         input(x, clamp(y+1, 0, height-1), c))/4.0f;

        input.set_layout(Interleaved, 3);
        blur.output_buffer().set_layout(Interleaved, 3);

        blur.compile_to_file("tiled_interleaved_blur", input, width, height);
    }

    // Now define the containing pipeline that brightens, then blurs, then brightens some more.
    {
        // The entire input image.
        ImageParam input(Float(32), 3);

        input.set_layout(Interleaved, 3);

        // This is the outermost pipeline, so input width and height
        // are meaningful. If you want to be able to call this outer
        // pipeline in a tiled fashion itself, then you should pass in
        // width and height as params, as with the blur above.

        Func brighter1("brighter1");
        brighter1(x, y, c) = input(x, y, c) * 1.2f;
        brighter1.reorder_storage(c, x, y);

        Func tiled_blur("tiled_interleaved");
        std::vector<ExternFuncArgument> args(3);
        args[0] = brighter1;

        // Pass input.width() and input.height() down to the blur so
        // it knows the global boundary it should clamp to. We assume
        // the global min is at 0, 0.
        args[1] = input.width();
        args[2] = input.height();
        tiled_blur.define_extern("tiled_interleaved_blur", args, Float(32), 3);
        tiled_blur.reorder_storage(tiled_blur.args()[2], tiled_blur.args()[0], tiled_blur.args()[1]);

        Func brighter2("brighter2");
        brighter2(x, y, c) = tiled_blur(x, y, c) * 1.2f;

        Var xi, yi;
        brighter2.reorder(c, x, y).tile(x, y, xi, yi, 32, 32);
        tiled_blur.compute_at(brighter2, x);
        brighter1.compute_at(brighter2, x);

        // Let's see what tiled_blur decides that it needs from
        // brighter1. They should be 34x34 tiles, but clamped to fit
        // within the input, so they'll often be 33x34, 34x33, or
        // 33x33 near the boundaries
        brighter1.trace_realizations();

        brighter2.output_buffer().set_layout(Interleaved, 3);

        brighter2.compile_to_file("tiled_interleaved", input);
    }

    return 0;
}
