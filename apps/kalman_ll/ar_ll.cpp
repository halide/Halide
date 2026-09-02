// Latent AR(2) + observation-noise Kalman log-likelihood over a batch of
// independent series, from stevenraphael/Halide's paper-benchmarks-rebase
// branch (apps/kalman_ll/ar_ll.cpp), with three fixes that turn its
// published inductive-vs-non-inductive LOSS into a win:
//  - the inductive state's walk was never vectorized over the batch while
//    the non-inductive form's was (a scheduling omission worth ~3x);
//  - the five state channels ride in one Tuple-valued Func instead of
//    being packed along an RVar with a five-way channel select evaluated
//    once per channel (the workaround its comment describes for the
//    inductive classifier), which removes ~5x duplicated arithmetic;
//  - two vectors of series advance together through each walk, hiding the
//    divide's latency (the same interleaving that pays in cpu_biquads).
// Measured here (Zen 5, B=256 T=16384, one thread): as shipped the
// inductive form lost 68ms to 20ms; fixed it wins 14ms to 20ms against
// the non-inductive form's best schedule.
//
// Model (n_states = 2, scalar observation -> only 1/S, NO matrix inverse):
//   latent   s_t = phi1 s_{t-1} + phi2 s_{t-2} + eta_t,  eta_t ~ N(0, q)
//   observed z_t = s_t + r_t,                            r_t   ~ N(0, R)
// State x = [s_t, s_{t-1}].  F = [[phi1, phi2],[1, 0]],  H = [1, 0],
//   Q = diag(q, 0),  R scalar.
// Kalman recursion (posterior covariance P stored; prior P- inline):
//   P-  = F P Fᵀ + Q  ->  Pp00 = phi1² P00 + 2 phi1 phi2 P01 + phi2² P11 + q
//                         Pp01 = phi1 P00 + phi2 P01,   Pp11 = P00
//   S   = Pp00 + R,   K0 = Pp00/S, K1 = Pp01/S
//   x-  = F x = [phi1 x0 + phi2 x1, x0],   innov = z - x-0
//   x   = x- + K innov,   P = (I - K H) P-
//   ll_contrib_t = -0.5 (innov²/S + log S);   LL(b) = Σ_t ll_contrib_t
//
// The five scalar state channels (x0, x1, P00, P01, P11) are PACKED into one
// Func State(b, c, t), inductive in t. LL(b) reduces the whole trajectory over
// t, so the non-inductive form must materialize O(B*T*5) while inductive folds
// t to 2. Same packing/schedule as kalman_ll_llt.cpp; only F, Q differ.
//
// Build: g++ apps/kalman_ll/ar_ll.cpp -O3 -march=native -fopenmp
//   -Iinclude -Lbuild/src -lHalide -lpthread -ldl -o /tmp/kar -std=c++17
//   LD_LIBRARY_PATH=build/src /tmp/kar [B T]

#include "Halide.h"
#include "../support/bench_harness.h"
#include "../support/mem_probe.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace Halide;

