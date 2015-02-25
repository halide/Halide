#include "Halide.h"
using namespace Halide;

Var x("x"), y("y"), c("c");

int main(int argc, char **argv) {

    // First define the function that gives the initial state.
    {
        Func initial;

        // The state is just a counter
        initial() = 0;
        initial.compile_to_file("julia_init");
    }

    // Then the function that updates the state. Also depends on user input.
    {
        ImageParam state(Int(32), 0);
        Param<int> mouse_x, mouse_y;
        Func new_state;
        // Increment the counter
        new_state() = state() + 1;
        new_state.compile_to_file("julia_update", state, mouse_x, mouse_y);
    }

    // Now the function that converts the state into an argb image.
    {
        ImageParam state(Int(32), 0);

        Expr c_real = cos(state() / 30.0f);
        Expr c_imag = sin(state() / 30.0f);
        Expr r_adjust = (cos(state() / 43.0f) + 1.5f) * 0.5f;
        c_real *= r_adjust;
        c_imag *= r_adjust;

        Func julia;
        julia(x, y, c) = Tuple((x - 511.5f)/256.0f, (y - 511.5f)/256.0f);

        const int iters = 20;

        RDom t(1, iters);
        Expr old_real = julia(x, y, t-1)[0];
        Expr old_imag = julia(x, y, t-1)[1];

        Expr new_real = old_real * old_real - old_imag * old_imag + c_real;
        Expr new_imag = 2 * old_real * old_imag + c_imag;

        julia(x, y, t) = Tuple(new_real, new_imag);

        // How many iterations until something escapes a circle of radius 2?
        new_real = julia(x, y, t)[0];
        new_imag = julia(x, y, t)[1];
        Expr mag = new_real * new_real + new_imag * new_imag;
        Expr escape = argmin(select(mag < 4, 1, 0))[0];

        // Now pick a color based on the number of escape iterations.
        Expr r_scale = 128;
        Expr g_scale = 200;
        Expr b_scale = 256;

        Func color_map;
        Expr escape_f = sqrt(cast<float>(x) / (iters + 1));
        Expr r = cast<int32_t>(escape_f * r_scale);
        Expr g = cast<int32_t>(escape_f * g_scale);
        Expr b = cast<int32_t>(escape_f * b_scale);
        color_map(x) = (255 << 24) | (r << 16) | (g << 8) | b;

        Func render;
        render(x, y) = color_map(escape);

        Var yi;

        // The julia set has rotational symmetry, so we just render
        // the top half and then flip it for the bottom half.
        Func final;
        Expr y_up = min(y, 511);
        Expr y_down = max(y, 512);
        final(x, y) = select(y < 512,
                             render(x, y_up),
                             render(1023 - x, 1023 - y_down));

        Var yo;
        final.bound(x, 0, 1024).bound(y, 0, 1024);
        final.split(y, y, yi, 4).parallel(y);

        render.compute_root();
        render.bound(x, 0, 1024).bound(y, 0, 512);
        render.split(y, y, yi, 4).parallel(y);

        julia.compute_at(render, x);

        render.vectorize(x, 4);
        julia.update().vectorize(x, 4);
        final.vectorize(x, 4);
        final.compile_to_file("julia_render", state);
    }

    return 0;
}
