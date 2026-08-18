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
// On an RTX 5060 Ti at queries=65536, against the same attention computed
// unfused - cublas multiplies into a scores matrix in global memory, the
// softmax below normalises it there, and cublas multiplies again. Both are
// checked against the host the same way. The GFlop/s count is the two
// multiplies and nothing else: the exponential per score, the two reductions
// along each row and the divide are all uncounted, so it is a way of comparing
// times for the same problem rather than a fraction of what the part can do.
//
//     keys depth out_depth    fused          unfused    of which softmax
//       64    64        64   60.0us   17884  236.3us     4544      92.1us
//      128    64        64  104.7us   20520  324.0us     6628     107.3us
//       64   128        64   86.5us   18610  256.7us     6274      92.1us
//
// The last column is why. The softmax reads a queries x keys matrix that the
// multiply before it just wrote, and writes another one for the multiply after
// it to read, and those two matrices are larger than everything else in the
// problem put together. The filter above never writes either of them: the
// scores are a tensor core accumulator from the moment they are computed to
// the moment they are consumed.
//
// There is no third column for the two multiplies without the softmax, though
// it is the obvious thing to want. They cannot be run as a pair: the second
// takes half precision operands, and the softmax needs the scores in single
// precision, because it exponentiates them and half precision scores of this
// size lose enough to matter. A runnable pair would either carry half the
// bytes or do different arithmetic, and comparing against either flatters this
// filter. Timing each multiply where it sits says the same thing honestly -
// they are 25us and 30us of the 236 in the first row.
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
// Each lane keeps the exponentials of the columns it holds, so each is
// evaluated once, rather than once where it is summed and once where it is
// divided by that sum. Leaving them inline measures the same to a tenth of a
// microsecond, but that says nothing: ptxas will happily common up two
// identical calls on the same value, so the two schedules may well be the
// same code. Asking for the work once is the honest way to write it either
// way.
//
// Nor does fast_exp change anything, and here the measurement does say
// something, because ncu says what this waits on: the load/store pipe at 98%
// of what it can issue, every arithmetic pipeline under-utilised, and the SMs
// busy 37% of the time. It is bound by memory instructions rather than by
// bandwidth - DRAM is at 36%.
//
// Wider accesses are the obvious thing to reach for and they do not help.
// Giving each lane a contiguous run of columns rather than one column in every
// thirty two gets the loads and stores as wide as the hardware has: at
// keys=128 the kernel issues one ld.global.nc.v2.b64 and one st.global.v2.b32
// per thread where this issues four and four, and 140 instructions where this
// issues 152. It measures 111.4us against 107.9. At keys=64 it wins instead,
// 89.0 against 92.1.
//
// Both sit at 96% of what the L1 can do, with the same occupancy and much the
// same register count, which is the reason: what crosses the L1 is the same
// either way. Thirty two lanes reading sixteen bytes each and four
// instructions of thirty two lanes reading four bytes each are the same four
// transactions. The instruction count is not what this is short of, so
// widening the accesses moves it by a few percent in whichever direction the
// shape happens to favour, and costs arithmetic in the schedule to do it.
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

        // Each lane keeps the exponentials of the columns it holds, so each is
        // evaluated once rather than once to sum and once to normalise.
        Var eo("eo"), ei("ei");
        e.compute_at(p, yi)
            .store_in(MemoryType::Register)
            .split(x, eo, ei, lanes)
            .gpu_lanes(ei)
            .unroll(eo);

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
