// Runs the inductive and non-inductive Chebyshev solvers, checks they agree with
// each other and with a plain-C++ reference, and reports timing.

// Self-contained: the Chebyshev pipeline is built and JIT-compiled inline (from
// the same recurrence as chebyshev_inductive_generator.cpp), so no separate
// generator/AOT compilation step is needed.

#include "Halide.h"

#include "../support/bench_harness.h"
#include "../support/jit_support.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Halide;

namespace {

// Default Chebyshev iteration count (overridable via argv[2]).
constexpr int M_DEFAULT = 100;

void host_matvec(const std::vector<double> &A, int n, const std::vector<double> &x,
                 std::vector<double> &y) {
    for (int i = 0; i < n; i++)
        y[i] = 0.0;
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++)
            y[i] += A[(size_t)j * n + i] * x[j];
}

// A = M^T M + n I guarantees lambda_min >= n.
void make_spd(int n, std::vector<double> &A, std::vector<double> &b, std::vector<double> &x_exact) {
    uint64_t s = 1;
    auto rnd = [&] {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return (double)(s >> 11) / (double)(1ULL << 53) * 2.0 - 1.0;
    };
    std::vector<double> Mm((size_t)n * n);
    for (auto &v : Mm)
        v = rnd();
    A.assign((size_t)n * n, 0.0);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            double acc = 0.0;
            for (int kk = 0; kk < n; kk++)
                acc += Mm[(size_t)i * n + kk] * Mm[(size_t)j * n + kk];
            A[(size_t)j * n + i] = acc + (i == j ? (double)n : 0.0);
        }
    x_exact.assign(n, 0.0);
    for (int i = 0; i < n; i++)
        x_exact[i] = rnd();
    b.assign(n, 0.0);
    host_matvec(A, n, x_exact, b);
}

double lambda_max_est(const std::vector<double> &A, int n, int iters = 60) {
    std::vector<double> v(n, 1.0), w(n);
    double lam = 0.0;
    for (int it = 0; it < iters; it++) {
        host_matvec(A, n, v, w);
        double nrm = 0.0;
        for (int i = 0; i < n; i++)
            nrm += w[i] * w[i];
        nrm = std::sqrt(nrm);
        if (nrm == 0) break;
        for (int i = 0; i < n; i++)
            v[i] = w[i] / nrm;
        lam = nrm;
    }
    return lam;
}

void make_coeffs(double lmin, double lmax, int m, std::vector<double> &alpha,
                 std::vector<double> &omega) {
    double d = 0.5 * (lmax + lmin), c = 0.5 * (lmax - lmin);
    alpha.assign(m, 0.0);
    omega.assign(m, 0.0);
    double a_prev = 0.0;
    for (int k = 0; k < m; k++) {
        if (k == 0) {
            alpha[k] = 1.0 / d;
            omega[k] = 0.0;
        } else {
            double beta = (c * a_prev * 0.5) * (c * a_prev * 0.5);
            alpha[k] = 1.0 / (d - beta / a_prev);
            omega[k] = alpha[k] * beta / a_prev;
        }
        a_prev = alpha[k];
    }
}

std::vector<double> host_chebyshev(const std::vector<double> &A, int n, const std::vector<double> &b,
                                   const std::vector<double> &alpha, const std::vector<double> &omega,
                                   int m) {
    std::vector<double> xp(n, 0.0), xc(n, 0.0), xn(n), Ax(n);
    for (int k = 0; k < m; k++) {
        host_matvec(A, n, xc, Ax);
        double a = alpha[k], w = omega[k];
        for (int i = 0; i < n; i++)
            xn[i] = (1.0 + w) * xc[i] - w * xp[i] + a * (b[i] - Ax[i]);
        xp = xc;
        xc = xn;
    }
    return xc;
}

double rel_error(const std::vector<double> &x, const std::vector<double> &y) {
    double e = 0, nn = 0;
    for (size_t i = 0; i < x.size(); i++) {
        double dd = x[i] - y[i];
        e += dd * dd;
        nn += y[i] * y[i];
    }
    return std::sqrt(e / (nn > 0 ? nn : 1.0));
}

