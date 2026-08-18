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
//       64    64        64   59.8us   17961  164.7us     6519      19.2us
//      128    64        64  105.7us   20320  331.0us     6487     105.0us
//       64   128        64   86.4us   18635  184.6us     8723      19.3us
//
// The last column is why. The softmax reads a queries x keys matrix that the
// multiply before it just wrote, and writes another one for the multiply after
// it to read, and those two matrices are larger than everything else in the
// problem put together. The filter above never writes either of them: the
// scores are a tensor core accumulator from the moment they are computed to
// the moment they are consumed. The softmax below is held in tensor core
// registers too, so what separates the two columns is that traffic and
// nothing else.
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
// It is held in tensor core registers, the same as the fused filter, which is
// worth doing even with no matrix multiply in sight. A tile load and a tile
// store are warp-wide and coalesced by construction, and the two reductions
// along the rows become butterflies where the fragments already sit, rather
// than each lane walking every other lane's partial. Narrowing to half
// precision on the way out costs nothing beyond the convert: an entry sits in
// the same lane whichever precision holds it, so it is a repack within each
// lane and no lane has to reach outside itself.
//
// The block holds whole rows, because the reductions run along them.
//
// Measured on an RTX 5060 Ti at queries=65536, this takes 19.2us at keys=64
// where a warp per row with the reductions written as rfactor onto the lane
// index took 92.1. That idiom lowers to a serial gather - each lane walks all
// thirty two lanes fetching their partial, sixty four shuffles per row for the
// two reductions - where a tile reduction is a butterfly, ten. The tile load
// and store are also warp wide by construction, which a row per thread is not.
//
// At keys=128 the two come out level, 105.0us against 107.3, and the gap
// between the two rows is not the whole story of what changed. The scores and
// the result are 24MB at keys=64, which fits the 32MB cache, so a benchmark
// that runs the same call over and over is reading them back out of it - 24MB
// in 19.2us is 1250 GB/s, well past what the memory can do. At keys=128 they
// are 48MB and it is reading them for real. So the first row is a cached
// number and the second is not, and what the second says is that once this is
// waiting on memory, how many instructions it takes to ask stops mattering.
//
// The kernel itself has nothing spare in it at either size: eight tile loads
// and eight tile stores for eight columns of tiles, one butterfly per
// reduction rather than one per tile, every narrowing convert paired, and no
// spills.

class AttentionSoftmax : public Halide::Generator<AttentionSoftmax> {
public:
    GeneratorParam<int> queries{"queries", 16384};
    GeneratorParam<int> keys{"keys", 64};

    Input<Buffer<float, 2>> scores{"scores"};
    // Half precision, because that is what the multiply that follows takes,
    // and what the fused filter narrows to at the same point.
    Output<Buffer<float16_t, 2>> p{"p"};

    void generate() {
        r = RDom(0, keys, "r");

        s(x, y) = scores(x, y);

        m(y) = -1e30f;
        m(y) = max(m(y), s(r, y));

        e(x, y) = exp(s(x, y) - m(y));

        total(y) = 0.f;
        total(y) += e(r, y);

        soft(x, y) = cast<float16_t>(e(x, y) / total(y));

        p(x, y) = soft(x, y);
    }

    void schedule() {
        if (using_autoscheduler()) {
            scores.dim(0).set_estimate(0, keys).dim(1).set_estimate(0, queries);
            p.bound(x, 0, keys).bound(y, 0, queries);
            return;
        }

        set_bounds(scores, keys, queries);
        set_bounds(p, keys, queries);

        const int tile = 16;
        // How many rows of scores one block takes. A block holds every key for
        // the rows it has, so widening this multiplies what it keeps in
        // registers.
        const int rows = 16;

        Var xo("xo"), yo("yo"), xio("xio"), yio("yio"), xi("xi"), yi("yi");
        Var rxi("rxi"), ryi("ryi");

        p.bound(x, 0, keys)
            .bound(y, 0, queries)
            .tile(x, y, xo, yo, xi, yi, keys, rows)
            .tile(xi, yi, xio, yio, xi, yi, tile, tile)
            .gpu_blocks(xo, yo)
            .unroll(xio)
            .unroll(yio)
            .tile_store(xi, yi);

        for (Func f : {s, e, soft}) {
            f.compute_at(p, xo)
                .store_in(MemoryType::Tile)
                .tile(x, y, rxi, ryi, tile, tile)
                .unroll(x)
                .unroll(y);
        }
        // The scores arrive from memory, which is a tile load; the rest are
        // computed where they sit.
        s.tile_load(rxi, ryi);
        e.tile_init(rxi, ryi);
        soft.tile_init(rxi, ryi);

        // The row statistics are one value per row, held as whole tiles with
        // that value repeated along the row, which is what a reduction along
        // an axis leaves behind and what makes reading them back alongside the
        // scores cost nothing.
        for (Func f : {m, total}) {
            f.store_in(MemoryType::Tile)
                .compute_at(p, xo)
                .split(y, y, ryi, tile)
                .unroll(y)
                .vectorize(ryi);
            f.update()
                .split(y, y, ryi, tile)
                .unroll(y)
                .tile_reduce(r, ryi);
        }
    }

private:
    Var x{"x"}, y{"y"};
    RDom r;
    Func s{"s"}, m{"m"}, e{"e"}, total{"total"}, soft{"soft"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Attention, attention)
HALIDE_REGISTER_GENERATOR(AttentionSoftmax, attention_softmax)
