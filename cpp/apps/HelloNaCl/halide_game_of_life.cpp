#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {
    // Declare the input image.
    ImageParam input(UInt(8), 3);

    // Declare some variables to use in halide function definitions.
    Var x, y, c;

    // Define a function that adds a boundary condition to the input
    // image. Also divide by 128 to turn the 0-or-255 inputs into
    // 0-or-1. The division compiles to a shift.
    Func clamped;
    clamped(c, x, y) = input(c, clamp(x, 0, input.extent(1)-1), clamp(y, 0, input.extent(2)-1))/128;
    
    // Count the number of live neighbors.
    Expr count = (clamped(c, x-1, y-1) + clamped(c, x, y-1) + 
                  clamped(c, x+1, y-1) + clamped(c, x-1, y) + 
                  clamped(c, x+1, y) + clamped(c, x-1, y+1) + 
                  clamped(c, x, y+1) + clamped(c, x+1, y+1));

    // Was this pixel alive in the previous generation?
    Expr alive_before = clamped(c, x, y) != 0;

    // We're alive in the next generation if we have two neighbors and
    // were alive before, or if we have three neighbors.
    Expr alive_now = (count == 2 && alive_before) || count == 3;

    // Emit 255 if we're alive and 0 if we're dead, to make nice bright colors.
    Expr alive_val = cast(UInt(8), 255);
    Expr dead_val = cast(UInt(8), 0);

    // We don't want the alpha channel to take part. It should always be alive.
    dead_val = select(c == 3, alive_val, dead_val);

    // Define the output image.
    Func output;
    output(c, x, y) = select(alive_now, alive_val, dead_val);

    // We're done defining the algorithm, now we'll express some
    // optimizations. The algorithm is architecture-neutral, but these
    // optimizations are tuned for x86. The goal of Halide is not to
    // get portable performance without developer intervention - the
    // goal is to get performance as good as writing assembly by hand
    // with less developer pain.

    // Vectorize the output in chunks of size 16. It's 8-bit so that
    // will fit nicely in an sse register.
    output.vectorize(x, 16);

    // Break the output into strips of 16 scanlines, and process all
    // the strips in parallel (using a task queue and a thread
    // pool). The number of threads in the thread pool is configured
    // via an environment variable.
    Var yi;
    output.split(y, y, yi, 16).parallel(y);

    // By default, the computation of clamped would be inlined into
    // the computation of the output, but adding the boundary
    // condition doesn't vectorize cleanly, so we're actually better
    // off making a padded copy. It's silly to make it too far ahead
    // of time though because then it won't be in cache. Instead, we
    // allocate a buffer per strip of scanlines to store the padded
    // input, but fill it in lazily as needed per vector of 16
    // outputs. This can be achieved by setting the storage
    // granularity of clamped to output.y, and the compute granularity
    // to output.x.
    clamped.store_at(output, y).compute_at(output, x);

    // Loads from the padded version of the input can be dense vector
    // loads if we store it in color planes instead of packed rgba.
    clamped.reorder_storage(x, y, c);

    // We should statically promise that there will be four output
    // channels (RGBA). Otherwise the compiled code will support an
    // arbitrary number of channels, which is useless overhead. We
    // should also unroll the computation across the channels. This
    // will simplify the alpha channel computation to just store 255.
    output.bound(c, 0, 4).unroll(c);

    // Emit a C-ABI object file and header that runs this pipeline. If
    // the target is set to use nacl, then this will generate
    // nacl-compatible code.
    std::vector<Argument> args;
    args.push_back(input);
    output.compile_to_object("halide_game_of_life.o", args, "halide_game_of_life");
    output.compile_to_header("halide_game_of_life.h", args, "halide_game_of_life");
    return 0;
}