// JIT build of the Chebyshev semi-iteration (ported from the generator). Both
// variants keep the same 2-D RDom (output component r.x, mat-vec component r.y)
// nested in the iteration index; they differ only in how the iterate history is
// stored: inductive folds column storage to 3 via fold_storage, non-inductive
// builds the same mod-3 ring by hand and materializes.
// Each form applies the once-per-iterate term (the previous two iterates and
// b) on the first pass over the column, r.y == 0, and only the mat-vec fma on
// the others. The condition is written as r.y <= 0 with likely() on the other
// branch so the loop partitioner peels that first pass: the partitioner needs
// an interval, which r.y == 0 is not, and without the peel every pass would
// load the once-term's three operands.
Func build_cheb(bool inductive, Buffer<float> &A, Buffer<float> &b,
                Buffer<float> &alpha, Buffer<float> &omega, int Miter,
                int fold_k = 3, bool materialize = false) {
    Var t("t"), k("k");
    Expr n = A.dim(0).extent();
    Func x(Float(32), inductive ? "x_i" : (materialize ? "x_m" : "x_n"));
    if (!inductive && materialize) {
        // Non-inductive FULL materialize: store every one of the M+1 iterate
        // columns (index k directly, no mod-3 ring), so the whole trajectory
        // lives in DRAM. This is the true O(M*n) non-inductive baseline that the
        // fold ablation (unfolded -> folded) reduces; the mod-3 ring below is a
        // hand-optimized non-inductive form that already keeps only 3 columns.
        Func X(Float(32), "Xmat");
        X(t, k) = cast<float>(0);
        RDom r(0, n, 0, n, 1, Miter, "r");
        Expr km1 = r.z - 1, km2 = max(0, r.z - 2);
        Expr once = (Expr(1.0f) + omega(km1)) * X(r.x, km1) - omega(km1) * X(r.x, km2) +
                    alpha(km1) * b(r.x);
        Expr matvec = alpha(km1) * A(r.x, r.y) * X(r.y, km1);
        X(r.x, r.z) = select(r.y <= 0, once - matvec, likely(X(r.x, r.z) - matvec));
        x(t) = X(t, Miter);
        X.compute_root();
        X.update(0).allow_race_conditions().vectorize(r.x, 16);
    } else if (inductive) {
        Func X(Float(32), "X");
        X(t, k) = cast<float>(0);
        RDom r(0, n, 0, n, "r");
        Expr km1 = max(0, k - 1), km2 = max(0, k - 2);
        Expr once = (Expr(1.0f) + omega(km1)) * X(r.x, km1) - omega(km1) * X(r.x, km2) +
                    alpha(km1) * b(r.x);
        Expr matvec = alpha(km1) * A(r.x, r.y) * X(r.y, km1);
        X(r.x, k) = select(k <= 0, cast<float>(0),
                           select(r.y <= 0, X(r.x, k) + once - matvec,
                                  likely(X(r.x, k) - matvec)));
        RDom rk(0, Miter + 1, "rk");
        x(t) = cast<float>(0);
        x(t) += select(rk == Miter, X(t, rk), cast<float>(0));
        x.update(0).reorder(t, rk);
        // fold_k = 3 folds the iterate history to a 3-column window; fold_k = M+1
        // pins the full extent (the unfolded ablation), holding fusion fixed.
        X.compute_at(x, rk).store_root().fold_storage(k, fold_k);
        X.update(0).allow_race_conditions().vectorize(r.x, 16);
    } else {
        Func X(Float(32), "Xni");
        X(t, k) = cast<float>(0);
        RDom r(0, n, 0, n, 1, Miter, "r");
        Expr cur = r.z % 3, c1 = (r.z + 2) % 3, c2 = (r.z + 1) % 3, km1 = r.z - 1;
        Expr once = (Expr(1.0f) + omega(km1)) * X(r.x, c1) - omega(km1) * X(r.x, c2) +
                    alpha(km1) * b(r.x);
        Expr matvec = alpha(km1) * A(r.x, r.y) * X(r.y, c1);
        X(r.x, cur) = select(r.y <= 0, once - matvec, likely(X(r.x, cur) - matvec));
        x(t) = X(t, Miter % 3);
        X.compute_root();
        X.update(0).allow_race_conditions().vectorize(r.x, 16);
    }
    return x;  // output bounds come from the realize target buffer
}

}  // namespace

