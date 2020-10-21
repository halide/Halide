#include "Halide.h"

namespace {

class ReactionDiffusionInit : public Halide::Generator<ReactionDiffusionInit> {
public:
    Output<Buffer<float>> output{"output", 3};
    GeneratorParam<bool> threads{"threads", true};

    void generate() {
        output(x, y, c) = Halide::random_float();
    }

    void schedule() {
        output.vectorize(x, natural_vector_size<float>());
        if (threads) {
            output.parallel(y, 8);
        }
    }

private:
    Var x, y, xi, yi, c;
};

class ReactionDiffusionUpdate : public Halide::Generator<ReactionDiffusionUpdate> {
public:
    Input<Buffer<float>> state{"state", 3};
    Input<int> mouse_x{"mouse_x"};
    Input<int> mouse_y{"mouse_y"};
    Input<int> frame{"frame"};
    Output<Buffer<float>> new_state{"new_state", 3};
    GeneratorParam<bool> threads{"threads", false};

    void generate() {
        clamped = Halide::BoundaryConditions::repeat_edge(state);

        blur_x(x, y, c) = (clamped(x - 2, y, c) +
                           clamped(x - 1, y, c) +
                           clamped(x, y, c) +
                           clamped(x + 1, y, c) +
                           clamped(x + 2, y, c));
        blur_y(x, y, c) = (clamped(x, y - 2, c) +
                           clamped(x, y - 1, c) +
                           clamped(x, y, c) +
                           clamped(x, y + 1, c) +
                           clamped(x, y + 2, c));
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
        Expr dB = 1 - B + 2 * G * R - R - G;

        R += dR * 0.15f;
        G += dG * 0.05f;
        B += dB * 0.07f;

        R = clamp(R, 0.0f, 1.0f);
        G = clamp(G, 0.0f, 1.0f);
        B = clamp(B, 0.0f, 1.0f);

        new_state(x, y, c) = mux(c, {R, G, B});

        // Noise at the edges
        new_state(x, state.dim(1).min(), c) = random_float(frame) * 0.2f;
        new_state(x, state.dim(1).max(), c) = random_float(frame) * 0.2f;
        new_state(state.dim(0).min(), y, c) = random_float(frame) * 0.2f;
        new_state(state.dim(0).max(), y, c) = random_float(frame) * 0.2f;

        // Add some white where the mouse is
        Expr min_x = clamp(mouse_x - 20, 0, state.dim(0).extent() - 1);
        Expr max_x = clamp(mouse_x + 20, 0, state.dim(0).extent() - 1);
        Expr min_y = clamp(mouse_y - 20, 0, state.dim(1).extent() - 1);
        Expr max_y = clamp(mouse_y + 20, 0, state.dim(1).extent() - 1);
        clobber = RDom(min_x, max_x - min_x + 1, min_y, max_y - min_y + 1);

        Expr dx = clobber.x - mouse_x;
        Expr dy = clobber.y - mouse_y;
        Expr radius = dx * dx + dy * dy;
        new_state(clobber.x, clobber.y, c) = select(radius < 400.0f,
                                                    1.0f,
                                                    new_state(clobber.x, clobber.y, c));
    }

    void schedule() {
        state.dim(2).set_bounds(0, 3);
        new_state
            .reorder(c, x, y)
            .bound(c, 0, 3)
            .unroll(c);

        Var yi;
        new_state
            .split(y, y, yi, 32)
            .vectorize(x, natural_vector_size<float>());

        blur
            .compute_at(new_state, yi)
            .vectorize(x, natural_vector_size<float>());

        clamped
            .store_at(new_state, y)
            .compute_at(new_state, yi);

        if (threads) {
            new_state.parallel(y);
        }
    }

private:
    Func blur_x, blur_y, blur, clamped;
    Var x, y, xi, yi, c;
    RDom clobber;
};

class ReactionDiffusionRender : public Halide::Generator<ReactionDiffusionRender> {
public:
    Input<Buffer<float>> state{"state", 3};
    Output<Buffer<uint32_t>> render{"render", 2};
    GeneratorParam<bool> threads{"threads", false};

    void generate() {
        Func contour;
        Expr v = state(x, y, c) * (1.01f - state(x, y, c)) * 4;
        v *= v;
        v *= v;
        v = min(v, 1.0f);
        contour(x, y, c) = v;

        Expr c0 = contour(x, y, 0);
        Expr c1 = contour(x, y, 1);
        Expr c2 = contour(x, y, 2);

        Expr R = min(c0, max(c1, c2));
        Expr G = min(1.0f, (c0 + c1 + c2) / 2);
        Expr B = max(c0, max(c1, c2));

        R = cast<uint32_t>(R * 255) & 0xff;
        G = cast<uint32_t>(G * 255) & 0xff;
        B = cast<uint32_t>(B * 255) & 0xff;
        Expr A = cast<uint32_t>(255);

        Expr bgra = B | (G << 8) | (R << 16) | (A << 24);

        render(x, y) = bgra;
    }

    void schedule() {
        render
            .vectorize(x, natural_vector_size<float>());
        if (threads) {
            render.parallel(y, 4);
        }
    }

private:
    Var x, y, c, xi, yi;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ReactionDiffusionInit, reaction_diffusion_init)
HALIDE_REGISTER_GENERATOR(ReactionDiffusionUpdate, reaction_diffusion_update)
HALIDE_REGISTER_GENERATOR(ReactionDiffusionRender, reaction_diffusion_render)
