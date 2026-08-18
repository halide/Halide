#include "Halide.h"

using namespace Halide;

namespace {

void set_bounds(OutputImageParam p, int extent_0, int extent_1) {
    p.set_host_alignment(16)
        .dim(0)
        .set_bounds(0, extent_0)
        .dim(1)
        .set_bounds(0, extent_1)
        .set_stride(extent_0);
}

// Attention over a batch of queries, with nothing leaving the tensor core
// registers between the two matrix multiplies: Q.K' gives a row of scores per
// query, a softmax normalises each row, and the result of that is fed straight
// into a second multiply against V as its a operand.
//
// The point of keeping it in registers is what it does not do. The scores are
// a queries x keys matrix, far larger than the output, and writing them out
// and reading them back is what an unfused attention spends its time on. Here
// they are only ever a tensor core accumulator, so the traffic is Q, K and V
// in and the output out, and the softmax reductions happen where the fragments
// already sit, by the lanes of a warp exchanging entries along a row.
//
// A block holds one group of rows and all the keys, because the softmax
// reduces along a row and a row has to be whole to reduce it. That bounds how
// many keys this can do at once - flash attention's trick of walking the keys
// in chunks and rescaling as it goes is what lifts that, and is not done here.
//
// GFlop/s on an RTX 5060 Ti, counting both multiplies, at queries=65536:
//
//     keys depth out_depth   fused   unfused   two gemms, no softmax
//       64    64        64   17887      4056                   19610
//      128    64        64   20527      4718                   20088
//       64   128        64   18611      5794                   17585
//
// The unfused column is the same attention, computed the same way and checked
// the same way, with the scores going through global memory: cublas multiplies
// into them, the softmax below normalises them there, and cublas multiplies
// again. The runner prints where its time goes; at the first row it is 25us in
// the first multiply, 114us in the softmax, and 30us in the second.
//
// So the softmax is two thirds of it, and it is not doing any arithmetic worth
// that. It is reading a queries x keys matrix that the multiply before it just
// wrote and writing another one for the multiply after it to read. Those two
// matrices are larger than everything else in the problem put together, and
// the filter above never writes either of them.
//
// The last column is cublas doing only the two multiplies, with no softmax at
// all: the scores still go through memory, but nothing happens to them on the
// way. This filter is within a tenth of it at every shape above and past it at
// two of them, which is the useful way to read these numbers - what it costs
// to do a softmax here is about what it costs to do nothing.
//
// The exponential is not worth economising on. Halide's fast_exp measured the
// same to within noise at every shape above, because the kernel issues one
// exponential per score against depth + out_depth multiply-accumulates per
// score across the two multiplies.
class Attention : public Halide::Generator<Attention> {
public:
    // The shape is compile time, because the schedule is built around it: the
    // number of keys decides how many tensor core tiles of scores a block
    // holds, and that has to be an unrolled constant.
    GeneratorParam<int> queries{"queries", 16384};
    GeneratorParam<int> keys{"keys", 64};
    GeneratorParam<int> depth{"depth", 64};
    GeneratorParam<int> out_depth{"out_depth", 64};

    // Q is depth-major, so each query's vector is contiguous, and K and V are
    // the same way. That is the layout attention is usually handed, and it
    // suits the tensor cores: the reduction of the first multiply runs along
    // it for both operands.
    Input<Buffer<float16_t, 2>> Q{"Q"};
    Input<Buffer<float16_t, 2>> K{"K"};
    Input<Buffer<float16_t, 2>> V{"V"};

    Output<Buffer<float, 2>> out{"out"};