int main(int argc, char **argv) {
    const int n = argc > 1 ? atoi(argv[1]) : 2048;  // dense SPD system size (O(n^2)/iter)
    const int M = argc > 2 ? atoi(argv[2]) : M_DEFAULT;

    std::vector<double> Ah, bh, x_exact;
    make_spd(n, Ah, bh, x_exact);
    double lmax = lambda_max_est(Ah, n) * 1.02;
    double lmin = (double)n * 0.98;  // A = M^T M + n I => lambda_min >= n
    std::vector<double> alpha_h, omega_h;
    make_coeffs(lmin, lmax, M, alpha_h, omega_h);
    std::vector<double> ref = host_chebyshev(Ah, n, bh, alpha_h, omega_h, M);

    // The system, coefficients and solvers are single precision; the setup
    // and the reference solve above stay in double. A is column-major
    // (dim0 = row, fastest), matching make_spd.
    Buffer<float> A(n, n), b(n), alpha(M), omega(M);
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++)
            A(i, j) = (float)Ah[(size_t)j * n + i];
    for (int i = 0; i < n; i++)
        b(i) = (float)bh[i];
    for (int k = 0; k < M; k++) {
        alpha(k) = (float)alpha_h[k];
        omega(k) = (float)omega_h[k];
    }
    Buffer<float> x_inductive(n), x_unfold(n), x_ring(n), x_mat(n);

    // Four variants:
    //   non-inductive FULL materialize (Xmat) : true O(M*n) DRAM trajectory.
    //   non-inductive mod-3 ring       (Xni)  : hand-optimized 3-column baseline.
    //   inductive UNFOLDED (fold -> M+1)      : storage pinned to full extent.
    //   inductive FOLDED   (fold -> 3)        : sliding 3-column window.
    Func f_mat = build_cheb(false, A, b, alpha, omega, M, 3, /*materialize=*/true);
    Func f_ring = build_cheb(false, A, b, alpha, omega, M);
    Func f_unf = build_cheb(true, A, b, alpha, omega, M, M + 1);
    Func f_ind = build_cheb(true, A, b, alpha, omega, M, 3);
    f_mat.compile_jit();
    f_ring.compile_jit();
    f_unf.compile_jit();
    f_ind.compile_jit();
    hb::Stats s_mat = hb::bench([&]() { f_mat.realize(x_mat); });
    hb::Stats s_ring = hb::bench([&]() { f_ring.realize(x_ring); });
    hb::Stats s_unf = hb::bench([&]() { f_unf.realize(x_unfold); });
    hb::Stats s_ind = hb::bench([&]() { f_ind.realize(x_inductive); });

    // Measured peak internal scratch (separate untimed realize per variant; the
    // custom allocator is never active during the timed benches above).
    double bytes_mat = hb::profiled_peak_bytes(f_mat, x_mat);
    double bytes_ring = hb::profiled_peak_bytes(f_ring, x_ring);
    double bytes_unf = hb::profiled_peak_bytes(f_unf, x_unfold);
    double bytes_ind = hb::profiled_peak_bytes(f_ind, x_inductive);

    std::vector<double> xi(n), xu(n), xn(n), xm(n);
    for (int i = 0; i < n; i++) {
        xi[i] = x_inductive(i);
        xu[i] = x_unfold(i);
        xn[i] = x_ring(i);
        xm[i] = x_mat(i);
    }

    double diff = rel_error(xi, xn), diffu = rel_error(xu, xn), diffm = rel_error(xm, xn);
    double err_i = rel_error(xi, x_exact), err_n = rel_error(xn, x_exact),
           err_u = rel_error(xu, x_exact), err_m = rel_error(xm, x_exact);
    double ref_i = rel_error(xi, ref), ref_n = rel_error(xn, ref);
    printf("inductive vs ring: %g   unfold vs ring: %g   materialize vs ring: %g\n", diff, diffu, diffm);
    printf("error vs exact:   inductive %g   unfold %g   ring %g   materialize %g\n", err_i, err_u, err_n, err_m);
    printf("error vs C++ ref: inductive %g   ring %g\n", ref_i, ref_n);

    // Single precision against a double reference: the forms agree with each
    // other to rounding, and all sit within float's reach of the solution.
    bool ok = !(diff > 1e-5 || diffu > 1e-5 || diffm > 1e-5 || err_i > 1e-4 || err_n > 1e-4 ||
                ref_i > 1e-4 || ref_n > 1e-4);
    // Footprint (state_MB) is the measured peak internal scratch of each variant.
    char note[160];
    snprintf(note, sizeof(note),
             "Chebyshev semi-iteration  n=%d M=%d  |  state fold %.0fx  |  unfolded fp/LLC=%.3f",
             n, M, hb::mem_ratio(bytes_mat, bytes_ind), hb::footprint_over_llc(bytes_mat));
    hb::print_spec_header("chebyshev", "host", note);
    hb::print_row("non-inductive FULL materialize (M+1 cols)", s_mat,
                  (double)M / (s_mat.min * 1e-3), "iter/s", bytes_mat, err_m, ok, "", bytes_mat);
    hb::print_row("non-inductive mod-3 ring (3 cols)", s_ring,
                  (double)M / (s_ring.min * 1e-3), "iter/s", bytes_ring, err_n, ok,
                  hb::verdict(s_ring.min, s_mat.min), bytes_mat);
    hb::print_row("inductive UNFOLDED (fold -> M+1 cols)", s_unf,
                  (double)M / (s_unf.min * 1e-3), "iter/s", bytes_unf, err_u, ok,
                  hb::verdict(s_unf.min, s_mat.min), bytes_mat);
    hb::print_row("inductive FOLDED (fold -> 3 cols)", s_ind,
                  (double)M / (s_ind.min * 1e-3), "iter/s", bytes_ind, err_i, ok,
                  hb::verdict(s_ind.min, s_unf.min), bytes_mat);

    if (!ok) {
        printf("Chebyshev solvers disagree or did not converge!\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
