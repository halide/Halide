#include "Halide.h"
using namespace Halide;

Var x("x"), y("y"), c("c");

int main(int argc, char **argv) {

    // First define the function that gives the initial state.
    {
        Param<float> cx, cy;
        Func initial("initial");

        // The initial state is a quantity of three chemicals present
        // at each pixel near the boundaries

        Expr dx = (x - cx), dy = (y - cy);
        Expr r = dx * dx + dy * dy;
        Expr mask = r < 200 * 200;
        initial(x, y, c) = random_float();// * select(mask, 1.0f, 0.001f);
        initial.reorder(c, x, y).bound(c, 0, 3).vectorize(c).gpu_tile(x, y, 4, 4);
        initial.output_buffer().set_bounds(2, 0, 3);
        initial.output_buffer().set_stride(0, 3);
        initial.output_buffer().set_stride(2, 1);
        initial.compile_to_file("reaction_diffusion_2_init", {cx, cy}, "reaction_diffusion_2_init");
    }

    // Then the function that updates the state. Also depends on user input.
    {
        ImageParam state(Float(32), 3, "state");
        Param<int> mouse_x, mouse_y;
        Param<float> cx, cy;
        Param<int> frame;

        Func clamped = BoundaryConditions::repeat_edge(state);
        state.set_bounds(2, 0, 3);

        Func blur_x("blur_x"), blur_y("blur_y"), blur("blur");
        blur_x(x, y, c) = (clamped(x-3, y, c) +
                           clamped(x-1, y, c) +
                           clamped(x, y, c) +
                           clamped(x+1, y, c) +
                           clamped(x+3, y, c));
        blur_y(x, y, c) = (clamped(x, y-3, c) +
                           clamped(x, y-1, c) +
                           clamped(x, y, c) +
                           clamped(x, y+1, c) +
                           clamped(x, y+3, c));
        blur(x, y, c) = (blur_x(x, y, c) + blur_y(x, y, c))/10;

        Expr R = blur(x, y, 0);
        Expr G = blur(x, y, 1);
        Expr B = blur(x, y, 2);

        // Push the colors outwards with a sigmoid
        Expr s = 0.5f;
        R *= (1 - s) + s * R * (3 - 2 * R);
        G *= (1 - s) + s * G * (3 - 2 * G);
        B *= (1 - s) + s * B * (3 - 2 * B);

        // Reaction
        Expr dR = B * (1 - R - G);
        Expr dG = (1 - B) * (R - G);
        Expr dB = 1 - B + 2 * G * R - R - G;

        Expr bump = (frame % 1024) / 1024.0f;
        bump *= 1 - bump;
        Expr alpha = lerp(0.3f, 0.7f, bump);
        dR = select(dR > 0, dR*alpha, dR);

        Expr t = 0.1f;

        R += t * dR;
        G += t * dG;
        B += t * dB;

        R = clamp(R, 0.0f, 1.0f);
        G = clamp(G, 0.0f, 1.0f);
        B = clamp(B, 0.0f, 1.0f);

        Func new_state("new_state");
        new_state(x, y, c) = select(c == 0, R, select(c == 1, G, B));

        // Noise at the edges
        new_state(x, state.top(), c) = random_float(frame)*0.2f;
        new_state(x, state.bottom(), c) = random_float(frame)*0.2f;
        new_state(state.left(), y, c) = random_float(frame)*0.2f;
        new_state(state.right(), y, c) = random_float(frame)*0.2f;

        // Add some white where the mouse is
        Expr min_x = clamp(mouse_x - 20, 0, state.width()-1);
        Expr max_x = clamp(mouse_x + 20, 0, state.width()-1);
        Expr min_y = clamp(mouse_y - 20, 0, state.height()-1);
        Expr max_y = clamp(mouse_y + 20, 0, state.height()-1);
        RDom clobber(min_x, max_x - min_x + 1, min_y, max_y - min_y + 1);

        Expr dx = clobber.x - mouse_x;
        Expr dy = clobber.y - mouse_y;
        Expr radius = dx * dx + dy * dy;
        new_state(clobber.x, clobber.y, c) = select(radius < 400.0f, 1.0f, new_state(clobber.x, clobber.y, c));

        new_state.reorder(c, x, y).bound(c, 0, 3).unroll(c);
        blur.reorder(c, x, y).vectorize(c);
        blur.compute_at(new_state, Var::gpu_threads());
        new_state.gpu_tile(x, y, 8, 2);
        new_state.update(0).reorder(c, x).unroll(c);
        new_state.update(1).reorder(c, x).unroll(c);
        new_state.update(2).reorder(c, y).unroll(c);
        new_state.update(3).reorder(c, y).unroll(c);
        new_state.update(4).reorder(c, clobber.x).unroll(c);

        new_state.update(0).gpu_tile(x, 8);
        new_state.update(1).gpu_tile(x, 8);
        new_state.update(2).gpu_tile(y, 8);
        new_state.update(3).gpu_tile(y, 8);
        new_state.update(4).gpu_tile(clobber.x, clobber.y, 1, 1);

        std::vector<Argument> args(6);
        args[0] = state;
        args[1] = mouse_x;
        args[2] = mouse_y;
        args[3] = cx;
        args[4] = cy;
        args[5] = frame;
        state.set_stride(0, 3);
        state.set_stride(2, 1);
        state.set_extent(2, 3);
        new_state.output_buffer().set_extent(2, 3);
        new_state.output_buffer().set_stride(0, 3);
        new_state.output_buffer().set_stride(2, 1);
        new_state.compile_to_file("reaction_diffusion_2_update", args, "reaction_diffusion_2_update");
    }

    // Now the function that converts the state into an bgra8 image.
    {
        ImageParam state(Float(32), 3, "state");

        Func contour;
        contour(x, y, c) = pow(state(x, y, c) * (1 - state(x, y, c)) * 4, 8);

        Expr c0 = contour(x, y, 0), c1 = contour(x, y, 1), c2 = contour(x, y, 2);

        Expr R = min(c0, max(c1, c2));
        Expr G = (c0 + c1 + c2)/3;
        Expr B = max(c0, max(c1, c2));

        Expr alpha = 255 << 24;
        Expr red = cast<int32_t>(R * 255) * (1 << 16);
        Expr green = cast<int32_t>(G * 255) * (1 << 8);
        Expr blue = cast<int32_t>(B * 255) * (1 << 0);

        Func render("render");
        render(x, y) = alpha + red + green + blue;

        state.set_bounds(2, 0, 3);
        state.set_stride(2, 1);
        state.set_stride(0, 3);
        render.gpu_tile(x, y, 32, 4);

        render.compile_to_file("reaction_diffusion_2_render", {state}, "reaction_diffusion_2_render");
    }

    return 0;
}
