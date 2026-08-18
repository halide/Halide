// FlashAttention in Halide using inductive functions.
//
// Standard attention:  O = softmax(Q K^T / sqrt(d)) V
//
// FlashAttention tiles the K/V sequence dimension and combines tiles using
// the online-softmax recurrence, so the O(N^2) score matrix is never
// materialized.  Per query i we carry three pieces of state across tiles t:
//
//   m(i, t)  running maximum of the scores seen through tile t
//   l(i, t)  running softmax normalizer (rescaled sum of exp(score - m))
//   acc(d,i) running weighted-value accumulator
//
// m and l are Halide *inductive* functions (Tutorial 25): a select() with a
// base case for t<=0 and a likely() recursive step that reads the previous
// tile.  When the running max grows from m(i,t-1) to m(i,t), l rescales its
// old value by exp(m_old - m_new).  acc is an ordinary RDom scan over tiles
// (see below) that reads m and l.
//
// Notes on inductive funcs:
//   * They need an explicit element type: Func f = Func(Float(32), "f").
//   * They cannot be pipeline outputs, so out_f wraps acc.
//
// Compile and run:
//   g++ flash_attention.cpp -g -I <halide>/include -L <halide>/lib \
//       -lHalide -lpthread -ldl -o flash_attention -std=c++17
//   LD_LIBRARY_PATH=<halide>/lib ./flash_attention

#include "Halide.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace Halide;

// ---------------------------------------------------------------------------
// Reference: naive O(N^2) attention
// ---------------------------------------------------------------------------
static void naive_attention(const std::vector<float> &Q,
                            const std::vector<float> &K,
                            const std::vector<float> &V,
                            std::vector<float> &out,
                            int N, int D) {
    std::vector<float> s(N);
    float scale = 1.f / std::sqrt((float)D);
    for (int i = 0; i < N; i++) {
        float mx = -1e30f;
        for (int j = 0; j < N; j++) {
            float dot = 0;
            for (int k = 0; k < D; k++)
                dot += Q[k + i * D] * K[k + j * D];
            s[j] = dot * scale;
            mx = std::max(mx, s[j]);
        }
        float lsum = 0;
        for (int j = 0; j < N; j++) { s[j] = std::exp(s[j] - mx); lsum += s[j]; }
        for (int dd = 0; dd < D; dd++) {
            float v = 0;
            for (int j = 0; j < N; j++) v += s[j] * V[dd + j * D];
            out[dd + i * D] = v / lsum;
        }
    }
}

// ---------------------------------------------------------------------------
// FlashAttention via Halide inductive functions
// ---------------------------------------------------------------------------
static bool flash_attention_halide(int N, int D, int tile_size) {
    assert(N % tile_size == 0);
    const int num_tiles = N / tile_size;

    Var i("i"),   // query index
        j("j"),   // position within a K/V tile, [0, tile_size)
        d("d"),   // head-dimension index
        t("t");   // tile index (the inductive axis)

    ImageParam Q_p(Float(32), 2, "Q_p");
    ImageParam K_p(Float(32), 2, "K_p");
    ImageParam V_p(Float(32), 2, "V_p");

    float scale = 1.f / std::sqrt((float)D);
    RDom rd(0, D, "rd");
    Func score("score");
    score(j, i, t) = sum(Q_p(rd, i) * K_p(rd, t * tile_size + j)) * scale;

    RDom rj_max(0, tile_size, "rj_max");
    Func tile_max("tile_max");
    tile_max(i, t) = maximum(score(rj_max, i, t));

    Func m = Func(Float(32), "m");
    m(i, t) = select(t <= 0,
                     tile_max(i, 0),
                     likely(max(m(i, t - 1), tile_max(i, t))));

    Func w("w");
    w(j, i, t) = exp(score(j, i, t) - m(i, t));

    RDom rj(0, tile_size, "rj");
    Func tile_l("tile_l");
    tile_l(i, t) = sum(w(rj, i, t));

    Func tile_acc("tile_acc");
    tile_acc(d, i, t) = sum(w(rj, i, t) * V_p(d, t * tile_size + rj));

    Func l = Func(Float(32), "l");
    l(i, t) = select(t <= 0,
                     tile_l(i, t),
                     likely(l(i, t - 1) * exp(m(i, t - 1) - m(i, t)) + tile_l(i, t)));

    Func acc("acc");
    acc(d, i) = 0.f;
    RDom rt(0, num_tiles, "rt");
    acc(d, i) = (acc(d, i) * exp(m(i, max(rt - 1, 0)) - m(i, rt)) + tile_acc(d, i, rt))/select(rt<num_tiles-1, 1, l(i,rt));

    Func out_f("out_f");
    out_f(d, i) = acc(d, i);// / l(i, num_tiles - 1);

    const int q_block = 4;
    Var io("io"), ii("ii");
    out_f.compute_root().split(i, io, ii, q_block);
    acc.compute_at(out_f, io);
    acc.update().reorder(d, rt);
    tile_max.compute_at(acc, rt);
    tile_acc.compute_at(acc, rt);
    m.compute_at(acc, rt).store_root();
    l.compute_at(acc, rt).store_root();

    // -----------------------------------------------------------------------
    // Random inputs
    // -----------------------------------------------------------------------
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);

    Buffer<float> Q_buf(D, N), K_buf(D, N), V_buf(D, N);
    std::vector<float> Q_v(N * D), K_v(N * D), V_v(N * D);
    for (int ii = 0; ii < N; ii++)
        for (int dd = 0; dd < D; dd++) {
            float q = dist(rng), k = dist(rng), v = dist(rng);
            Q_buf(dd, ii) = q;  K_buf(dd, ii) = k;  V_buf(dd, ii) = v;
            Q_v[dd + ii * D] = q;
            K_v[dd + ii * D] = k;
            V_v[dd + ii * D] = v;
        }

    Q_p.set(Q_buf);
    K_p.set(K_buf);
    V_p.set(V_buf);

    Buffer<float> result = out_f.realize({D, N});

    // -----------------------------------------------------------------------
    // Compare against reference
    // -----------------------------------------------------------------------
    std::vector<float> ref(N * D);
    naive_attention(Q_v, K_v, V_v, ref, N, D);

    float max_err = 0;
    for (int ii = 0; ii < N; ii++)
        for (int dd = 0; dd < D; dd++)
            max_err = std::max(max_err, std::abs(result(dd, ii) - ref[dd + ii * D]));

    printf("N=%d D=%d tile=%d  max_err=%.6e  %s\n",
           N, D, tile_size, max_err,
           max_err < 1e-4f ? "PASS" : "FAIL");
    return max_err < 1e-4f;
}

int main() {
    bool ok = true;
    try {
        ok &= flash_attention_halide(/*N=*/16, /*D=*/8,  /*tile=*/4);
        ok &= flash_attention_halide(/*N=*/32, /*D=*/16, /*tile=*/8);
        ok &= flash_attention_halide(/*N=*/64, /*D=*/32, /*tile=*/16);
        ok &= flash_attention_halide(/*N=*/64, /*D=*/32, /*tile=*/32);
    } catch (const Halide::Error &e) {
        fprintf(stderr, "Halide::Error: %s\n", e.what());
        return 1;
    }
    return ok ? 0 : 1;
}
