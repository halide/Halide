#include "Halide.h"

namespace {

class ReactionDiffusion2Init : public Halide::Generator<ReactionDiffusion2Init> {
public:
    Output<Buffer<float, 3>> output{"output"};

    void generate() {
        output(x, y, c) = Halide::random_float();
    }

    void schedule() {
        if (get_target().has_gpu_feature()) {
            output
                .reorder(c, x, y)
                .bound(c, 0, 3)
                .vectorize(c)
                .gpu_tile(x, y, xi, yi, 4, 4);
            output.dim(0).set_stride(3);
            output.dim(2).set_bounds(0, 3).set_stride(1);
        }
    }

private:
    Var x, y, xi, yi, c;
};

class ReactionDiffusion2Update : public Halide::Generator<ReactionDiffusion2Update> {
public:
    Input<Buffer<float, 3>> state{"state"};
    Input<int> mouse_x{"mouse_x"};
    Input<int> mouse_y{"mouse_y"};
    Input<int> frame{"frame"};
    Output<Buffer<float, 3>> new_state{"new_state"};

    void generate() {
        // Add a boundary condition to the old state that treats out of bounds values as random
        clamped = Halide::BoundaryConditions::constant_exterior(state, random_float(frame));

        // Diffusion

        blur_x(x, y, c) = (clamped(x - 3, y, c) +
                           clamped(x - 1, y, c) +
                           clamped(x, y, c) +
                           clamped(x + 1, y, c) +
                           clamped(x + 3, y, c));
        blur_y(x, y, c) = (clamped(x, y - 3, c) +
                           clamped(x, y - 1, c) +
                           clamped(x, y, c) +
                           clamped(x, y + 1, c) +
                           clamped(x, y + 3, c));
        blur(x, y, c) = (blur_x(x, y, c) + blur_y(x, y, c)) / 10;

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
        Expr dB = 0.5f * (1 - B + 2 * G * R - R - G);

        // We'll massively speed up the reaction where the user touches
        Expr dx = x - mouse_x;
        Expr dy = y - mouse_y;
        Expr radius = dx * dx + dy * dy;
        Expr bump = 0.002f * state.dim(0).extent() * state.dim(1).extent() / max(1.0f, radius);
        bump = select(mouse_x >= 0, bump, 0.0f);

        Expr t = 0.04f + bump;

        R += t * dR;
        G += t * dG;
        B += t * dB;

        R = clamp(R, 0.0f, 1.0f);
        G = clamp(G, 0.0f, 1.0f);
        B = clamp(B, 0.0f, 1.0f);

        new_state(x, y, c) = mux(c, {R, G, B});

    }

    void schedule() {
        state.dim(2).set_bounds(0, 3);
        new_state
            .reorder(c, x, y)
            .bound(c, 0, 3)
            .unroll(c);

        if (get_target().has_gpu_feature()) {
            blur
                .reorder(c, x, y)
                .unroll(c)
                .compute_at(new_state, xi);

            new_state.gpu_tile(x, y, xi, yi, 8, 2);

            state.dim(0).set_stride(3);
            state.dim(2).set_stride(1).set_extent(3);
            new_state.dim(0).set_stride(3);
            new_state.dim(2).set_stride(1).set_extent(3);
        } else {
            Var yi;
            new_state
                .split(y, y, yi, 32)
                .parallel(y)
                .vectorize(x, natural_vector_size<float>());

            clamped
                .store_in(MemoryType::Stack)
                .store_at(new_state, y)
                .compute_at(new_state, yi)
                .vectorize(Halide::_0, natural_vector_size<float>());
        }
    }

private:
    Func blur_x, blur_y, blur, clamped;
    Var x, y, xi, yi, c;
};

class ReactionDiffusion2Render : public Halide::Generator<ReactionDiffusion2Render> {
public:
    Input<Buffer<float, 3>> state{"state"};
    // TODO(srj): should be Input<bool>; using Input<int> to work around Issue #1760
    Input<int> output_bgra{"output_bgra", 0, 0, 1};
    Output<Buffer<uint8_t, 3>> render{"render"};

    void generate() {
        Func contour;
        contour(x, y, c) = pow(state(x, y, c) * (1 - state(x, y, c)) * 4, 2);

        Expr c0 = contour(x, y, 0);
        Expr c1 = contour(x, y, 1);
        Expr c2 = contour(x, y, 2);

        Expr R = min(c0, max(c1, c2));
        Expr G = (c0 + c1 + c2) / 3;
        Expr B = max(c0, max(c1, c2));
        Expr A = 1.0f;

        // Metal and CGImage require different pixel layouts.
        // Calculate both here and select() the right one;
        // we'll add specialize() paths in the schedule to
        // make this efficient.
        Expr bgra = mux(c, {cast<uint8_t>(B * 255),
                            cast<uint8_t>(G * 255),
                            cast<uint8_t>(R * 255),
                            cast<uint8_t>(A * 255)});

        Expr rgba = mux(c, {cast<uint8_t>(R * 255),
                            cast<uint8_t>(G * 255),
                            cast<uint8_t>(B * 255),
                            cast<uint8_t>(A * 255)});

        render(x, y, c) = select(output_bgra == true, bgra, rgba);
    }

    void schedule() {
        render.dim(0).set_stride(4);
        render.dim(2).set_stride(1).set_bounds(0, 4);
        if (get_target().has_gpu_feature()) {
            state.dim(0).set_stride(3);
            state.dim(2).set_stride(1).set_bounds(0, 3);
            render
                .reorder(c, x, y)
                .unroll(c)
                .gpu_tile(x, y, xi, yi, 32, 4);
        } else {
            Var yi;
            render
                .reorder(c, x, y)
                .unroll(c)
                .vectorize(x, natural_vector_size<float>())
                .split(y, y, yi, 64)
                .parallel(y);
        }
        render.specialize(output_bgra == true);
        render.specialize(output_bgra == false);
    }

private:
    Var x, y, c, xi, yi;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ReactionDiffusion2Init, reaction_diffusion_2_init)
HALIDE_REGISTER_GENERATOR(ReactionDiffusion2Update, reaction_diffusion_2_update)
HALIDE_REGISTER_GENERATOR(ReactionDiffusion2Render, reaction_diffusion_2_render)