    void generate() {
        k = RDom(0, depth, "k");
        r = RDom(0, keys, "r");
        rv = RDom(0, keys, "rv");

        // The scores, one row per query.
        s(x, y) = 0.f;
        s(x, y) += cast<float>(Q(k, y)) * cast<float>(K(k, x));

        // The largest score in each row, subtracted before the exponential so
        // that it can't overflow.
        m(y) = -1e30f;
        m(y) = max(m(y), s(r, y));

        e(x, y) = exp(s(x, y) - m(y));

        sum_e(y) = 0.f;
        sum_e(y) += e(r, y);

        // Normalising is left until after the second multiply. It is the same
        // answer, and there it is one pass over the output tile rather than
        // over the scores, which is the larger of the two whenever there are
        // more keys than there are columns of V.
        acc(x, y) = 0.f;
        acc(x, y) += cast<float>(e(rv, y)) * cast<float>(V(x, rv));

        soft(x, y) = acc(x, y) / sum_e(y);

        out(x, y) = soft(x, y);
    }

    void schedule() {
        if (using_autoscheduler()) {
            Q.dim(0).set_estimate(0, depth).dim(1).set_estimate(0, queries);
            K.dim(0).set_estimate(0, depth).dim(1).set_estimate(0, keys);
            V.dim(0).set_estimate(0, out_depth).dim(1).set_estimate(0, keys);
            out.bound(x, 0, out_depth).bound(y, 0, queries);
            return;
        }

        set_bounds(Q, depth, queries);
        set_bounds(K, depth, keys);
        set_bounds(V, out_depth, keys);
        set_bounds(out, out_depth, queries);

        const int tile = 16;
        // How many rows of queries one block takes. One tile's worth: a block
        // holds every key for the rows it has, so widening this multiplies
        // what it has to keep in registers.
        const int rows = 16;

        Var xo("xo"), yo("yo"), xio("xio"), yio("yio"), xi("xi"), yi("yi");
        Var rxi("rxi"), ryi("ryi");
        RVar rro("rro"), rri("rri");

        out.bound(x, 0, out_depth)
            .bound(y, 0, queries)
            .tile(x, y, xo, yo, xi, yi, out_depth, rows)
            .tile(xi, yi, xio, yio, xi, yi, tile, tile)
            .gpu_blocks(xo, yo)
            .unroll(xio)
            .unroll(yio)
            .tile_store(xi, yi);

        // Everything below lives in tensor core registers for the whole block.
        soft.compute_at(out, xo)
            .store_in(MemoryType::Tile)
            .tile(x, y, rxi, ryi, tile, tile)
            .unroll(x)
            .unroll(y)
            .tile_init(rxi, ryi);

        acc.compute_at(out, xo)
            .store_in(MemoryType::Tile)
            .tile(x, y, rxi, ryi, tile, tile)
            .unroll(x)
            .unroll(y)
            .tile_init(rxi, ryi);
        acc.update()
            .tile(x, y, rxi, ryi, tile, tile)
            .split(rv, rro, rri, tile)
            .reorder(x, y, rro)
            .unroll(x)
            .unroll(y)
            // This operand comes out of a fragment rather than out of memory,
            // so which tile of it each step reads has to be known here.
            .unroll(rro)
            .tile_matmul(rri, rxi, ryi);

        e.compute_at(out, xo)
            .store_in(MemoryType::Tile)
            .tile(x, y, rxi, ryi, tile, tile)
            .unroll(x)
            .unroll(y)
            .tile_init(rxi, ryi);

        // The row statistics are one value per row, but they are held as whole
        // tiles with that value repeated along the row, which is what a
        // reduction along an axis leaves behind and what makes reading them
        // back alongside the scores cost nothing.
        for (Func f : {m, sum_e}) {
            f.store_in(MemoryType::Tile)
                .compute_at(out, xo)
                .split(y, y, ryi, tile)
                .unroll(y)
                .vectorize(ryi);
            f.update()
                .split(y, y, ryi, tile)
                .unroll(y)
                .tile_reduce(r, ryi);
        }

        s.compute_at(out, xo)
            .store_in(MemoryType::Tile)
            .tile(x, y, rxi, ryi, tile, tile)
            .unroll(x)
            .unroll(y)
            .tile_init(rxi, ryi);
        s.update()
            .tile(x, y, rxi, ryi, tile, tile)
            .split(k, rro, rri, tile)
            .reorder(x, y, rro)
            .unroll(x)
            .unroll(y)
            .tile_matmul(rri, rxi, ryi);
    }

private:
    Var x{"x"}, y{"y"};
    RDom k, r, rv;
    Func s{"s"}, m{"m"}, e{"e"}, sum_e{"sum_e"}, acc{"acc"}, soft{"soft"};
};

// The softmax on its own, over a scores matrix that is already in memory. This
// is the middle of an unfused attention: cublas multiplies into the scores,
// this normalises them, and cublas multiplies again. It exists so that the
// baseline computes the same thing as the filter above, rather than being two
// multiplies with the interesting part left out.
//
// It walks each row three times - once for the row maximum, once for the row
// sum, and once to normalise - so the block stages its rows into shared memory
// first and walks them there. Reading them straight out of global memory
// instead would have each lane of a warp start a row apart, which measured 2.6
// times slower and would have made this a strawman rather than a baseline.
//
// What it does not do is write its output coalesced: one thread owns a row, so
// the lanes of a warp write a row apart. Fixing that means a warp per row and
// cross-lane reductions, which is what a hand written softmax does. As it
// stands it moves 24MB in 114us, or 210 GB/s against a part that peaks near
// 360, so there is perhaps another third to be had here.
//
// The exponentials are left inline, so each one is evaluated twice, once to
// sum it and once to normalise by that sum. Computing them at the row instead
// is a one line change to the schedule and measures the same, because this
// waits on memory rather than on the arithmetic.
class AttentionSoftmax : public Halide::Generator<AttentionSoftmax> {
public:
    GeneratorParam<int> queries{"queries", 16384};
    GeneratorParam<int> keys{"keys", 64};

