// Log-domain Viterbi decoding -- the standardized inductive-vs-non-inductive
// benchmark. Both variants share the exact recurrence structure of viterbi.cpp
// (the 2-D RDom r over output state r.x and previous state r.y, running-max of
// prob(r.y, t-1) + log_trans + log_emit; prev is a plain RDom argmax); they
// differ ONLY in how the time axis is expressed and stored:
//
//   INDUCTIVE     : t is a pure Var, prob is inductive in t (select(t<=0,...) +
//                   likely), storage folds to 2 time-slices (O(S*2)).
//   NON-INDUCTIVE : t is an explicit RDom scan (rt in 1..T-1), the whole
//                   trajectory is materialized (O(S*T)).
//
// Log domain (sum of logs + max) rather than products, so long sequences don't
// underflow float32 -- what librosa does internally too.

#include "Halide.h"

#include "../support/bench_harness.h"
#include "../support/jit_support.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

using namespace Halide;

int main(int argc, char **argv) {
    int S = argc > 1 ? atoi(argv[1]) : 4;      // hidden states (S=4: DNA-style HMM)
    int M = argc > 2 ? atoi(argv[2]) : 3;      // emission alphabet size
    int T = argc > 3 ? atoi(argv[3]) : 20000;  // observation-sequence length
    const char *data_path = argc > 4 ? argv[4] : "/tmp/viterbi_bench_data.bin";

    try {
        Var s("s"), t("t");

        Buffer<float> init(S), trans(S, S), emit(S, M);
        Buffer<int> obs(T);
        for (int i = 0; i < S; i++)
            init(i) = 1.0f / S;
        for (int r = 0; r < S; r++) {
            float row_sum = 0;
            for (int c = 0; c < S; c++) {
                trans(r, c) = 1 + ((r + 1) * (c + 2)) % 5;
                row_sum += trans(r, c);
            }
            for (int c = 0; c < S; c++)
                trans(r, c) /= row_sum;
        }
        for (int st = 0; st < S; st++) {
            float row_sum = 0;
            for (int o = 0; o < M; o++) {
                emit(st, o) = 1 + ((st + 1) * (o + 3)) % 4;
                row_sum += emit(st, o);
            }
            for (int o = 0; o < M; o++)
                emit(st, o) /= row_sum;
        }
        for (int i = 0; i < T; i++)
            obs(i) = (i * 2 + 1) % M;

        Buffer<float> log_init(S), log_trans(S, S), log_emit(S, M);
        for (int i = 0; i < S; i++)
            log_init(i) = std::log(init(i));
        for (int a = 0; a < S; a++)
            for (int b = 0; b < S; b++)
                log_trans(a, b) = std::log(trans(a, b));
        for (int st = 0; st < S; st++)
            for (int o = 0; o < M; o++)
                log_emit(st, o) = std::log(emit(st, o));

        const float neg_inf = -std::numeric_limits<float>::infinity();
        Expr obs_0 = clamp(obs(0), 0, M - 1);

        // Build one variant; returns the decoded `path` Func. fold_k pins the
        // prob storage window when inductive (2 = folded, T+1 = unfolded ablation).
        auto build = [&](bool inductive, int fold_k = 2) -> Func {
            RDom r(0, S, 0, S, "r");  // r.x = output state, r.y = previous state
            RDom r2(0, S, "r2"), r3(0, S, "r3");
            Expr obs_t = Halide::Internal::promise_clamped(
                obs(Halide::Internal::promise_clamped(t, 0, T - 1)), 0, M - 1);

            Func prob(Float(32), 2, inductive ? "prob_i" : "prob_n");
            prob(s, t) = neg_inf;

            auto normal_cand = [&](Expr idx, Expr idx2) {
                return likely(prob(idx, t - 1) + log_trans(idx2, idx) + log_emit(idx2, obs_t));
            };

            if (inductive) {
                // Inductive in t: pure Var t, base case at t<=0, running max over r.
                prob(r.x, t) = select(t <= 0,
                                      log_init(r.x) + log_emit(r.x, obs_0),
                                      likely(max(prob(r.x, t), normal_cand(r.y, r.x))));
                prob.update(0).allow_race_conditions().vectorize(r.x);
            } else {
                // Non-inductive: explicit init at t=0, then an RDom scan over time.
                RDom ri(0, S, "ri");
                prob(ri, 0) = log_init(ri) + log_emit(ri, obs_0);
                RDom rr(0, S, 0, S, 1, T - 1, "rr");  // rr.x=state, rr.y=prev, rr.z=time
                Expr ot = clamp(obs(rr.z), 0, M - 1);
                Expr candn = prob(rr.y, rr.z - 1) + log_trans(rr.x, rr.y) + log_emit(rr.x, ot);
                prob(rr.x, rr.z) = max(prob(rr.x, rr.z), candn);
                prob.update(0).vectorize(ri);
                prob.update(1).allow_race_conditions().vectorize(rr.x);
            }

            // prev: plain RDom argmax (same in both variants; reads prob only).
            Expr is_terminal = (t >= T);
            Expr terminal_cand = prob(r2, t - 1);
            Expr cand2 = select(is_terminal, terminal_cand, normal_cand(r2, s));
            Expr row_max = maximum(r3, prob(r3, t - 1));
            Expr compare_val = select(is_terminal, row_max, prob(s, t));

            Func prev(Int(32), 2, inductive ? "prev_i" : "prev_n");
            prev(s, t) = S;
            prev(s, t) = select(t <= 0, 0, likely(select(cand2 == compare_val, r2, likely(prev(s, t)))));

            Func path(Int(32), 1, inductive ? "path_i" : "path_n");
            path(t) = undef<int>();
            path(T - 1) = prev(0, T);
            RDom rt(1, T - 1, "rt");
            path(T - 1 - rt) = prev(clamp(path(T - rt), 0, S - 1), T - rt);

            prev.bound(s, 0, S).bound(t, 0, T + 1);
            if (inductive) {
                prob.compute_at(prev, t).store_root().fold_storage(t, fold_k);
            } else {
                prob.compute_root();
            }
            prob.vectorize(s);
            prev.vectorize(s).update().vectorize(s);
            prev.compute_root();
            path.compute_root();
            return path;
        };

        Func path_n = build(false), path_u = build(true, T + 1), path_i = build(true, 2);
        for (Func *f : {&path_n, &path_u, &path_i}) {
            hb::reuse_jit_allocations(*f);
        }
        Buffer<int> res_i(T), res_u(T), res_n(T);
        path_i.realize(res_i);  // warm/JIT
        path_u.realize(res_u);
        path_n.realize(res_n);
        hb::Stats sn = hb::bench([&] { path_n.realize(res_n); });
        hb::Stats su = hb::bench([&] { path_u.realize(res_u); });
        hb::Stats si = hb::bench([&] { path_i.realize(res_i); });

        // Measured peak internal scratch (untimed, separate from the benches above).
        double bytes_non = hb::profiled_peak_bytes(path_n, res_n);
        double bytes_unf = hb::profiled_peak_bytes(path_u, res_u);
        double bytes_ind = hb::profiled_peak_bytes(path_i, res_i);

        // C++ log-domain reference (last-index argmax, matching Halide's prev).
        std::vector<std::vector<float>> rv(T, std::vector<float>(S));
        std::vector<std::vector<int>> rp(T, std::vector<int>(S, 0));
        for (int st = 0; st < S; st++)
            rv[0][st] = log_init(st) + log_emit(st, obs(0));
        for (int tt = 1; tt < T; tt++)
            for (int st = 0; st < S; st++) {
                float best = neg_inf;
                int best_r = 0;
                for (int rr = 0; rr < S; rr++) {
                    float v = rv[tt - 1][rr] + log_trans(st, rr) + log_emit(st, obs(tt));
                    if (v >= best) {
                        best = v;
                        best_r = rr;
                    }
                }
                rv[tt][st] = best;
                rp[tt][st] = best_r;
            }
        std::vector<int> ref_path(T);
        {
            int best_r = 0;
            float best = neg_inf;
            for (int st = 0; st < S; st++)
                if (rv[T - 1][st] >= best) {
                    best = rv[T - 1][st];
                    best_r = st;
                }
            ref_path[T - 1] = best_r;
        }
        for (int tt = T - 2; tt >= 0; tt--)
            ref_path[tt] = rp[tt + 1][ref_path[tt + 1]];

        auto count_mismatch = [&](Buffer<int> &res) {
            int m = 0; for (int tt = 0; tt < T; tt++) if (res(tt) != ref_path[tt]) m++; return m; };
        int mi = count_mismatch(res_i), mu = count_mismatch(res_u), mn = count_mismatch(res_n);

        // Correctness is on the path SCORE (total log-probability), not the index
        // sequence: with degenerate synthetic emission/transition tables (large S)
        // the optimal Viterbi path is non-unique, so an equally-optimal decode is a
        // valid answer even if its indices differ. Compare each decoded path's
        // realized log-prob to the reference optimum.
        auto path_score = [&](auto &&at) {
            double sc = log_init(at(0)) + log_emit(at(0), obs(0));
            for (int tt = 1; tt < T; tt++)
                sc += log_trans(at(tt), at(tt - 1)) + log_emit(at(tt), obs(tt));
            return sc;
        };
        // Rescore the reference path with the SAME double routine so the optimality
        // check compares like-for-like (no float-vs-double drift over T steps).
        double opt = path_score([&](int tt) { return ref_path[tt]; });
        // Optimal Viterbi paths are non-unique under degenerate ties, but every
        // optimal path has the same total log-prob; a decode is correct iff its
        // score is >= the reference optimum (up to per-step float rounding).
        double tol = 1e-3 * T;
        auto score_gap = [&](Buffer<int> &res) { return opt - path_score([&](int tt) { return res(tt); }); };
        double gi = score_gap(res_i), gu = score_gap(res_u), gn = score_gap(res_n);
        bool oki = gi <= tol, oku = gu <= tol, okn = gn <= tol;

        // Roofline x-axis: unfolded footprint (backpointer trajectory, S*(T+1)
        // states) / LLC. Recurrence-length (T) and state-count (S) sweeps collapse
        // onto fp/LLC. Dimension roles: S = per-step work (transition scan width),
        // T = recurrence length (folded axis).
        const double fp_unfold = (double)bytes_non;
        char note[160];
        snprintf(note, sizeof(note),
                 "Viterbi decode (log domain)  S=%d M=%d T=%d  |  state fold %.0fx  |  unfolded fp/LLC=%.3f",
                 S, M, T, hb::mem_ratio(bytes_non, bytes_ind), hb::footprint_over_llc(fp_unfold));
        hb::print_spec_header("viterbi_log", "host", note);
        // check column reports the score gap vs the optimum (0 = optimal path);
        // the index-mismatch count is printed below for reference.
        hb::print_row("non-inductive (materialize)", sn, (S * (double)T) / (sn.min * 1e3),
                      "Mstates/s", bytes_non, gn, okn, "", fp_unfold);
        hb::print_row("inductive UNFOLDED (fold t -> T+1)", su, (S * (double)T) / (su.min * 1e3),
                      "Mstates/s", bytes_unf, gu, oku, hb::verdict(su.min, sn.min), fp_unfold);
        hb::print_row("inductive FOLDED (fold t -> 2)", si, (S * (double)T) / (si.min * 1e3),
                      "Mstates/s", bytes_ind, gi, oki, hb::verdict(si.min, su.min), fp_unfold);
        printf("  path-index mismatches vs reference (ties => non-unique optimum): "
               "non=%d unfold=%d fold=%d\n",
               mn, mu, mi);

        // Dump the inductive path for the librosa comparison script.
        std::vector<int32_t> po(T);
        for (int i = 0; i < T; i++)
            po[i] = res_i(i);
        FILE *f = fopen((std::string(data_path) + ".path").c_str(), "wb");
        if (f) {
            fwrite(po.data(), sizeof(int32_t), T, f);
            fclose(f);
        }

        return (oki && oku && okn) ? 0 : 1;
    } catch (const Halide::Error &e) {
        fprintf(stderr, "EXCEPTION: %s\n", e.what());
        return 1;
    }
}
