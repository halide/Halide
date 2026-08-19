// FlashAttention on the tensor cores, following the structure of
// flash_attention.cpp: the running maximum and the running normalizer are
// inductive Funcs over the tile axis, and the accumulator is an ordinary scan
// that reads them. A block owns a strip of queries and walks the key tiles,
// carrying everything in tensor core fragments.
//
// This does not compile yet, but everything except the carried state does.
// The scores, the two row reductions and both matrix multiplies all land in
// fragments, one key tile per step.
//
// What is left is the state itself. The running maximum is read by the scan at
// both rt - 1 and rt, so two tiles of it are live, and storage folding gives it
// two slots indexed by t % 2. A fragment lives in registers, which cannot be
// indexed at run time, so that index has to be a constant. The two ways of
// getting one both cost more than they give:
//
//  - Rolling the state between iterations rather than folding it, which is what
//    MemoryType::Register does, keeps the region written full width by design
//    and relies on unrolling to make the copies free. That widens the window of
//    every producer back to the whole prefix.
//  - Unrolling the scan by two makes both t % 2 and (t - 1) % 2 constants, but
//    the pair of steps then spans two key tiles, which widens the producers the
//    same way and asks for a tile before the first.
//
// Reading a fragment at a folded index needs to become a choice between the
// slots, made where the code is generated rather than at run time.

#include "Halide.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace Halide;

