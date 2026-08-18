// Flash attention on the tensor cores: the recurrence of
// inductive_flash_attention scheduled so that every value it carries between
// key blocks lives in tensor core registers and never reaches memory.
//
// A block of the grid owns a strip of queries and walks the keys. Each step
// multiplies Q by a block of K into an accumulator, reduces along its rows for
// the block's maximum, exponentiates, reduces again for the block's sum, and
// multiplies against a block of V. The state carried to the next step - the
// running maximum, the running sum and the accumulator - is three more
// fragments, and storage folding keeps two blocks of them live rather than
// one per key block.

#include "Halide.h"
#include <cmath>
#include <cstdio>

using namespace Halide;

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
    const int queries = 64, keys = 128, depth = 32, out_depth = 32;
    const int KB = tile, nb = keys / KB;

    Buffer<float16_t> Q(depth, queries), K(depth, keys), V(out_depth, keys);
    auto fill = [](Buffer<float16_t> &buf) {
        for (int j = 0; j < buf.height(); j++) {
            for (int i = 0; i < buf.width(); i++) {
                buf(i, j) = float16_t(((i * 7 + j * 13) % 7) * 0.25f - 0.75f);
            }
        }
        buf.set_host_dirty();
    };
    fill(Q);
    fill(K);
    fill(V);

    Var x("x"), y("y"), b("b");
    RDom k(0, depth, "k"), rx(0, KB, "rx"), rkv(0, KB, "rkv");
    Func S("S"), rm("rm"), P("P"), rs("rs"), PV("PV"), last("last"), out("out");
    // The row reductions read back into whole tiles. A fragment spread along
    // an axis already holds its value in every entry, so this costs nothing,
    // and it gives the state a tile shape to be computed at - on its own, a
    // value uniform along a row says nothing about how it sits in a warp.
    Func rmT("rmT"), rsT("rsT");
    Func state({Float(32), Float(32), Float(32)}, "state");

    // One key block's worth of work, all independent of the running state.
    S(x, y, b) = 0.f;
    S(x, y, b) += cast<float>(Q(k, y)) * cast<float>(K(k, b * KB + x));
    rm(y, b) = -1e30f;
    rm(y, b) = max(rm(y, b), S(rx, y, b));
    P(x, y, b) = exp(S(x, y, b) - rm(y, b));
    rs(y, b) = 0.f;
    rs(y, b) += P(rx, y, b);
    PV(x, y, b) = 0.f;
    PV(x, y, b) += cast<float>(P(rkv, y, b)) * cast<float>(V(x, b * KB + rkv));

    rmT(x, y, b) = rm(y, b);
    rsT(x, y, b) = rs(y, b);

    Expr m_prev = state(x, y, b - 1)[0];
    Expr l_prev = state(x, y, b - 1)[1];
    Expr acc_prev = state(x, y, b - 1)[2];
    Expr m_new = max(m_prev, rmT(x, y, b));
    Expr carried = exp(m_prev - m_new), incoming = exp(rmT(x, y, b) - m_new);
    state(x, y, b) =
        select(b <= 0,
               Tuple(rmT(x, y, b), rsT(x, y, b), PV(x, y, b)),
               Tuple(m_new,
                     carried * l_prev + incoming * rsT(x, y, b),
                     carried * acc_prev + incoming * PV(x, y, b)));

    // Sweep the key blocks and keep the last, which is what gives the state a
    // loop to be folded against. See inductive_flash_attention.
    RDom rb(0, nb, "rb");
    last(x, y) = 0.f;
    last(x, y) = select(rb == nb - 1,
                        state(x, y, rb)[2] / state(x, y, rb)[1], last(x, y));
    out(x, y) = last(x, y);

    Var xo("xo"), yo("yo"), xio("xio"), yio("yio"), xi("xi"), yi("yi");
    Var rxi("rxi"), ryi("ryi");
    RVar rro("rro"), rri("rri"), rbo("rbo"), rbi("rbi");

    // A block owns a strip of queries and every column of the output.
    out.bound(x, 0, out_depth)
        .bound(y, 0, queries)
        .tile(x, y, xo, yo, xi, yi, out_depth, tile)
        .tile(xi, yi, xio, yio, xi, yi, tile, tile)
        .gpu_blocks(xo, yo)
        .unroll(xio)
        .unroll(yio)
        .tile_store(xi, yi);

    // The result of the sweep, and the state it reads, live across the whole
    // walk over the keys, so they sit at block level.
    last.compute_at(out, xo)
        .store_in(MemoryType::Tile)
        .tile(x, y, rxi, ryi, tile, tile)
        .unroll(x)
        .unroll(y)
        .tile_init(rxi, ryi);
    // The state ping-pongs between two sets of registers, and which set a
    // step uses has to be known when the code is generated rather than looked
    // up at run time, so the walk over key blocks is unrolled by two.
    last.update()
        .tile(x, y, rxi, ryi, tile, tile)
        .split(rb, rbo, rbi, 2)
        .reorder(x, y, rbi, rbo)
        .unroll(x)
        .unroll(y)
        .unroll(rbi)
        .tile_init(rxi, ryi);

    // Two key blocks of state are live at once, because each is read to make
    // the next.
    state.store_in(MemoryType::Tile)
        .store_at(out, xo)
        .compute_at(last, rbi)
        .fold_storage(b, 2)
        .tile(x, y, rxi, ryi, tile, tile)
        .unroll(x)
        .unroll(y)
        .tile_init(rxi, ryi);

    // Everything for one key block, computed where the state is.
    for (Func f : {P, PV, S, rmT, rsT}) {
        f.compute_at(last, rbi)
            .store_in(MemoryType::Tile)
            .tile(x, y, rxi, ryi, tile, tile)
            .unroll(x)
            .unroll(y)
            .tile_init(rxi, ryi);
    }
    S.update()
        .tile(x, y, rxi, ryi, tile, tile)
        .split(k, rro, rri, tile)
        .reorder(x, y, rro)
        .unroll(x)
        .unroll(y)
        .tile_matmul(rri, rxi, ryi);
    PV.update()
        .tile(x, y, rxi, ryi, tile, tile)
        .split(rkv, rro, rri, tile)
        .reorder(x, y, rro)
        .unroll(x)
        .unroll(y)
        .unroll(rro)
        .tile_matmul(rri, rxi, ryi);

    // The two row reductions, each held as a whole tile with the value
    // repeated along the row.
    for (Func f : {rm, rs}) {
        f.store_in(MemoryType::Tile)
            .compute_at(last, rbi)
            .split(y, y, ryi, tile)
            .unroll(y)
            .vectorize(ryi);
    }
    rm.update().split(y, y, ryi, tile).unroll(y).tile_reduce(rx, ryi);
    rs.update().split(y, y, ryi, tile).unroll(y).tile_reduce(rx, ryi);

    Buffer<float> result(out_depth, queries);
    out.realize(result, target);
    result.copy_to_host();

    int bad = 0;
    for (int j = 0; j < queries; j++) {
        std::vector<float> score(keys);
        float row_max = -1e30f;
        for (int i = 0; i < keys; i++) {
            score[i] = 0;
            for (int d = 0; d < depth; d++) {
                score[i] += (float)Q(d, j) * (float)K(d, i);
            }
            row_max = std::max(row_max, score[i]);
        }
        float total = 0;
        for (int i = 0; i < keys; i++) {
            score[i] = std::exp(score[i] - row_max);
            total += score[i];
        }
        for (int i = 0; i < out_depth; i++) {
            float want = 0;
            for (int c = 0; c < keys; c++) {
                want += score[c] * (float)V(i, c);
            }
            want /= total;
            if (std::abs(result(i, j) - want) > 1e-3f * std::abs(want) + 1e-4f) {
                if (bad++ < 5) {
                    printf("result(%d, %d) = %f instead of %f\n", i, j,
                           result(i, j), want);
                }
            }
        }
    }
    if (bad) {
        printf("Failed!\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
