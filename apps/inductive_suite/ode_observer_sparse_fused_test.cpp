// Allen-Cahn phase-field dynamics on a 1-D grid (f = eps*laplacian(y) + y -
// y^3), integrated by Adams-Bashforth 2 from a classical RK4 start, with an
// observer reading the mean order parameter <y> of every slice. The Halide
// forms:
//   A. inductive FOLDED   (y is inductive in n; its history folds to a 2-slice
//                          window; the observer is folded into the step)
//   B. inductive UNFOLDED (the same, with the history pinned to the full T extent)
//   C. non-inductive      (an RDom scan materializes the full O(D*B*T)
//                          trajectory; the observer is a second pass over it)
// and two baselines:
//   D. Boost.odeint's adams_bashforth<2> with a runge_kutta4 start and its
//      observer, and a fused C++ loop: the AB2 update, the right-hand side
//      of the new slice, and the slice's sum in one pass per step.
// Everything timed is float, baselines included.

#include "Halide.h"

#include "../support/bench_harness.h"
#include "../support/jit_support.h"
#include <boost/numeric/odeint.hpp>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace Halide;

int main(int argc, char **argv) {
    int D = argc > 1 ? atoi(argv[1]) : 1024;
    int B = argc > 2 ? atoi(argv[2]) : 1;
    int T = argc > 3 ? atoi(argv[3]) : 8192;
    const float h = 0.02f, eps = 0.1f;
    // The inductive observer keeps 16 lane partials along the slice.
    if (D % 16 != 0) {
        fprintf(stderr, "D must be a multiple of 16\n");
        return 1;
    }

    Buffer<float> y0(D, B);
    srand(5);
    for (int bb = 0; bb < B; bb++)
        for (int i = 0; i < D; i++)
            y0(i, bb) = (float)(rand() % 200) / 100.0f - 1.0f;

    Var d("d"), b("b"), n("n");

    // The right-hand side and one classical RK4 step, on the host: the
    // integrator is Adams-Bashforth 2, which needs two initial slices, so
    // y1 is one RK4 step from y0, computed here and handed to every
    // pipeline as an input. The fused C++ loop uses the same two functions
    // for its start; Boost's stepper initializes itself with its own
    // runge_kutta4.
    // The two boundary elements apart, so the interior vectorizes: with the
    // neighbours read through conditional indices the whole stencil stays
    // scalar.
    auto rhs_at = [&](float lm, float c, float lp) { return eps * (lm - 2.f * c + lp) + c - c * c * c; };
    auto rhs = [&](const std::vector<float> &yv, std::vector<float> &fv) {
        const float *__restrict__ y = yv.data();
        float *__restrict__ f = fv.data();
        f[0] = rhs_at(y[0], y[0], y[1]);
        for (int i = 1; i < D - 1; i++)
            f[i] = rhs_at(y[i - 1], y[i], y[i + 1]);
        f[D - 1] = rhs_at(y[D - 2], y[D - 1], y[D - 1]);
    };
    auto rk4_step = [&](std::vector<float> &y) {
        std::vector<float> k1(D), k2(D), k3(D), k4(D), tmp(D);
        rhs(y, k1);
        for (int i = 0; i < D; i++)
            tmp[i] = y[i] + 0.5f * h * k1[i];
        rhs(tmp, k2);
        for (int i = 0; i < D; i++)
            tmp[i] = y[i] + 0.5f * h * k2[i];
        rhs(tmp, k3);
        for (int i = 0; i < D; i++)
            tmp[i] = y[i] + h * k3[i];
        rhs(tmp, k4);
        for (int i = 0; i < D; i++)
            y[i] += (h / 6.f) * (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i]);
    };

    Buffer<float> y1(D, B);
    {
        std::vector<float> yv(D);
        for (int bb = 0; bb < B; bb++) {
            for (int i = 0; i < D; i++)
                yv[i] = y0(i, bb);
            rk4_step(yv);
            for (int i = 0; i < D; i++)
                y1(i, bb) = yv[i];
        }
    }

    // The stencil right-hand side of a slice, as the host computes it:
    // f = eps*lap + c - c^3, left-associated.
    auto stencil = [&](Expr left, Expr center, Expr right) {
        Expr lap = left - 2.0f * center + right;
        return eps * lap + center - center * center * center;
    };

    // The observer as a second pass over a slice: the mean order parameter
    // <y> = (1/D) sum_i y_i, the standard phase-field monitoring diagnostic.
    // Summed in two stages: 16 partial sums across the lanes with plain
    // vector adds over the slice, then those 16 reduced once per step,
    // rather than a horizontal reduction of every vector into the scalar.
    auto observe = [&](Func E, auto slice) {
        RDom rd(0, D, "rd");
        E(b, n) = cast<float>(0);
        E(b, n) += slice(rd.x, b, n) * (1.0f / D);
        RVar rdo("rdo"), rdi("rdi");
        Var l("l");
        E.update(0).split(rd.x, rdo, rdi, 16);
        Func partial = E.update(0).rfactor(rdi, l);
        partial.compute_at(E, n).vectorize(l);
        partial.update(0).reorder(l, rdo).vectorize(l);
        E.compute_root();
        E.update(0).reorder(rdi, b, n);
        E.bound(b, 0, B).bound(n, 0, T);
    };

    // Build one inductive pipeline, the observer folded into the step.
    // `fold_k` pins the state window (2 = folded, T = unfolded ablation).
    auto build = [&](const char *tag, int fold_k) {
        // The state carries {y_n, f(y_{n-1}), running sum}: each step
        // evaluates the right-hand side once, on the slice it carries, and
        // reads the one before from the carried component, so nothing
        // reaches back more than one step. The running sum is the observer
        // folded into the step: each element adds its value to the sum 16
        // elements before it in the same slice, so the last 16 elements
        // hold 16 lane partials of the slice's sum, and the vector of 16
        // lanes reads the vector written just before it.
        Func y({Float(32), Float(32), Float(32)}, 3, std::string("y_") + tag), E(std::string("E_") + tag);
        y(d, b, n) = Tuple(cast<float>(0), cast<float>(0), cast<float>(0));
        RDom r(0, D, "r");
        Expr i = r.x;
        Expr iL = max(i - 1, 0), iR = min(i + 1, D - 1);
        Expr c1 = y(i, b, n - 1)[0];
        Expr f1 = stencil(y(iL, b, n - 1)[0], c1, y(iR, b, n - 1)[0]);
        Expr f2 = y(i, b, n - 1)[1];
        Expr step_y = c1 + h * (1.5f * f1 - 0.5f * f2);
        // Base cases: n<=0 -> y0 (its derivative is never read); n==1 -> the
        // RK4 slice y1, carrying f(y0) for the first AB2 step.
        Expr f0 = stencil(y0(iL, b), y0(i, b), y0(iR, b));
        Expr val = select(n <= 1, select(n <= 0, y0(i, b), y1(i, b)), likely(step_y));
        Expr fval = select(n <= 1, select(n <= 0, cast<float>(0), f0), likely(f1));
        Expr carried = select(i >= 16, y(max(i - 16, 0), b, n)[2], cast<float>(0));
        y(i, b, n) = Tuple(val, fval, carried + val);

        // The observer reads the 16 lane partials at the end of the slice.
        RDom rl(0, 16, "rl");
        E(b, n) = cast<float>(0);
        E(b, n) += y(D - 16 + rl, b, n)[2] * (1.0f / D);
        E.compute_root();
        E.update(0).reorder(rl, b, n);
        E.bound(b, 0, B).bound(n, 0, T);

        y.compute_at(E, n).store_root().fold_storage(n, fold_k);
        y.update(0).allow_race_conditions().vectorize(r.x, 16);
        E.compile_jit();
        return E;
    };

    // Three-way ablation: inductive FOLDED (2-slice window) vs inductive
    // UNFOLDED (fold n -> T, full trajectory, same fusion) vs non-inductive
    // materialize.
    Func E_fold = build("fold", 2);
    Func E_unfold = build("unfold", T);

    // Non-inductive (materialize full trajectory) with the two-pass observer.
    // It has every slice in memory, so it recomputes the stencil of the slice
    // two back rather than carrying the derivative: another trajectory's
    // worth of traffic would cost more than the arithmetic saves.
    Func E_mat("E_mat");
    {
        Func y_m(Float(32), "y_m");
        // r.x (row) is an RVar, not a plain pure Var, so the stencil's ±1 shifts
        // can't look like a shift of a pure dimension to is_inductive().
        RDom r(0, D, 2, T - 2, "r");  // r.x = row, r.y = rn; slices 0,1 seeded, scan from n=2
        Expr rd_ = r.x, rn = r.y;
        y_m(d, b, n) = undef<float>();
        y_m(d, b, 0) = y0(d, b);
        y_m(d, b, 1) = y1(d, b);  // RK4 startup
        Expr p1 = rn - 1, p2 = rn - 2;
        auto f = [&](Expr t) {
            return stencil(y_m(max(rd_ - 1, 0), b, t), y_m(rd_, b, t),
                           y_m(min(rd_ + 1, D - 1), b, t));
        };
        y_m(rd_, b, rn) = y_m(rd_, b, p1) + h * (1.5f * f(p1) - 0.5f * f(p2));

        observe(E_mat, [&](Expr x, Expr bb, Expr nn) { return y_m(x, bb, nn); });
        y_m.compute_root();
        y_m.update(0).unscheduled();                                                    // slice 0 init
        y_m.update(1).unscheduled();                                                    // slice 1 (RK4) init
        y_m.update(2).reorder(r.x, r.y, b).allow_race_conditions().vectorize(r.x, 16);  // AB2 scan
        E_mat.compile_jit();
    }

    Buffer<float> ef(B, T), eu(B, T), emat(B, T);
    E_fold.realize(ef);
    E_unfold.realize(eu);
    E_mat.realize(emat);
    hb::Stats s_fold = hb::bench([&]() { E_fold.realize(ef); });
    hb::Stats s_unfold = hb::bench([&]() { E_unfold.realize(eu); });
    hb::Stats s_mat = hb::bench([&]() { E_mat.realize(emat); });

    // Measured peak internal scratch (untimed, separate from the benches above).
    double bytes_ind = hb::profiled_peak_bytes(E_fold, ef);
    double bytes_unf = hb::profiled_peak_bytes(E_unfold, eu);
    double bytes_non = hb::profiled_peak_bytes(E_mat, emat);

    // The mean order parameter <y> of a slice, as a separate pass; the
    // vectorizer keeps the float sum in lane partials.
    auto energy = [&](const std::vector<float> &y) {
        float e = 0;
        for (int i = 0; i < D; i++)
            e += y[i];
        return e / D;
    };
    namespace odeint = boost::numeric::odeint;
    using state = std::vector<float>;
    auto sys = [&](const state &y, state &dy, float) { rhs(y, dy); };
    // Idiomatic Boost.odeint: integrate_n_steps + observer, with runge_kutta4 as the
    // initializing stepper, the same startup as the host's rk4_step. Value, Deriv
    // and Time are all float, so no element is promoted.
    typedef odeint::runge_kutta4<state, float, state, float, odeint::range_algebra,
                                 odeint::default_operations, odeint::initially_resizer>
        rk4_f;
    typedef odeint::adams_bashforth<2, state, float, state, float, odeint::range_algebra,
                                    odeint::default_operations, odeint::initially_resizer, rk4_f>
        ab2_rk4;
    std::vector<float> eb((size_t)B * T);
    hb::Stats s_boost = hb::bench([&]() {
        for (int bb = 0; bb < B; bb++) {
            state x(D);
            for (int i = 0; i < D; i++)
                x[i] = y0(i, bb);
            int step = 0;
            auto obs = [&](const state &xs, float) {
                if (step < T) eb[(size_t)bb * T + step++] = energy(xs);
            };
            ab2_rk4 ab;
            odeint::integrate_n_steps(ab, sys, x, 0.0f, h, T - 1, obs);
        }
    });

    // The fused C++ loop. Five slices rotate by pointer: the current y and
    // its right-hand side f1, the previous right-hand side f2, and the new
    // slice and its right-hand side. One pass per step writes the AB2
    // update, the right-hand side of the new slice (its neighbours' updates
    // recomputed from the inputs, so the loop carries nothing through
    // memory), and the slice's sum, which the vectorizer keeps in lane
    // partials. A non-capturing function, its parameters plain locals: the
    // vectorizer does not see through a closure's members.
    auto ab2_steps = [](int D, int T, float h, float eps, float *y, float *f1, float *f2,
                        float *y_new, float *f_new, float *ec_row, auto rhs_at) {
        for (int nn = 2; nn < T; nn++) {
            const float *__restrict__ yi = y, *__restrict__ fa = f1, *__restrict__ fb = f2;
            float *__restrict__ yo = y_new, *__restrict__ fo = f_new;
#define AB2(j) (yi[j] + h * (1.5f * fa[j] - 0.5f * fb[j]))
            float e = 0;
            yo[0] = AB2(0);
            fo[0] = rhs_at(yo[0], yo[0], AB2(1));
            e += yo[0];
            for (int i = 1; i < D - 1; i++) {
                float cl = AB2(i - 1), c = AB2(i), cr = AB2(i + 1);
                yo[i] = c;
                fo[i] = eps * (cl - 2.f * c + cr) + c - c * c * c;
                e += c;
            }
            yo[D - 1] = AB2(D - 1);
            fo[D - 1] = rhs_at(AB2(D - 2), yo[D - 1], yo[D - 1]);
            e += yo[D - 1];
#undef AB2
            ec_row[nn] = e / D;
            std::swap(y, y_new);
            float *dead = f2;
            f2 = f1;
            f1 = f_new;
            f_new = dead;
        }
    };
    std::vector<float> ec((size_t)B * T);
    hb::Stats s_cpp = hb::bench([&]() {
        std::vector<float> s0(D), s1(D), s2(D), s3(D), s4(D);
        for (int bb = 0; bb < B; bb++) {
            for (int i = 0; i < D; i++)
                s0[i] = y0(i, bb);
            ec[(size_t)bb * T + 0] = energy(s0);  // n=0
            rhs(s0, s3);                          // f_0
            rk4_step(s0);                         // n=1 via RK4
            ec[(size_t)bb * T + 1] = energy(s0);
            rhs(s0, s2);  // f_1
            ab2_steps(D, T, h, eps, s0.data(), s2.data(), s3.data(), s1.data(), s4.data(),
                      &ec[(size_t)bb * T], rhs_at);
        }
    });

    auto relerr = [&](auto get) {
        double e = 0;
        for (int bb = 0; bb < B; bb++)
            for (int nn = 0; nn < T; nn++) {
                float ref = ec[(size_t)bb * T + nn];
                e = std::max(e, (double)std::abs(get(bb, nn) - ref) / (std::abs(ref) + 1e-6f));
            }
        return e;
    };
    double err_f = relerr([&](int bb, int nn) { return ef(bb, nn); });
    double err_u = relerr([&](int bb, int nn) { return eu(bb, nn); });
    double err_m = relerr([&](int bb, int nn) { return emat(bb, nn); });
    double err_b = relerr([&](int bb, int nn) { return eb[(size_t)bb * T + nn]; });
    const double tol = 1e-5;

    const double thr = (double)D * (double)T * B / 1e6;  // M state-updates
    // Analytic unfolded footprint (full trajectory D*B*T floats) = the roofline
    // x-axis; recurrence-length (T) and batch (B) sweeps collapse onto fp/LLC.
    const double fp_unfold = (double)D * B * T * 4;
    char note[176];
    snprintf(note, sizeof(note),
             "SPARSE observer (fold ablation)  D=%d B=%d T=%d  |  state fold %.0fx (%.2f -> %.2f MB)  |  unfolded fp/LLC=%.3f",
             D, B, T, hb::mem_ratio(bytes_non, bytes_ind), hb::mb(bytes_non), hb::mb(bytes_ind),
             hb::footprint_over_llc(fp_unfold));
    hb::print_spec_header("ode_observer_sparse_fused", "host", note);
    hb::print_row("Boost.odeint (rk4 init + observer)", s_boost, thr / (s_boost.min * 1e-3),
                  "Mupd/s", (double)D * 4, err_b, err_b < tol);
    hb::print_row("fused C++ loop", s_cpp, thr / (s_cpp.min * 1e-3),
                  "Mupd/s", 0.0, 0.0, true);
    hb::print_row("non-inductive (materialize)", s_mat, thr / (s_mat.min * 1e-3),
                  "Mupd/s", bytes_non, err_m, err_m < tol, "", fp_unfold);
    hb::print_row("inductive UNFOLDED (fold n -> T)", s_unfold, thr / (s_unfold.min * 1e-3),
                  "Mupd/s", bytes_unf, err_u, err_u < tol, hb::verdict(s_unfold.min, s_mat.min), fp_unfold);
    hb::print_row("inductive FOLDED (fold n -> 2)", s_fold, thr / (s_fold.min * 1e-3),
                  "Mupd/s", bytes_ind, err_f, err_f < tol, hb::verdict(s_fold.min, s_unfold.min), fp_unfold);
    printf("  gap (inductive-fold vs Boost): %+.1f%%   inductive-fold vs non-ind: %.2fx\n",
           100.0 * (s_fold.min - s_boost.min) / s_boost.min, s_mat.min / s_fold.min);
    bool pass = err_f < tol && err_u < tol && err_m < tol && err_b < tol;
    printf("  -> %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
