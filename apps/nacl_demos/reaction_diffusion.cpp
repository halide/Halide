#include "Halide.h"
using namespace Halide;

Var x("x"), y("y"), c("c");

int main(int argc, char **argv) {

    // First define the function that gives the initial state.
    {
        Func initial;

        // The initial state is a quantity of two chemicals present at each pixel
        initial(x, y, c) = random_float();
        initial.compile_to_static_library("reaction_diffusion_init", {});
    }

    // Then the function that updates the state. Also depends on user input.
    {
        ImageParam state(Float(32), 3);
        Param<int> mouse_x, mouse_y;
        Expr a = state(x, y, 0), b = state(x, y, 1);

        Func clamped = BoundaryConditions::repeat_edge(state);

        RDom kernel(-2, 5);
        Func g, gaussian;
        g(x) = exp(-x*x*0.3f);
        gaussian(x) = g(x) / sum(g(kernel));
        gaussian.compute_root();

        Func blur_x, blur_y;
        blur_x(x, y, c) = sum(gaussian(kernel) * clamped(x + kernel, y, c));
        blur_y(x, y, c) = sum(gaussian(kernel) * blur_x(x, y + kernel, c));

        // Diffusion
        Expr new_a = blur_y(x, y, 0);
        Expr new_b = blur_y(x, y, 1);

        // Reaction
        Expr f = 0.08f;
        Expr k = 0.16f;
        new_a += 0.4f * (a - a*a*a - b + k);
        new_b += 0.4f * f * (a - b);

        new_a = clamp(new_a, 0.0f, 1.0f);
        new_b = clamp(new_b, 0.0f, 1.0f);

        Func new_state;
        new_state(x, y, c) = select(c == 0, new_a, new_b);

        // Finally add some noise at the edges to keep things moving
        Expr r = lerp(-0.05f, 0.05f, random_float());
        new_state(x, 0, c) += r;
        new_state(x, 1023, c) += r;
        new_state(0, y, c) += r;
        new_state(1023, y, c) += r;

        new_state
            .vectorize(x, 4)
            .bound(c, 0, 2).unroll(c);

        Var yi;
        new_state.split(y, y, yi, 16).parallel(y);

        blur_x.store_at(new_state, y).compute_at(new_state, yi);
        blur_x.vectorize(x, 4);

        clamped.store_at(new_state, y).compute_at(new_state, yi);

        new_state.compile_to_static_library("reaction_diffusion_update", {state, mouse_x, mouse_y});
    }

    // Now the function that converts the state into an argb image.
    {
        ImageParam state(Float(32), 3);

        Expr a = state(x, y, 0), b = state(x, y, 1);

        Expr alpha = 255 << 24;
        Expr red = cast<int32_t>(a * 255) * (1 << 16);
        Expr green = 0;
        Expr blue = cast<int32_t>(b * 255);

        Func render;
        render(x, y) = alpha + red + green + blue;

        render.vectorize(x, 4);
        Var yi;
        render.split(y, y, yi, 16).parallel(y);

        render.compile_to_static_library("reaction_diffusion_render", {state});
    }

    return 0;
}