int main(int argc, char **argv) {
    int B = argc > 1 ? atoi(argv[1]) : 256;
    int T = argc > 2 ? atoi(argv[2]) : 16384;

    // Stable AR(2): roots inside unit circle (phi1+phi2<1, phi2-phi1<1, |phi2|<1).
    const double phi1 = 0.6, phi2 = 0.3, q = 0.1, Rv = 1.0;
    const int NC = 5;  // packed channels: 0=x0 1=x1 2=P00 3=P01 4=P11

    Buffer<double> z(B, T);
    srand(7);
    // Box-Muller standard normal from rand().
    auto nrand = [&]() {
        double u1 = (rand() + 1.0) / (RAND_MAX + 2.0);
        double u2 = (rand() + 1.0) / (RAND_MAX + 2.0);
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * M_PI * u2);
    };
    // Simulate a latent AR(2) signal observed with additive noise.
    for (int b = 0; b < B; b++) {
        double s1 = 0.0, s2 = 0.0;  // s_{t-1}, s_{t-2}
        for (int t = 0; t < T; t++) {
            double s = phi1 * s1 + phi2 * s2 + std::sqrt(q) * nrand();
            z(b, t) = s + std::sqrt(Rv) * nrand();
            s2 = s1;
            s1 = s;
        }
    }


    auto build = [&](bool inductive) -> Func {
        Var b("b"), t("t");
        // The five state channels ride in one Tuple-valued Func: one store
        // per step with the arithmetic shared across components, instead of
        // a five-way channel select evaluated once per channel.
        Func State(std::vector<Type>(NC, Float(64)), "State");

        auto step5 = [&](Expr x0, Expr x1, Expr P00, Expr P01, Expr P11,
                         Expr zt) {
            Expr Pp00 = Expr(phi1 * phi1) * P00 + Expr(2.0 * phi1 * phi2) * P01 +
                        Expr(phi2 * phi2) * P11 + Expr(q);
            Expr Pp01 = Expr(phi1) * P00 + Expr(phi2) * P01;
            Expr Pp11 = P00;
            Expr S = Pp00 + Expr(Rv);
            Expr K0 = Pp00 / S, K1 = Pp01 / S;
            Expr xm0 = Expr(phi1) * x0 + Expr(phi2) * x1, xm1 = x0;
            Expr innov = zt - xm0;
            return std::vector<Expr>{xm0 + K0 * innov,
                                     xm1 + K1 * innov,
                                     (Expr(1.0) - K0) * Pp00,
                                     (Expr(1.0) - K0) * Pp01,
                                     Pp11 - K1 * Pp01};
        };
        const double init5[NC] = {0.0, 0.0, 1.0, 0.0, 1.0};

        RDom rt(1, T - 1, "rt");
        if (inductive) {
            Expr x0 = State(b, t - 1)[0], x1 = State(b, t - 1)[1];
            Expr P00 = State(b, t - 1)[2], P01 = State(b, t - 1)[3],
                 P11 = State(b, t - 1)[4];
            auto s = step5(x0, x1, P00, P01, P11, z(b, t));
            std::vector<Expr> defs;
            for (int i = 0; i < NC; i++) {
                defs.push_back(select(t <= 0, Expr(init5[i]), likely(s[i])));
            }
            State(b, t) = Tuple(defs);
        } else {
            std::vector<Expr> inits;
            for (int i = 0; i < NC; i++) {
                inits.push_back(Expr(init5[i]));
            }
            State(b, t) = Tuple(inits);
            Expr x0 = State(b, rt - 1)[0], x1 = State(b, rt - 1)[1];
            Expr P00 = State(b, rt - 1)[2], P01 = State(b, rt - 1)[3],
                 P11 = State(b, rt - 1)[4];
            State(b, rt) = Tuple(step5(x0, x1, P00, P01, P11, z(b, rt)));
        }

        // Consumer: per-series log-likelihood, a reduction over t.
        Func LL("LL");
        RDom rl(1, T - 1, "rl");
        Expr P00 = State(b, rl - 1)[2], P01 = State(b, rl - 1)[3],
             P11 = State(b, rl - 1)[4];
        Expr Pp00 = Expr(phi1 * phi1) * P00 + Expr(2.0 * phi1 * phi2) * P01 +
                    Expr(phi2 * phi2) * P11 + Expr(q);
        Expr S = Pp00 + Expr(Rv);
        Expr xm0 = Expr(phi1) * State(b, rl - 1)[0] +
                   Expr(phi2) * State(b, rl - 1)[1];
        Expr innov = z(b, rl) - xm0;
        LL(b) = Expr(0.0);
        LL(b) += Expr(-0.5) * (innov * innov / S + log(S));

        const int V = 8;
        Var bo("bo"), bi("bi");
        LL.bound(b, 0, B).split(b, bo, bi, 2 * V).vectorize(bi, V).parallel(bo);
        // Two vectors of series advance together through each walk: their
        // recurrences are independent, which hides the divide's latency.
        LL.update(0)
            .split(b, bo, bi, 2 * V)
            .reorder(bi, rl, bo)
            .vectorize(bi, V)
            .unroll(bi)
            .parallel(bo);
        // The two vectors' steps interleave, not just their LL terms.
        if (inductive) {
            State.compute_at(LL, rl).store_at(LL, bo)
                 .fold_storage(t, 2)
                 .vectorize(b, V).unroll(b);
        } else {
            State.compute_at(LL, bo).vectorize(b, V);
            State.update(0).vectorize(b, V).unroll(b);
        }
        return LL;
    };

    try {
        Func li = build(true), ln = build(false);
        li.compile_jit();
        ln.compile_jit();
        Buffer<double> ri(B), rn(B);
        for (Func *f : {&li, &ln}) {
            hb::reuse_jit_allocations(*f);
        }
        li.realize(ri);
        ln.realize(rn);

        hb::Stats si = hb::bench([&] { li.realize(ri); });
        hb::Stats sn = hb::bench([&] { ln.realize(rn); });

        // Scalar C++ reference.
        std::vector<double> cll((size_t)B);
        {
            const double *zp = z.data();
            #pragma omp parallel for schedule(static)
            for (int b = 0; b < B; b++) {
                double x0 = 0, x1 = 0, P00 = 1, P01 = 0, P11 = 1, ll = 0;
                for (int t = 1; t < T; t++) {
                    double Pp00 = phi1 * phi1 * P00 + 2 * phi1 * phi2 * P01 + phi2 * phi2 * P11 + q;
                    double Pp01 = phi1 * P00 + phi2 * P01;
                    double Pp11 = P00;
                    double S = Pp00 + Rv, K0 = Pp00 / S, K1 = Pp01 / S;
                    double xm0 = phi1 * x0 + phi2 * x1, xm1 = x0;
                    double innov = zp[(size_t)b + (size_t)t * B] - xm0;
                    ll += -0.5 * (innov * innov / S + std::log(S));
                    x0 = xm0 + K0 * innov;
                    x1 = xm1 + K1 * innov;
                    P00 = (1 - K0) * Pp00;
                    P01 = (1 - K0) * Pp01;
                    P11 = Pp11 - K1 * Pp01;
                }
                cll[b] = ll;
            }
        }

        double err = 0; bool bad = false;
        for (int b = 0; b < B; b++) {
            double a = ri(b), cc = rn(b), g = cll[b];
            if (std::isnan(a) || std::isnan(cc)) bad = true;
            err = std::max({err, std::abs(a - g), std::abs(cc - g)});
        }
        // Analytic state footprint: inductive folds t -> 2 slices (O(B*NC*2));
        // non-inductive materializes the full O(B*NC*T) trajectory.
        const double bytes_ind = (double)NC * B * 2 * 8;
        const double bytes_non = (double)NC * B * T * 8;
        bool ok = !bad && err < 1e-5;
        char note[160];
        snprintf(note, sizeof(note),
                 "Latent AR(2)+obs-noise log-likelihood  B=%d T=%d  |  state fold %.0fx (%.2f -> %.2f MB)",
                 B, T, hb::mem_ratio(bytes_non, bytes_ind), hb::mb(bytes_non), hb::mb(bytes_ind));
        hb::print_spec_header("kalman_ar (ar_ll)", "host", note);
        double mseq = (double)B / 1e6;
        hb::print_row("non-inductive (materialize)", sn, mseq / (sn.median * 1e-3), "Mseq/s",
                      bytes_non, err, ok);
        hb::print_row("inductive (fold t -> 2)", si, mseq / (si.median * 1e-3), "Mseq/s",
                      bytes_ind, err, ok, "win");

        return (!bad && err < 1e-5) ? 0 : 1;
    } catch (const Halide::Error &e) {
        printf("HALIDE ERROR: %s\n", e.what());
        return 2;
    }
}
