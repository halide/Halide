// Follow-up to ode_observer_sparse_test.cpp: try to close the last ~10% gap between the
// (already stencil-fused) Halide inductive version and hand-written Boost/C++ on the
// SPARSE Allen-Cahn free-energy-observer problem.
//
// The observer reduction E(b,n) += energy_density(slice) is vectorized (atomic().vectorize)
// in all Halide variants so the fold ablation is measured with fusion held fixed. We compare:
//   A. inductive FOLDED   (y history folds to a 3-slice window)
//   B. inductive UNFOLDED (same fusion, y history pinned to the full T extent)
//   C. non-inductive      (materialize the full O(D*B*T) trajectory)
//   D. Boost.odeint + observer / C++ reference + observer (third-party baselines)
//
// Build: g++ apps/ode/ode_observer_sparse_fused_test.cpp -O3 -march=native \
//   -Idistrib/include -Lbuild/src -lHalide -lpthread -ldl -o /tmp/ode_spf -std=c++17
//   LD_LIBRARY_PATH=build/src HL_NUM_THREADS=1 /tmp/ode_spf [D B T]

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

    Buffer<float> y0(D, B);
    srand(5);
    for (int bb = 0; bb < B; bb++)
        for (int i = 0; i < D; i++)
            y0(i, bb) = (float)(rand() % 200) / 100.0f - 1.0f;

    // Standardized protocol (warmup + trials + median[p25,p75] via HB_TRIALS).
    auto bench = [&](int /*iters*/, auto fn) { return hb::bench(fn); };

    Var d("d"), b("b"), n("n");

    // The right-hand side and one classical RK4 step, on the host: the
    // integrator is Adams-Bashforth 2, which needs two initial slices, so
    // y1 is one RK4 step from y0, computed here and handed to every
    // pipeline as an input. The C++ reference uses the same two functions;
    // Boost's stepper initializes itself with its own runge_kutta4.
    auto rhs = [&](const std::vector<float> &y, std::vector<float> &f) {
        for (int i = 0; i < D; i++) {
            float lm = y[i > 0 ? i - 1 : 0], lp = y[i + 1 < D ? i + 1 : D - 1];
            f[i] = eps * (lm - 2.f * y[i] + lp) + y[i] - y[i] * y[i] * y[i];
        }
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

    // The stencil right-hand side of a slice, as the reference computes it:
    // f = eps*lap + c - c^3, left-associated.
    auto stencil = [&](Expr left, Expr center, Expr right) {
        Expr lap = left - 2.0f * center + right;
        return eps * lap + center - center * center * center;
    };

    // The observer: the mean order parameter <y> = (1/D) sum_i y_i, the
    // standard phase-field monitoring diagnostic. Summed in two stages: 16
    // partial sums across the lanes with plain vector adds over the slice,
    // then those 16 reduced once per step, rather than a horizontal
    // reduction of every vector into the scalar.
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

    // Build one inductive (dynamics) + observer pipeline. `fold_k` pins the
    // state window (2 = folded, T = unfolded ablation).
    auto build = [&](const char *tag, int fold_k) {
        // The state carries {y_n, f(y_{n-1})}: each step evaluates the
        // right-hand side once, on the slice it carries, and reads the one
        // before from the carried component, as the reference does, so
        // nothing reaches back more than one step.
        Func y({Float(32), Float(32)}, 3, std::string("y_") + tag), E(std::string("E_") + tag);
        y(d, b, n) = Tuple(cast<float>(0), cast<float>(0));
        RDom r(0, D, "r");
        Expr i = r.x;
        Expr iL = clamp(i - 1, 0, D - 1), iR = clamp(i + 1, 0, D - 1);
        Expr c1 = y(i, b, n - 1)[0];
        Expr f1 = stencil(y(iL, b, n - 1)[0], c1, y(iR, b, n - 1)[0]);
        Expr f2 = y(i, b, n - 1)[1];
        Tuple step(likely(c1 + h * (1.5f * f1 - 0.5f * f2)), likely(f1));
        // Base cases: n<=0 -> y0 (its derivative is never read); n==1 -> the
        // RK4 slice y1, carrying f(y0) for the first AB2 step.
        Expr f0 = stencil(y0(iL, b), y0(i, b), y0(iR, b));
        Tuple base = select(n <= 0, Tuple(y0(i, b), cast<float>(0)), Tuple(y1(i, b), f0));
        y(i, b, n) = select(n <= 1, base, step);

        observe(E, [&](Expr x, Expr bb, Expr nn) { return y(x, bb, nn)[0]; });
        y.compute_at(E, n).store_root().fold_storage(n, fold_k);
        y.update(0).allow_race_conditions().vectorize(r.x, 16);
        E.compile_jit();
        return E;
    };

    // Three-way ablation (the observer held fixed): inductive FOLDED (2-slice
    // window) vs inductive UNFOLDED (fold n -> T, full trajectory, same fusion) vs
    // non-inductive materialize.
    Func E_fold = build("fold", 2);
    Func E_unfold = build("unfold", T);

    // Non-inductive (materialize full trajectory) with the SAME observer. It
    // has every slice in memory, so it recomputes the stencil of the slice
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
            return stencil(y_m(clamp(rd_ - 1, 0, D - 1), b, t), y_m(rd_, b, t),
                           y_m(clamp(rd_ + 1, 0, D - 1), b, t));
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
    hb::Stats s_fold = bench(5, [&]() { E_fold.realize(ef); });
    hb::Stats s_unfold = bench(5, [&]() { E_unfold.realize(eu); });
    hb::Stats s_mat = bench(5, [&]() { E_mat.realize(emat); });

    // Measured peak internal scratch (untimed, separate from the benches above).
    double bytes_ind = hb::profiled_peak_bytes(E_fold, ef);
    double bytes_unf = hb::profiled_peak_bytes(E_unfold, eu);
    double bytes_non = hb::profiled_peak_bytes(E_mat, emat);

    // energy + rhs helpers
    auto energy = [&](const std::vector<float> &y) {  // mean order parameter <y>
        double e = 0;
        for (int i = 0; i < D; i++)
            e += y[i];
        return (float)(e / D);
    };
    namespace odeint = boost::numeric::odeint;
    using state = std::vector<float>;
    auto sys = [&](const state &y, state &dy, double) { rhs(y, dy); };
    // Idiomatic Boost.odeint: integrate_n_steps + observer, with runge_kutta4 as the
    // initializing stepper, the same startup as the host's rk4_step (tight agreement).
    typedef odeint::adams_bashforth<2, state, double, state, double, odeint::range_algebra,
                                    odeint::default_operations, odeint::initially_resizer,
                                    odeint::runge_kutta4<state>>
        ab2_rk4;
    std::vector<float> eb((size_t)B * T);
    hb::Stats s_boost = bench(5, [&]() {
        for (int bb = 0; bb < B; bb++) {
            state x(D);
            for (int i = 0; i < D; i++)
                x[i] = y0(i, bb);
            int step = 0;
            auto obs = [&](const state &xs, double) {
                if (step < T) eb[(size_t)bb * T + step++] = energy(xs);
            };
            ab2_rk4 ab;
            odeint::integrate_n_steps(ab, sys, x, 0.0, (double)h, T - 1, obs);
        }
    });

    std::vector<float> ec((size_t)B * T);
    hb::Stats s_cpp = bench(5, [&]() {
        std::vector<float> yv(D), f1(D), f2(D), tmp(D);
        for (int bb = 0; bb < B; bb++) {
            for (int i = 0; i < D; i++)
                yv[i] = y0(i, bb);
            ec[(size_t)bb * T + 0] = energy(yv);  // n=0
            state y0v = yv;
            rk4_step(yv);  // n=1 via RK4
            ec[(size_t)bb * T + 1] = energy(yv);
            rhs(y0v, f2);                     // f_0
            rhs(yv, f1);                      // f_1
            for (int nn = 2; nn < T; nn++) {  // AB2 from n=2
                for (int i = 0; i < D; i++)
                    tmp[i] = yv[i] + h * (1.5f * f1[i] - 0.5f * f2[i]);
                yv = tmp;
                f2 = f1;
                rhs(yv, f1);
                ec[(size_t)bb * T + nn] = energy(yv);
            }
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
                  "Mupd/s", (double)D * 4, err_b, err_b < 1e-3);
    hb::print_row("C++ reference + observer (oracle)", s_cpp, thr / (s_cpp.min * 1e-3),
                  "Mupd/s", 0.0, 0.0, true);
    hb::print_row("non-inductive (materialize)", s_mat, thr / (s_mat.min * 1e-3),
                  "Mupd/s", bytes_non, err_m, err_m < 1e-3, "", fp_unfold);
    hb::print_row("inductive UNFOLDED (fold n -> T)", s_unfold, thr / (s_unfold.min * 1e-3),
                  "Mupd/s", bytes_unf, err_u, err_u < 1e-3, hb::verdict(s_unfold.min, s_mat.min), fp_unfold);
    hb::print_row("inductive FOLDED (fold n -> 2)", s_fold, thr / (s_fold.min * 1e-3),
                  "Mupd/s", bytes_ind, err_f, err_f < 1e-3, hb::verdict(s_fold.min, s_unfold.min), fp_unfold);
    printf("  gap (inductive-fold vs Boost): %+.1f%%   inductive-fold vs non-ind: %.2fx\n",
           100.0 * (s_fold.min - s_boost.min) / s_boost.min, s_mat.min / s_fold.min);
    bool pass = err_f < 1e-3 && err_u < 1e-3 && err_m < 1e-3 && err_b < 1e-3;
    printf("  -> %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
