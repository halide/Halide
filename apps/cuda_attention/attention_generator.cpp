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
// GFlop/s on an RTX 5060 Ti at queries=65536. The count is the two multiplies
// and nothing else: the exponential per score, the two reductions along each
// row and the divide are all uncounted, so this is a way of comparing times
// for the same problem rather than a fraction of what the part can do.
//
//     keys depth out_depth   fused   unfused   two gemms, no softmax
//       64    64        64   17886      4456                   19939
//      128    64        64   20534      6520                   20067
//       64   128        64   18570      6263                   17735
//
// The unfused column is the same attention, computed the same way and checked
// the same way, with the scores going through global memory: cublas multiplies
// into them, the softmax below normalises them there, and cublas multiplies
// again. The runner prints where its time goes; at the first row it is 25us in
// the first multiply, 92us in the softmax, and 30us in the second.
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
// A warp takes a row, with the lanes walking consecutive columns, so that
// every read of the scores and every write of the result is coalesced. The
// two reductions then run across the lanes rather than within one, which is
// what the rfactor below says: each lane reduces the columns it holds, and the
// lanes combine through warp shuffles. Each lane keeps the columns it walks,
// so the scores are read once rather than once per pass over them.
//
// Getting there took three goes, and the two that were rejected are the point
// of this comment. A row per thread reading straight out of global memory has
// the lanes of a warp starting a row apart, and measured 303us where this
// measures 92. Staging the block's rows through shared memory first fixed the
// read but not the write, and measured 114us. Neither would have been a
// baseline worth comparing against.
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

        // A warp per row, with the lanes walking consecutive columns, so
        // that both the read of the scores and the write of the result are
        // coalesced. What that costs is that the two reductions now run
        // across the lanes rather than within one, which rfactor expresses:
        // each lane reduces the columns it holds, and the lanes then combine
        // through warp shuffles.
        const int lanes = 32;
        const int rows = 8;
        Var xo("xo"), xi("xi"), yo("yo"), yi("yi"), u("u"), v("v");
        RVar ri("ri"), ro("ro");

        p.bound(x, 0, keys)
            .bound(y, 0, queries)
            .split(x, xo, xi, lanes)
            .split(y, yo, yi, rows)
            .reorder(xi, xo, yi, yo)
            .gpu_blocks(yo)
            .gpu_threads(yi)
            .gpu_lanes(xi)
            .unroll(xo);

        // Each lane holds the columns it walks, so the scores are read once
        // rather than once per pass over them.
        Var so("so"), si("si");
        scores.in()
            .compute_at(p, yi)
            .store_in(MemoryType::Register)
            .split(_0, so, si, lanes)
            .gpu_lanes(si)
            .unroll(so);

        Func mi = m.update().split(r, ri, ro, lanes).reorder(ri, ro).rfactor(ro, u);
        mi.compute_at(p, yi).gpu_lanes(u);
        mi.update().gpu_lanes(u);
        m.compute_at(p, yi).store_in(MemoryType::Register);

        Func ti = total.update().split(r, ri, ro, lanes).reorder(ri, ro).rfactor(ro, v);
        ti.compute_at(p, yi).gpu_lanes(v);
        ti.update().gpu_lanes(v);
        total.compute_at(p, yi).store_in(MemoryType::Register);
    }

private:
    Var x{"x"}, y{"y"};
    RDom r;
    Func e{"e"}, m{"m"}, total{"total"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Attention, attention)
HALIDE_REGISTER_GENERATOR(AttentionSoftmax, attention_softmax)
