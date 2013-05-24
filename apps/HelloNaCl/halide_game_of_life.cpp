#include <Halide.h>

using namespace Halide;

// Declare some variables to use in halide function definitions.
Var x, y;

Expr game_of_life(Func last_gen) {
    // Count the number of live neighbors.
    Expr count = (last_gen(x-1, y-1) + last_gen(x, y-1) + 
                  last_gen(x+1, y-1) + last_gen(x-1, y) + 
                  last_gen(x+1, y) + last_gen(x-1, y+1) + 
                  last_gen(x, y+1) + last_gen(x+1, y+1));

    // Was this pixel alive in the previous generation?
    Expr alive_before = last_gen(x, y) != 0;

    // We're alive in the next generation if we have two neighbors and
    // were alive before, or if we have three neighbors.
    Expr alive_now = (count == 2 && alive_before) || count == 3;   

    Expr result = select(alive_now, 255, 0);

    return result;
}

int main(int argc, char **argv) {

    // Declare the input image.
    ImageParam input(UInt(32), 2);

    // Extract the three color channels from the input. We'll run the
    // sim on each independently.
    Func red, green, blue;
    red(x, y)   = (input(x, y)) % 2;
    green(x, y) = (input(x, y) / (1 << 8)) % 2;
    blue(x, y)  = (input(x, y) / (1 << 16)) % 2;

    // We want to do the same thing to each channel, so call a C++
    // function that constructs the same code for each.
    Expr new_red   = game_of_life(red);
    Expr new_green = game_of_life(green);
    Expr new_blue  = game_of_life(blue);

    // Pack the new values into the color channels of the output, and
    // add an alpha of 255.
    Expr result = ((255 << 24) + 
                   (new_blue * (1 << 16)) + 
                   (new_green * (1 << 8)) + 
                   new_red);

    Func output;
    output(x, y) = result;

    // We're done defining the algorithm, now we'll express some
    // optimizations. The algorithm is architecture-neutral, but these
    // optimizations are tuned for x86. The goal of Halide is not to
    // get portable performance without developer intervention - the
    // goal is to get performance as good as writing assembly by hand
    // with less developer pain.

    // Vectorize the output in chunks of size 4. It's 32-bit so that
    // will fit nicely in an sse register.
    output.vectorize(x, 4);

    // Break the output into strips of 16 scanlines, and process all
    // the strips in parallel (using a task queue and a thread
    // pool). The number of threads in the thread pool is configured
    // via an environment variable.
    Var yi;
    output.split(y, y, yi, 16).parallel(y);

    // Emit a C-ABI object file and header that runs this pipeline. If
    // the target is set to use nacl, then this will generate
    // nacl-compatible code. Also emits the assembly source for your perusal.
    std::vector<Argument> args;
    args.push_back(input);
    output.compile_to_object("halide_game_of_life.o", args, "halide_game_of_life");
    output.compile_to_assembly("halide_game_of_life.s", args, "halide_game_of_life");
    output.compile_to_header("halide_game_of_life.h", args, "halide_game_of_life");
    return 0;
}
