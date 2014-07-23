#include <Halide.h>
using namespace Halide;

Var x("x"), y("y"), c("c");

int main(int argc, char **argv) {

    // First define the function that gives the initial state.
    {
        Param<float> cx, cy;
        Func initial;

        // The initial state is a quantity of three chemicals present
        // at each pixel near the boundaries
        
        Expr dx = (x - cx), dy = (y - cy);
        Expr r = dx * dx + dy * dy;
        Expr mask = r < 200 * 200;
        initial(x, y, c) = random_float();// * select(mask, 1.0f, 0.001f);
        initial.compile_to_file("reaction_diffusion_2_init", cx, cy);
    }

    // Then the function that updates the state. Also depends on user input.
    {
        ImageParam state(Float(32), 3);
        Param<int> mouse_x, mouse_y;
        Param<float> cx, cy;
        
        Func clamped;
        clamped(x, y, c) = state(clamp(x, 0, state.width()-1), clamp(y, 0, state.height()-1), c);
        
        RDom kernel(-1, 3);
        Func g, gaussian;
        g(x) = exp(-x*x*0.4f);
        gaussian(x) = g(x) / sum(g(kernel));
        gaussian.compute_root();

        Func blur_x, blur_y;
        blur_x(x, y, c) = sum(gaussian(kernel) * clamped(x + kernel, y, c));
        blur_y(x, y, c) = sum(gaussian(kernel) * blur_x(x, y + kernel, c));

        // Diffusion
        Expr R = (blur_y(x, y, 0) + blur_y(x, y+1, 0))/2;
        Expr G = blur_y(x, y, 1);
        Expr B = (blur_y(x, y, 2) + blur_y(x, y-1, 2))/2;

        // Push the colors outwards with a sigmoid
        Expr s = 1.3f;
        R *= (1 - s) + s * R * (3 - 2 * R);
        G *= (1 - s) + s * G * (3 - 2 * G);
        B *= (1 - s) + s * B * (3 - 2 * B);

        // Reaction
        Expr dR = B * (1 - R - G);
        Expr dG = (1 - B) * (R - G);
        Expr dB = 1 - B + 2 * G * R - R - G;

        // Everything increases faster than it decays
        dR = select(dR > 0, dR * 1.45f, dR);
        dG = select(dG > 0, dG * 1.45f, dG);
        dB = select(dB > 0, dB * 1.5f, dB);

        Expr dx = (x - cx)/cx, dy = (y - cy)/cy;
        Expr radius = dx * dx + dy*dy;
        Expr t = 0.15f;
        
        R += t * dR;
        G += t * dG;
        B += t * dB;

        R = clamp(R, 0.0f, 1.0f);
        G = clamp(G, 0.0f, 1.0f);
        B = clamp(B, 0.0f, 1.0f);

        Func new_state;
        new_state(x, y, c) = select(c == 0, R, select(c == 1, G, B));

        // Noise at the edges
        new_state(x, state.top(), c) = random_float();
        new_state(x, state.bottom(), c) = random_float();
        new_state(state.left(), y, c) = random_float();
        new_state(state.right(), y, c) = random_float();

        // Add some white where the mouse is
        Expr min_x = clamp(mouse_x - 20, 0, state.width()-1);
        Expr max_x = clamp(mouse_x + 20, 0, state.width()-1);
        Expr min_y = clamp(mouse_y - 20, 0, state.height()-1);
        Expr max_y = clamp(mouse_y + 20, 0, state.height()-1);
        RDom clobber(min_x, max_x - min_x + 1, min_y, max_y - min_y + 1);

        dx = clobber.x - mouse_x;
        dy = clobber.y - mouse_y;
        radius = dx * dx + dy * dy;
        new_state(clobber.x, clobber.y, c) = select(radius < 400.0f, 1.0f, new_state(clobber.x, clobber.y, c));

        new_state.reorder(c, x, y).bound(c, 0, 3).unroll(c);

        Var yi;
        new_state.split(y, y, yi, 64).parallel(y);

        blur_x.store_at(new_state, y).compute_at(new_state, yi);
        blur_y.store_at(new_state, y).compute_at(new_state, yi);
        clamped.store_at(new_state, y).compute_at(new_state, yi);

        new_state.vectorize(x, 4);
        blur_x.vectorize(x, 4);
        blur_y.vectorize(x, 4);

        new_state.compile_to_file("reaction_diffusion_2_update", state, mouse_x, mouse_y, cx, cy);
    }

    // Now the function that converts the state into an argb image.
    {
        ImageParam state(Float(32), 3);

        Expr R = state(x, y, 0), G = state(x, y, 1), B = state(x, y, 2);

        Expr alpha = 255 << 24;
        Expr red = cast<int32_t>(R * 255) * (1 << 16);
        Expr green = cast<int32_t>(G * 255) * (1 << 8);
        Expr blue = cast<int32_t>(B * 255);

        Func render;
        render(x, y) = alpha + red + green + blue;

        render.vectorize(x, 4);
        Var yi;
        render.split(y, y, yi, 64).parallel(y);

        render.compile_to_file("reaction_diffusion_2_render", state);
    }

    return 0;
}