    Input<Buffer<float, 2>> scores{"scores"};
    // Half precision, because that is what the multiply that follows takes,
    // and what the fused filter rounds to at the same point.
    Output<Buffer<float16_t, 2>> p{"p"};

    void generate() {
        r = RDom(0, keys, "r");

        m(y) = -1e30f;
        m(y) = max(m(y), scores(r, y));

        e(x, y) = exp(scores(x, y) - m(y));

        total(y) = 0.f;
        total(y) += e(r, y);

        p(x, y) = cast<float16_t>(e(x, y) / total(y));
    }

    void schedule() {
        if (using_autoscheduler()) {
            scores.dim(0).set_estimate(0, keys).dim(1).set_estimate(0, queries);
            p.bound(x, 0, keys).bound(y, 0, queries);
            return;
        }

        set_bounds(scores, keys, queries);
        set_bounds(p, keys, queries);

        // One thread per row: the reductions run along a row, and a row is
        // short enough for one thread to walk. Reading the rows straight out
        // of global memory that way would have each lane of a warp start a row
        // apart, so the block stages its rows through shared memory first,
        // where the copy can be coalesced and the three passes over them cost
        // nothing.
        const int threads = 64;
        const int vec = 4;
        Var yo("yo"), yi("yi"), xo("xo"), xv("xv"), t("t"), ti("ti");
        p.bound(x, 0, keys)
            .bound(y, 0, queries)
            .reorder(x, y)
            .split(y, yo, yi, threads)
            .gpu_blocks(yo)
            .gpu_threads(yi)
            .vectorize(x, vec);

        scores.in()
            .compute_at(p, yo)
            .store_in(MemoryType::GPUSharedAsync)
            .split(_0, xo, xv, vec)
            .fuse(xo, _1, t)
            .split(t, t, ti, threads)
            .gpu_threads(ti)
            .vectorize(xv);

        for (Func f : {m, total}) {
            f.compute_at(p, yi);
            f.update().reorder(r, y);
        }
        // e is left inline, so it is evaluated once where it is summed and
        // once where it is divided by that sum. Computing it at p, yi instead
        // keeps a row of it and evaluates it once, and measures the same.
    }

private:
    Var x{"x"}, y{"y"};
    RDom r;
    Func e{"e"}, m{"m"}, total{"total"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Attention, attention)
HALIDE_REGISTER_GENERATOR(AttentionSoftmax, attention_softmax)
