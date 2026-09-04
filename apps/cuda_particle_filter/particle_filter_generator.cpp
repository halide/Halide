// A bootstrap particle filter for the Gordon/Salmond/Smith (1993) nonlinear
// state-space model with a Metropolis resampler, structured like the ODE
// benchmark: the recurrence axis (time) is the inductive pure Var, and the
// cross-element axis (particle) is an RVar, so the resampler's data-dependent
// reads of the previous slice are cross-RVar reads -- which an update allows
// -- and the particle loop nests inside the inductive time loop by
// construction. The cloud folds to two time slices (inductive) or
// materializes the whole trajectory (rdom); the consumer is the per-step
// weighted mean, a reduction over particles that feeds nothing back.
//
// Model:  x_t = 0.5 x_{t-1} + 25 x_{t-1}/(1+x_{t-1}^2) + 8 cos(1.2(t-1)) + v_t
//         y_t = x_t^2/20 + w_t,   v_t ~ N(0,10), w_t ~ N(0,1), x_0 ~ N(0,5).
#include "Halide.h"
#include <cmath>

using namespace Halide;

class ParticleFilter : public Generator<ParticleFilter> {
public:
    GeneratorParam<int> particles{"particles", 1024};
    GeneratorParam<int> steps{"steps", 1024};
    GeneratorParam<int> batch{"batch", 64};
    GeneratorParam<int> chain{"chain", 12};
    GeneratorParam<std::string> scan{"scan", "inductive"};  // inductive (fold) | rdom (materialize)

    Input<Buffer<float, 2>> obs{"obs"};   // observations y(step, batch)
    Output<Buffer<float, 2>> est{"est"};  // filtered mean x_hat(step, batch)

    void generate() {
        Var i("i"), t("t"), b("b");
        const int N = particles;
        const float sigma_v = std::sqrt(10.0f);
        const float sigma_w = 1.0f;
        const bool folded = (scan == std::string("inductive"));

        auto randn = [&]() {
            Expr u1 = random_float(), u2 = random_float();
            return sqrt(-2.0f * log(max(u1, 1e-7f))) * cos(6.2831853f * u2);
        };
        auto loglik = [&](Expr y, Expr x) {
            Expr r = y - x * x / 20.0f;
            return -0.5f * r * r / (sigma_w * sigma_w);
        };

        Func state({Float(32), Float(32)}, 3, "state");  // {x, log-weight}
        state(i, t, b) = Tuple(undef<float>(), undef<float>());

        // One inductive update over the particle RVar, time the inductive Var.
        RDom p(0, N, "p");
        Expr x0 = randn() * std::sqrt(5.0f);
        Expr lw0 = loglik(obs(0, b), x0);

        Expr cur_x = state(p, t - 1, b)[0];
        Expr cur_lw = state(p, t - 1, b)[1];
        for (int k = 0; k < (int)chain; k++) {
            Expr j = clamp(cast<int>(random_float() * N), 0, N - 1);
            Expr xj = state(j, t - 1, b)[0];       // cross-RVar gather: allowed
            Expr lwj = state(j, t - 1, b)[1];
            Expr accept = log(max(random_float(), 1e-20f)) < (lwj - cur_lw);
            cur_x = select(accept, xj, cur_x);
            cur_lw = select(accept, lwj, cur_lw);
        }
        Expr drift = 0.5f * cur_x + 25.0f * cur_x / (1.0f + cur_x * cur_x) +
                     8.0f * cos(1.2f * cast<float>(t - 1));
        Expr x_new = drift + sigma_v * randn();
        Expr lw_new = loglik(obs(t, b), x_new);
        state(p, t, b) = select(t <= 0, Tuple(x0, lw0),
                                Tuple(likely(x_new), likely(lw_new)));

        // Consumer: per-step weighted mean, weights shifted by the slice max.
        RDom rp(0, N, "rp");
        Func mx("mx");
        mx(t, b) = maximum(state(rp, t, b)[1]);
        Func acc({Float(32), Float(32)}, 2, "acc");
        acc(t, b) = {0.0f, 0.0f};
        Expr w = exp(state(rp, t, b)[1] - mx(t, b));
        acc(t, b) += {w, w * state(rp, t, b)[0]};
        est(t, b) = acc(t, b)[1] / max(acc(t, b)[0], 1e-30f);

        // Schedule.
        est.compute_root().reorder(t, b);
        mx.compute_at(est, t);
        acc.compute_at(est, t);
        if (folded) {
            state.compute_at(est, t).store_root().fold_storage(t, 2);
        } else {
            state.compute_root();
        }
        state.update(0).reorder(p, b);
    }
};

HALIDE_REGISTER_GENERATOR(ParticleFilter, particle_filter)
