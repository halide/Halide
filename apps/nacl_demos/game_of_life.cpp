#include "Halide.h"

using namespace Halide;

Var x("x"), y("y"), c("c");

int main(int argc, char **argv) {

    Expr random_bit = cast<uint8_t>(random_float() > 0.5f);

    // First define the function that gives the initial state of the
    // game board
    {
        Func initial;

        initial(x, y, c) = random_bit;
        initial.compile_to_file("game_of_life_init", {});
    }

    // Then the function that updates the state. Also depends on user input.
    {
        ImageParam state(UInt(8), 3);
        Param<int> mouse_x, mouse_y;

        // Add a boundary condition.
        Func clamped = BoundaryConditions::repeat_edge(state);

        Expr xm = max(x-1, 0), xp = min(x+1, state.width()-1);
        Expr ym = max(y-1, 0), yp = min(y+1, state.height()-1);

        // Count the number of live neighbors.
        Expr count = (clamped(x - 1, y - 1, c) + clamped(x, y - 1, c) +
                      clamped(x + 1, y - 1, c) + clamped(x - 1, y, c) +
                      clamped(x + 1, y, c) + clamped(x - 1, y + 1, c) +
                      clamped(x, y + 1, c) + clamped(x + 1, y + 1, c));

        // Was this pixel alive in the previous generation?
        Expr alive_before = state(x, y, c) != 0;

        // We're alive in the next generation if we have two neighbors and
        // were alive before, or if we have three neighbors.
        Expr alive_now = (count == 2 && alive_before) || count == 3;

        Expr alive = cast<uint8_t>(1);
        Expr dead = cast<uint8_t>(0);

        Func output;
        output(x, y, c) = select(alive_now, alive, dead);

        // Clobber part of the output around where the mouse is with random junk
        Expr min_x = clamp(mouse_x - 10, 0, state.width()-1);
        Expr max_x = clamp(mouse_x + 10, 0, state.width()-1);
        Expr min_y = clamp(mouse_y - 10, 0, state.height()-1);
        Expr max_y = clamp(mouse_y + 10, 0, state.height()-1);
        RDom clobber(min_x, max_x - min_x + 1, min_y, max_y - min_y + 1);

        Expr dx = clobber.x - mouse_x;
        Expr dy = clobber.y - mouse_y;
        Expr r = dx*dx + dy*dy;

        output(clobber.x, clobber.y, c) =
            select(r < 100,
                   cast<uint8_t>(random_float() < 0.25f),
                   output(clobber.x, clobber.y, c));

        output.vectorize(x, 16);
        clamped.compute_at(output, x);

        Var yi;
        output.split(y, y, yi, 16).reorder(x, yi, c, y).parallel(y);

        output.compile_to_file("game_of_life_update", {state, mouse_x, mouse_y});
    }

    // Now the function that converts the state into an argb image.
    {
        ImageParam state(UInt(8), 3);

        Func state_32;
        state_32(x, y, c) = cast<int32_t>(state(x, y, c));

        Func render;
        Expr r = select(state_32(x, y, 0) == 1, 255, 0);
        Expr g = select(state_32(x, y, 1) == 1, 255, 0);
        Expr b = select(state_32(x, y, 2) == 1, 255, 0);
        render(x, y) = (255 << 24) + (r << 16) + (g << 8) + b;

        render.vectorize(x, 4);
        state_32.compute_at(render, x);

        Var yi;
        render.split(y, y, yi, 16).parallel(y);

        render.compile_to_file("game_of_life_render", {state});
    }

    return 0;
}
