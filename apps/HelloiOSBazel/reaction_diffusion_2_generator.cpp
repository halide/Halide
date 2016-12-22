#include "Halide.h"

namespace {

// TODO: convert to new-style once Input<Buffer> added
class ReactionDiffusion2Init : public Halide::Generator<ReactionDiffusion2Init> {
public:
    Param<float> cx{"cx"};
    Param<float> cy{"cy"};

    Func build() {
        Func initial;
        initial(x, y, c) = Halide::random_float();

        // schedule 
        if (get_target().has_gpu_feature()) {
            initial.reorder(c, x, y).bound(c, 0, 3).vectorize(c).gpu_tile(x, y, 4, 4);
            initial.output_buffer()
                .dim(0).set_stride(3)
                .dim(2).set_bounds(0, 3).set_stride(1);
        }
        
        return initial;
    }

private:
    Var x, y, c;
};

HALIDE_REGISTER_GENERATOR(ReactionDiffusion2Init, "reaction_diffusion_2_init")

// TODO: convert to new-style once Input<Buffer> added
class ReactionDiffusion2Update : public Halide::Generator<ReactionDiffusion2Update> {
public:
    ImageParam state{Float(32), 3, "state"};
    Param<int> mouse_x{"mouse_x"};
    Param<int> mouse_y{"mouse_y"};
    Param<float> cx{"cx"};
    Param<float> cy{"cy"};
    Param<int> frame{"frame"};

    Func build() {
        Func clamped = Halide::BoundaryConditions::repeat_edge(state);

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

        Func new_state{"new_state"};
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

        // schedule
        state.dim(2).set_bounds(0, 3);
        new_state.reorder(c, x, y).bound(c, 0, 3).unroll(c);

        if (get_target().has_gpu_feature()) {
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

            state
                .dim(0).set_stride(3)
                .dim(2).set_stride(1).set_extent(3);
            new_state.output_buffer()
                .dim(0).set_stride(3)
                .dim(2).set_stride(1).set_extent(3);
        } else {
            Var yi;
            new_state.split(y, y, yi, 64).parallel(y);

            blur.compute_at(new_state, yi);
            clamped.store_at(new_state, y).compute_at(new_state, yi);

            new_state.vectorize(x, 4);
            blur.vectorize(x, 4);
        }

        return new_state;
    }

private:
    Func blur_x, blur_y, blur;
    Var x, y, c;
};

HALIDE_REGISTER_GENERATOR(ReactionDiffusion2Update, "reaction_diffusion_2_update")

// TODO: convert to new-style once Input<Buffer> added
class ReactionDiffusion2Render : public Halide::Generator<ReactionDiffusion2Render> {
public:
    ImageParam state{Float(32), 3, "state"};

    Func build() {
        Func render;

        Func contour;
        contour(x, y, c) = pow(state(x, y, c) * (1 - state(x, y, c)) * 4, 8);

        Expr c0 = contour(x, y, 0), c1 = contour(x, y, 1), c2 = contour(x, y, 2);

        Expr R = min(c0, max(c1, c2));
        Expr G = (c0 + c1 + c2)/3;
        Expr B = max(c0, max(c1, c2));

        // TODO: ugly hack to rearrange output
        const int32_t kRFactor = get_target().has_gpu_feature() ? (1 << 16) : (1 << 0);
        const int32_t kGFactor = get_target().has_gpu_feature() ? (1 << 8) : (1 << 8);
        const int32_t kBFactor = get_target().has_gpu_feature() ? (1 << 0) : (1 << 16);

        Expr alpha = cast<int32_t>(255 << 24);
        Expr red = cast<int32_t>(R * 255) * kRFactor;
        Expr green = cast<int32_t>(G * 255) * kGFactor;
        Expr blue = cast<int32_t>(B * 255) * kBFactor;

        render(x, y) = cast<int32_t>(alpha + red + green + blue);

        // schedule
        if (get_target().has_gpu_feature()) {
            state
                .dim(0).set_stride(3)
                .dim(2).set_stride(1).set_bounds(0, 3);
            render.gpu_tile(x, y, 32, 4);
        } else {
            render.vectorize(x, 4);
            Var yi;
            render.split(y, y, yi, 64).parallel(y);
        }

        return render;
    }

private:
    Var x, y, c;
};

HALIDE_REGISTER_GENERATOR(ReactionDiffusion2Render, "reaction_diffusion_2_render")

}  // namespace
