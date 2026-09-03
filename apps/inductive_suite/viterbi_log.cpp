// Log-domain Viterbi decoding: the inductive form of the recurrence against
// its RDom form. Both share the 2-D RDom r over output state r.x and previous
// state r.y (running max of score(r.y, t-1) + log_trans + log_emit, with the
// argmax carried alongside as a backpointer); they differ only in how the
// time axis is expressed and stored:
//
//   INDUCTIVE : t is a pure Var, the scores are inductive in t (select(t<=0,
//               ...) + likely) and their storage folds to two time slices.
//   RDom      : t is an explicit RDom scan over 1..T, and the whole
//               trajectory of scores is materialized.
//
// The scores are rescaled every step by subtracting state 0's score of the
// previous step, so they stay O(1) in float over any sequence length; the
// argmax at each step, and so the decoded path, is unchanged by a per-step
// constant.
//
// The HMM is random and seeded: log transition and log emission tables from
// uniform random probabilities with normalized rows, and an observation
// sequence sampled from that HMM. The decoded paths are checked exactly
// against a double-precision decode, except where the reference's own
// decision is a near-tie, and each path's log-probability is checked
// against the reference optimum.

#include "Halide.h"

#include "../support/bench_harness.h"
#include "../support/jit_support.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace Halide;

