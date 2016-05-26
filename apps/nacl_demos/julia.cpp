#include "Halide.h"
using namespace Halide;

Var x("x"), y("y"), c("c");

int main(int argc, char **argv) {

    // First define the function that gives the initial state.
    {
        Func initial;

        // The state is just a counter
        initial() = 0;
        initial.compile_to_static_library("julia_init", {});
    }

    // Then the function that updates the state. Also depends on user input.
    {
        ImageParam state(Int(32), 0);
        Param<int> mouse_x, mouse_y;
        Func new_state;
        // Increment the counter
        new_state() = state() + 1;
        new_state.compile_to_static_library("julia_update", {state, mouse_x, mouse_y});
    }

    // Now the function that converts the state into an argb image.
    {
        ImageParam state(Int(32), 0);

        Expr c_real = cos(state() / 60.0f);
        Expr c_imag = sin(state() / 43.0f);
        Expr r_adjust = (cos(state() / 86.0f) + 2.0f) * 0.25f;
        c_real *= r_adjust;
        c_imag *= r_adjust;

        Func julia;
        julia(x, y, c) = Tuple((x - 511.5f)/350.0f, (y - 511.5f)/350.0f);

        const int iters = 20;

        RDom t(1, iters);
        Expr old_real = julia(x, y, t-1)[0];
        Expr old_imag = julia(x, y, t-1)[1];

        Expr new_real = old_real * old_real - old_imag * old_imag + c_real;
        Expr new_imag = 2 * old_real * old_imag + c_imag;
        Expr mag = new_real * new_real + new_imag * new_imag;
        new_real = select(mag > 1e20f, old_real, new_real);
        new_imag = select(mag > 1e20f, old_imag, new_imag);

        julia(x, y, t) = Tuple(new_real, new_imag);

        // Define some arbitrary measure on the complex plane, and
        // compute the minimum of that measure over the orbit of each
        // point.
        new_real = julia(x, y, t)[0];
        new_imag = julia(x, y, t)[1];
        mag = new_real * c_real - new_imag * new_imag * c_imag;
        Expr measure = minimum(abs(mag - 0.1f));

        // Now pick a color based on that
        Expr r_f = 16 * sqrt(2.0f/(measure + 0.01f));
        Expr b_f = 512 * measure * fast_exp(-measure*measure);
        Expr g_f = (r_f + b_f)/2;

        Expr min_c = min(r_f, min(b_f, g_f));
        r_f -= min_c;
        b_f -= min_c;
        g_f -= min_c;

        Expr r = cast<int32_t>(min(r_f, 255));
        Expr g = cast<int32_t>(min(g_f, 255));
        Expr b = cast<int32_t>(min(b_f, 255));
        Expr color = (255 << 24) | (r << 16) | (g << 8) | b;

        Func render;
        render(x, y) = color;

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
        final.compile_to_static_library("julia_render", {state});
    }

    return 0;
}