static void naive_attention(const Buffer<float16_t> &Q, const Buffer<float16_t> &K,
                            const Buffer<float16_t> &V, std::vector<float> &out,
                            int N, int D) {
    std::vector<float> s(N);
    float scale = 1.f / std::sqrt((float)D);
    for (int i = 0; i < N; i++) {
        float mx = -1e30f;
        for (int j = 0; j < N; j++) {
            float dot = 0;
            for (int k = 0; k < D; k++) dot += (float)Q(k, i) * (float)K(k, j);
            s[j] = dot * scale;
            mx = std::max(mx, s[j]);
        }
        float lsum = 0;
        for (int j = 0; j < N; j++) { s[j] = std::exp(s[j] - mx); lsum += s[j]; }
        for (int dd = 0; dd < D; dd++) {
            float v = 0;
            for (int j = 0; j < N; j++) v += s[j] * (float)V(dd, j);
            out[dd + i * D] = v / lsum;
        }
    }
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::CUDA)) {
        printf("[SKIP] WMMA operations require CUDA.\n");
        return 0;
    }
    if (target.get_cuda_capability_lower_bound() < 70) {
        printf("[SKIP] WMMA operations require CUDA compute capability 7.0 or above.\n");
        return 0;
    }

    const int tile = 16;
    const int N = 128, D = 32, TS = 32;
    const int num_tiles = N / TS;
    const float scale = 1.f / std::sqrt((float)D);

    Buffer<float16_t> Q(D, N), K(D, N), V(D, N);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    for (int i = 0; i < N; i++) {
        for (int dd = 0; dd < D; dd++) {
            Q(dd, i) = float16_t(dist(rng));
            K(dd, i) = float16_t(dist(rng));
            V(dd, i) = float16_t(dist(rng));
        }
    }
    Q.set_host_dirty();
    K.set_host_dirty();
    V.set_host_dirty();

    Var d("d"), i("i"), j("j"), t("t");
    RDom rd(0, D, "rd"), rj_max(0, TS, "rj_max"), rj(0, TS, "rj");
    Func score("score"), tile_max("tile_max"), w("w"), tile_l("tile_l");
    Func tile_acc("tile_acc"), acc("acc"), out("out");
    Func m = Func(Float(32), "m"), l = Func(Float(32), "l");

    // One key tile's scores. The scale is left off here and applied inside the
    // exponentials, which is the same answer and keeps the maxima comparable.
    score(j, i, t) = 0.f;
    score(j, i, t) += cast<float>(Q(rd, i)) * cast<float>(K(rd, t * TS + j));

    tile_max(i, t) = -1e30f;
    tile_max(i, t) = max(tile_max(i, t), score(rj_max, i, t));

    // The running maximum depends on nothing but itself. Asked for one tile
    // before the first, it gives the same answer as the first, so the rescaling
    // below is by exp(0) on the step that has nothing to rescale.
    m(i, t) = select(t <= 0, tile_max(i, t),
                     likely(max(m(i, t - 1), tile_max(i, t))));

    // Weights are taken against the running maximum, so what arrives is
    // already on the right scale and only what is carried needs rescaling.
    w(j, i, t) = exp((score(j, i, t) - m(i, t)) * scale);

    tile_l(i, t) = 0.f;
    tile_l(i, t) += w(rj, i, t);

    l(i, t) = select(t <= 0, tile_l(i, t),
                     likely(l(i, t - 1) * exp((m(i, t - 1) - m(i, t)) * scale) +
                            tile_l(i, t)));

    tile_acc(d, i, t) = 0.f;
    tile_acc(d, i, t) += cast<float>(w(rj, i, t)) * cast<float>(V(d, t * TS + rj));

    RDom rt(0, num_tiles, "rt");
    acc(d, i) = 0.f;
    // Normalising rides along on the last step, which is also what makes l a
    // producer of acc and so gives it a loop to be computed in.
    acc(d, i) = (acc(d, i) * exp((m(i, max(rt - 1, 0)) - m(i, rt)) * scale) +
                 tile_acc(d, i, rt)) /
                select(rt < num_tiles - 1, 1.f, l(i, rt));

    out(d, i) = acc(d, i);

    Var dof("do"), io("io"), dio("dio"), iio("iio"), di("di"), ii("ii");
    Var rdi("rdi"), rii("rii");
    RVar rro("rro"), rri("rri");

    // A block owns a strip of queries and every column of the output.
    out.bound(d, 0, D)
        .bound(i, 0, N)
        .tile(d, i, dof, io, di, ii, D, tile)
        .tile(di, ii, dio, iio, di, ii, tile, tile)
        .gpu_blocks(dof, io)
        .unroll(dio)
        .unroll(iio)
        .tile_store(di, ii);

    acc.compute_at(out, dof)
        .store_in(MemoryType::Tile)
        .tile(d, i, rdi, rii, tile, tile)
        .unroll(d)
        .unroll(i)
        .tile_init(rdi, rii);
    acc.update()
        .tile(d, i, rdi, rii, tile, tile)
        .reorder(d, i, rt)
        .unroll(d)
        .unroll(i)
        .tile_init(rdi, rii);

    for (Func f : {score, w}) {
        f.compute_at(acc, rt)
            .store_in(MemoryType::Tile)
            .tile(j, i, rdi, rii, tile, tile)
            .unroll(j)
            .unroll(i)
            .tile_init(rdi, rii);
    }
    tile_acc.compute_at(acc, rt)
        .store_in(MemoryType::Tile)
        .tile(d, i, rdi, rii, tile, tile)
        .unroll(d)
        .unroll(i)
        .tile_init(rdi, rii);
    score.update()
        .tile(j, i, rdi, rii, tile, tile)
        .split(rd, rro, rri, tile)
        .reorder(j, i, rro)
        .unroll(j)
        .unroll(i)
        .tile_matmul(rri, rdi, rii);
    tile_acc.update()
        .tile(d, i, rdi, rii, tile, tile)
        .split(rj, rro, rri, tile)
        .reorder(d, i, rro)
        .unroll(d)
        .unroll(i)
        .unroll(rro)
        .tile_matmul(rri, rdi, rii);

    for (Func f : {tile_max, tile_l}) {
        f.store_in(MemoryType::Tile)
            .compute_at(acc, rt)
            .split(i, i, rii, tile)
            .unroll(i)
            .vectorize(rii);
    }
    tile_max.update().split(i, i, rii, tile).unroll(i).tile_reduce(rj_max, rii);
    tile_l.update().split(i, i, rii, tile).unroll(i).tile_reduce(rj, rii);

    // The two carried scalars, one fragment each, folded down to the two tiles
    // that are live at once.
    for (Func f : {m, l}) {
        f.store_in(MemoryType::Tile)
            .store_at(out, dof)
            .compute_at(acc, rt)
            .fold_storage(t, 2)
            .split(i, i, rii, tile)
            .unroll(i)
            .vectorize(rii);
    }

    Buffer<float> result(D, N);
    out.realize(result, target);
    result.copy_to_host();

    std::vector<float> ref(N * D);
    naive_attention(Q, K, V, ref, N, D);
    float max_err = 0;
    for (int ii2 = 0; ii2 < N; ii2++) {
        for (int dd = 0; dd < D; dd++) {
            max_err = std::max(max_err, std::abs(result(dd, ii2) - ref[dd + ii2 * D]));
        }
    }
    printf("max_err = %e  %s\n", max_err, max_err < 2e-2f ? "PASS" : "FAIL");
    return max_err < 2e-2f ? 0 : 1;
}