int main(int argc, char **argv) {
    int S = argc > 1 ? atoi(argv[1]) : 4;      // hidden states
    int M = argc > 2 ? atoi(argv[2]) : 3;      // emission alphabet size
    int T = argc > 3 ? atoi(argv[3]) : 20000;  // observation-sequence length

    // A reference decision whose best and second-best candidates are within
    // this many nats is a near-tie: the float decode may take either branch.
    const double tie_tol = 1e-6;
    // The decoded path's log-probability must be within this relative
    // distance of the reference optimum.
    const double score_tol = 1e-5;

    try {
        Var s("s"), t("t");

        // Random HMM. trans(out, in) is P(out | in), normalized over out for
        // each previous state in; emit(st, o) is P(o | st), normalized over o.
        std::mt19937 rng(12345);
        std::uniform_real_distribution<float> uni(0.0f, 1.0f);
        Buffer<float> init(S), trans(S, S), emit(S, M);
        for (int i = 0; i < S; i++)
            init(i) = 1.0f / S;
        for (int in = 0; in < S; in++) {
            float col_sum = 0;
            for (int out = 0; out < S; out++) {
                trans(out, in) = uni(rng) + 1e-3f;
                col_sum += trans(out, in);
            }
            for (int out = 0; out < S; out++)
                trans(out, in) /= col_sum;
        }
        for (int st = 0; st < S; st++) {
            float row_sum = 0;
            for (int o = 0; o < M; o++) {
                emit(st, o) = uni(rng) + 1e-3f;
                row_sum += emit(st, o);
            }
            for (int o = 0; o < M; o++)
                emit(st, o) /= row_sum;
        }

        // The observations: a state sequence walked from the HMM, each state
        // emitting one symbol.
        Buffer<int> obs(T);
        {
            auto draw = [&](auto prob, int n) {
                float u = uni(rng), acc = 0;
                for (int k = 0; k < n; k++) {
                    acc += prob(k);
                    if (u < acc) return k;
                }
                return n - 1;
            };
            int state = draw([&](int k) { return init(k); }, S);
            for (int i = 0; i < T; i++) {
                obs(i) = draw([&](int k) { return emit(state, k); }, M);
                state = draw([&](int k) { return trans(k, state); }, S);
            }
        }

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
        // score storage window when inductive (2 = folded, T+1 = unfolded ablation).
        auto build = [&](bool inductive, int fold_k = 2) -> Func {
            // The walk carries {best score, argmax} per state as one Tuple:
            // every candidate is computed once, and the compare that keeps
            // the larger score is the compare that keeps its index (>= so
            // ties go to the last index, as the reference does). Step T is
            // the terminal argmax over the final scores, with no transition
            // or emission, so the backpointer plane's last row starts the
            // traceback.
            // The output state is an RVar, not a pure Var, in both forms: an
            // update may only refer to its own Func with the pure Vars in the
            // positions it writes them, and this one reads the previous state
            // in the output state's slot (the inductive classifier makes the
            // same objection, since a read at another position along a pure
            // Var would make that Var inductive). Vectorizing over it is
            // race-free, each lane writing its own state, but has to be waved
            // through.
            RDom r(0, S, 0, S, "r");  // r.x = output state, r.y = previous state
            Expr obs_t = Halide::Internal::promise_clamped(
                obs(Halide::Internal::promise_clamped(t, 0, T - 1)), 0, M - 1);

            // The argmax is a byte: it indexes the states, and the backpointer
            // plane, one per state per step, is what the traceback reads.
            Func state({Float(32), UInt(8)}, 2, inductive ? "state_i" : "state_n");
            state(s, t) = {neg_inf, cast<uint8_t>(0)};

            // A candidate for output state `out` from previous state `in` at
            // step `step`: the previous score, rescaled by state 0's score of
            // the same previous step, plus the transition and emission.
            auto candidate = [&](Expr prev_score, Expr pivot, Expr out, Expr in, Expr step) {
                return prev_score - pivot + select(step >= T, 0.f, log_trans(out, in)) +
                       select(step >= T, 0.f, log_emit(out, obs_t));
            };

            if (inductive) {
                // Inductive in t: pure Var t, base case at t<=0, argmax over r.
                Expr cand = candidate(state(r.y, t - 1)[0], state(0, t - 1)[0], r.x, r.y, t);
                Expr cur = state(r.x, t)[0];
                Tuple step = select(cand >= cur, Tuple(cand, cast<uint8_t>(r.y)), state(r.x, t));
                state(r.x, t) = select(t <= 0,
                                       Tuple(log_init(r.x) + log_emit(r.x, obs_0), cast<uint8_t>(0)),
                                       Tuple(likely(step[0]), likely(step[1])));
                state.update(0).allow_race_conditions().vectorize(r.x);
            } else {
                // RDom form: explicit init at t=0, then an RDom scan over time.
                RDom ri(0, S, "ri");
                state(ri, 0) = {log_init(ri) + log_emit(ri, obs_0), cast<uint8_t>(0)};
                RDom rr(0, S, 0, S, 1, T, "rr");  // rr.x=state, rr.y=prev, rr.z=time
                Expr ot = clamp(obs(min(rr.z, T - 1)), 0, M - 1);
                Expr prev = state(rr.y, rr.z - 1)[0] - state(0, rr.z - 1)[0];
                Expr candn = select(rr.z >= T, prev, prev + log_trans(rr.x, rr.y) + log_emit(rr.x, ot));
                state(rr.x, rr.z) = select(candn >= state(rr.x, rr.z)[0],
                                           Tuple(candn, cast<uint8_t>(rr.y)), state(rr.x, rr.z));
                state.update(0).vectorize(ri);
                state.update(1).allow_race_conditions().vectorize(rr.x);
            }

            // The backpointers, materialized for the traceback: a copy of the
            // argmax component, one row per step, while the scores fold.
            Func prev(UInt(8), 2, inductive ? "prev_i" : "prev_n");
            prev(s, t) = state(s, t)[1];

            Func path(Int(32), 1, inductive ? "path_i" : "path_n");
            path(t) = undef<int>();
            path(T - 1) = cast<int>(prev(0, T));
            RDom rt(1, T - 1, "rt");
            path(T - 1 - rt) = cast<int>(prev(clamp(path(T - rt), 0, S - 1), T - rt));

            prev.bound(s, 0, S).bound(t, 0, T + 1);
            if (inductive) {
                state.compute_at(prev, t).store_root().fold_storage(t, fold_k);
            } else {
                state.compute_root();
            }
            state.vectorize(s);
            prev.vectorize(s);
            prev.compute_root();
            path.compute_root();
            return path;
        };

        Func path_n = build(false), path_u = build(true, T + 1), path_i = build(true, 2);
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

        // Double-precision reference decode, with the same per-step rescaling
        // and the same last-index argmax. One forward pass records the
        // backpointers (a byte per state per step) for the traceback; a
        // second, with the path known, records the margin of each decision
        // on it: best minus second-best candidate, at (t+1, path[t+1]) for
        // path[t], and at the terminal argmax for path[T-1].
        std::vector<uint8_t> rp((size_t)T * S, 0);
        std::vector<double> prev_row(S), cur_row(S);
        std::vector<int> ref_path(T);
        std::vector<float> margin(T);
        auto forward = [&](auto on_decision) {
            for (int st = 0; st < S; st++)
                prev_row[st] = (double)log_init(st) + (double)log_emit(st, obs(0));
            for (int tt = 1; tt < T; tt++) {
                const double pivot = prev_row[0];
                for (int st = 0; st < S; st++) {
                    double best = -std::numeric_limits<double>::infinity(), second = best;
                    int best_r = 0;
                    for (int rr = 0; rr < S; rr++) {
                        double v = prev_row[rr] - pivot + (double)log_trans(st, rr) + (double)log_emit(st, obs(tt));
                        if (v >= best) {
                            second = best;
                            best = v;
                            best_r = rr;
                        } else if (v > second) {
                            second = v;
                        }
                    }
                    cur_row[st] = best;
                    on_decision(tt, st, best_r, best - second);
                }
                std::swap(prev_row, cur_row);
            }
            double best = -std::numeric_limits<double>::infinity(), second = best;
            int best_r = 0;
            for (int st = 0; st < S; st++) {
                if (prev_row[st] >= best) {
                    second = best;
                    best = prev_row[st];
                    best_r = st;
                } else if (prev_row[st] > second) {
                    second = prev_row[st];
                }
            }
            on_decision(T, 0, best_r, best - second);
        };
        forward([&](int tt, int st, int best_r, double) {
            if (tt < T) rp[(size_t)tt * S + st] = (uint8_t)best_r;
            else ref_path[T - 1] = best_r;
        });
        for (int tt = T - 2; tt >= 0; tt--)
            ref_path[tt] = rp[(size_t)(tt + 1) * S + ref_path[tt + 1]];
        forward([&](int tt, int st, int, double gap) {
            if (tt == T) margin[T - 1] = (float)gap;
            else if (st == ref_path[tt]) margin[tt - 1] = (float)gap;
        });
        int near_ties = 0;
        for (int tt = 0; tt < T; tt++)
            near_ties += margin[tt] <= tie_tol;

        // A decoded path is compared position by position, from the end. A
        // divergence at a decision the reference made by less than tie_tol
        // is excused, and so are the positions before it until the two paths
        // rejoin; any other divergence is an error.
        struct Mismatch {
            int positions = 0, excused = 0, unexcused = 0;
        };
        auto compare = [&](Buffer<int> &res) {
            Mismatch m;
            bool diverged = false;
            for (int tt = T - 1; tt >= 0; tt--) {
                if (res(tt) == ref_path[tt]) {
                    diverged = false;
                    continue;
                }
                m.positions++;
                if (!diverged) {
                    if (margin[tt] <= tie_tol) m.excused++;
                    else m.unexcused++;
                    diverged = true;
                }
            }
            return m;
        };
        Mismatch mi = compare(res_i), mu = compare(res_u), mn = compare(res_n);

        // The log-probability of a decoded path, against the reference optimum.
        auto path_score = [&](auto &&at) {
            double sc = (double)log_init(at(0)) + (double)log_emit(at(0), obs(0));
            for (int tt = 1; tt < T; tt++)
                sc += (double)log_trans(at(tt), at(tt - 1)) + (double)log_emit(at(tt), obs(tt));
            return sc;
        };
        double opt = path_score([&](int tt) { return ref_path[tt]; });
        auto score_gap = [&](Buffer<int> &res) {
            return (opt - path_score([&](int tt) { return res(tt); })) / std::abs(opt);
        };
        double gi = score_gap(res_i), gu = score_gap(res_u), gn = score_gap(res_n);
        bool oki = mi.unexcused == 0 && gi <= score_tol;
        bool oku = mu.unexcused == 0 && gu <= score_tol;
        bool okn = mn.unexcused == 0 && gn <= score_tol;

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
        // The err column is the relative gap of the path's log-probability
        // from the optimum; the exact path comparison is printed below.
        hb::print_row("non-inductive (materialize)", sn, (S * (double)T) / (sn.min * 1e3),
                      "Mstates/s", bytes_non, gn, okn, "", fp_unfold);
        hb::print_row("inductive UNFOLDED (fold t -> T+1)", su, (S * (double)T) / (su.min * 1e3),
                      "Mstates/s", bytes_unf, gu, oku, hb::verdict(su.min, sn.min), fp_unfold);
        hb::print_row("inductive FOLDED (fold t -> 2)", si, (S * (double)T) / (si.min * 1e3),
                      "Mstates/s", bytes_ind, gi, oki, hb::verdict(si.min, su.min), fp_unfold);
        printf("  reference decisions on the path within %.0e nats of a tie: %d of %d\n",
               tie_tol, near_ties, T);
        auto report = [&](const char *name, const Mismatch &m) {
            printf("  path vs double reference, %s: %d positions differ; divergences: %d excused (near-tie), %d unexcused\n",
                   name, m.positions, m.excused, m.unexcused);
        };
        report("non", mn);
        report("unfold", mu);
        report("fold", mi);

        return (oki && oku && okn) ? 0 : 1;
    } catch (const Halide::Error &e) {
        fprintf(stderr, "EXCEPTION: %s\n", e.what());
        return 1;
    }
}
