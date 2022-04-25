#include "Halide.h"

namespace {

class ReactionDiffusionInit : public Halide::Generator<ReactionDiffusionInit> {
public:
    Output<Buffer<float, 3>> output{"output"};
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
    Input<Buffer<float, 3>> state{"state"};
    Input<int> mouse_x{"mouse_x"};
    Input<int> mouse_y{"mouse_y"};
    Input<int> frame{"frame"};
    Output<Buffer<float, 3>> new_state{"new_state"};
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

        // Boost reaction rate using distance from mouse
        Expr mx = (mouse_x - x);
        Expr my = (mouse_y - y);
        Expr boost = 5 * max(0, (1.f - (mx * mx + my * my) * 0.001f)) + 1;

        R += dR * 0.14f * boost;
        G += dG * 0.05f * boost;
        B += dB * 0.065f * boost;

        R = clamp(R, 0.0f, 1.0f);
        G = clamp(G, 0.0f, 1.0f);
        B = clamp(B, 0.0f, 1.0f);

        new_state(x, y, c) = mux(c, {R, G, B});

        // Noise at the edges
        new_state(x, state.dim(1).min(), c) = random_float(frame) * 0.2f;
        new_state(x, state.dim(1).max(), c) = random_float(frame) * 0.2f;
        new_state(state.dim(0).min(), y, c) = random_float(frame) * 0.2f;
        new_state(state.dim(0).max(), y, c) = random_float(frame) * 0.2f;

        noise(x, y, c) = random_float(frame);

        blurry_noise(x, y, c) = 0.25f * (noise(x, y, c) +
                                         noise(x + 1, y, c) +
                                         noise(x + 1, y + 1, c) +
                                         noise(x, y + 1, c));

        // Add some noise where the mouse is
        Expr min_x = clamp(mouse_x - 10, 0, state.dim(0).extent() - 1);
        Expr max_x = clamp(mouse_x + 10, 0, state.dim(0).extent() - 1);
        Expr min_y = clamp(mouse_y - 10, 0, state.dim(1).extent() - 1);
        Expr max_y = clamp(mouse_y + 10, 0, state.dim(1).extent() - 1);
        clobber = RDom(min_x, max_x - min_x + 1, min_y, max_y - min_y + 1);

        Expr dx = clobber.x - mouse_x;
        Expr dy = clobber.y - mouse_y;
        Expr radius = dx * dx + dy * dy;
        new_state(clobber.x, clobber.y, c) = select(radius < 100.0f,
                                                    blurry_noise(clobber.x, clobber.y, c),
                                                    new_state(clobber.x, clobber.y, c));
    }

    void schedule() {
        state.dim(2).set_bounds(0, 3);
        new_state
            .reorder(c, x, y)
            .bound(c, 0, 3)
            .unroll(c);

        noise.compute_root()
            .vectorize(x, natural_vector_size<float>());

        new_state
            .tile(x, y, xi, yi, 256, 8)
            .vectorize(xi, natural_vector_size<float>());

        blur
            .compute_at(new_state, xi)
            .vectorize(x);

        clamped
            .store_at(new_state, x)
            .compute_at(new_state, yi);

        if (threads) {
            new_state.parallel(y);
        }
    }

private:
    Func blur_x, blur_y, blur, clamped, noise, blurry_noise;
    Var x, y, xi, yi, c;
    RDom clobber;
};

class ReactionDiffusionRender : public Halide::Generator<ReactionDiffusionRender> {
public:
    Input<Buffer<float, 3>> state{"state"};
    Output<Buffer<uint32_t, 2>> render{"render"};
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

        Expr R = min(c0, (c1 + c2) / 2);
        Expr G = clamp((c1 + c0 + c2) / 2, 0.0f, 1.0f);
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
