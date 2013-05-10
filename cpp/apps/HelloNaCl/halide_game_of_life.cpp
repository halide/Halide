#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam input(UInt(8), 3);
    Var x, y, c;

    Func clamped;
    clamped(c, x, y) = input(c, clamp(x, 0, input.extent(1)-1), clamp(y, 0, input.extent(2)-1))/128;
    
    Expr count = (clamped(c, x-1, y-1) + clamped(c, x, y-1) + clamped(c, x+1, y-1) + 
                  clamped(c, x-1, y) + clamped(c, x+1, y) + 
                  clamped(c, x-1, y+1) + clamped(c, x, y+1) + clamped(c, x+1, y+1));
    Expr alive_before = clamped(c, x, y) != 0;
    Expr alive_now = (count == 2 && alive_before) || count == 3;

    alive_now = alive_now;

    Expr alive_val = cast(UInt(8), 255);
    Expr dead_val = cast(UInt(8), 0);
    dead_val = select(c == 3, alive_val, dead_val); // alpha channel is always alive

    Func output;
    output(c, x, y) = select(alive_now, alive_val, dead_val);


    Var yi;
    output.vectorize(x, 16).split(y, y, yi, 16).parallel(y);
    clamped.store_at(output, y).compute_at(output, x);
    clamped.reorder_storage(x, y, c);
    output.bound(c, 0, 4).unroll(c);

    std::vector<Argument> args;
    args.push_back(input);
    output.compile_to_object("halide_game_of_life.o", args, "halide_game_of_life");
    output.compile_to_header("halide_game_of_life.h", args, "halide_game_of_life");
    return 0;
}
