#include "Halide.h"
#include <algorithm>

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
// Every warp in a block reduces over every key, so they all read the whole of
// K and V. Those are staged into shared memory with asynchronous copies and
// shared by the block, which is what the warp count is for: with one warp
// there is nothing to share the staging with, and the tensor core loads come
// from global memory instead. That is worth 1.7x to 1.8x, and it is the only
// thing the warp count buys - warps without the staging measure the same as
// one warp, because there is nothing else for them to share.
//
// On an RTX 5060 Ti at queries=65536, against the same attention computed
// unfused - cublas multiplies into a scores matrix in global memory, the
// softmax below normalises it there, and cublas multiplies again. Both are
// checked against the host the same way. The GFlop/s count is the two
// multiplies and nothing else: the exponential per score, the two reductions
// along each row and the divide are all uncounted, so it is a way of comparing
// times for the same problem rather than a fraction of what the part can do.
// The column before it is the same schedule with one warp per block and
// nothing staged, which is what this did before.
//
//     keys depth out_depth    fused        one warp      unfused    of which softmax
//       64    64        64   34.7us   30975   59.9us   124.6us     8619      27.4us
//      128    64        64   65.5us   32791  105.7us   273.6us     7850     120.6us
//       64   128        64   49.4us   32604   86.4us   147.2us    10944      28.6us
//
// The first row reaches 60% of the 51541 GFlop/s that apps/cuda_mat_mul
// measures for back to back half precision multiplies into single precision
// accumulators, and ncu puts the tensor pipe at 58% of peak and names it the
// limit. The rest of the machine is idle beside it: the FMA pipe is at 14%,
// the special function unit that computes the exponentials at 14%, and the
// integer pipe at 9%. So the softmax is close to free, and what is left on the
// table is the tensor cores waiting - for DRAM, at 63% of peak, and for the
// 23% the load/store pipe spends fetching operands out of shared memory.
//
// Everything accumulates in single precision, and there is no knob to do
// otherwise, because that is what attention is: torch's flash, cuDNN and
// memory-efficient backends all carry scores of 640000 - past what half
// precision holds - without producing the nans an overflowing accumulator
// would, and all sit six times nearer a float64 reference than rounding the
// scores to half precision would put them. A half precision accumulator for
// the second multiply measured 12% to 14% faster, but it computes something
// else, and something no attention library offers.
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
// they are 64us and 34us of the 125 in the first row.
//
// The exponential is not worth economising on. Halide's fast_exp measured the
// same to within noise at every shape above, which the profile agrees with:
// the kernel issues one exponential per score against depth + out_depth
// multiply-accumulates per score across the two multiplies, and the unit that
// computes them sits at 14%.
class Attention : public Halide::Generator<Attention> {
public:
    // The shape is compile time, because the schedule is built around it: the
    // number of keys decides how many tensor core tiles of scores a block
    // holds, and that has to be an unrolled constant.
    GeneratorParam<int> queries{"queries", 16384};
    GeneratorParam<int> keys{"keys", 64};
    GeneratorParam<int> depth{"depth", 64};
    GeneratorParam<int> out_depth{"out_depth", 64};

    // How many tensor core tiles of queries each warp takes, and how many
    // warps a block has. Every warp in a block reduces over all the keys, so
    // they all read the same K and V, which is the reuse staging them gets.
    // Zero means use the measured shapes below.
    GeneratorParam<int> tiles_y{"tiles_y", 0};
    GeneratorParam<int> warps{"warps", 0};
    // Whether to stage K and V into shared memory for the block to share.
    GeneratorParam<bool> stage{"stage", true};
    // Extra elements per row of the staged panels, to spread consecutive rows
    // across banks. Zero means sixteen bytes, the least that keeps each row
    // aligned for both the widest asynchronous copy and the tensor core loads.
    GeneratorParam<int> pad{"pad", 0};

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
        int ty = tiles_y, wy = warps;
        if (ty == 0 || wy == 0) {
            // Measured on an RTX 5060 Ti. Four warps is the shape that suits
            // most of these: fewer leaves too little to spread the cost of
            // staging K and V over, and more runs the block out of registers,
            // because every warp keeps all of its keys in them. What moves is
            // which way to spend a bigger block - more keys per warp wants
            // more rows per warp, and a deeper reduction wants more warps.
            ty = 1;
            wy = 4;
            if ((int)keys > 64) {
                ty = 2;
            } else if ((int)depth > 64) {
                wy = 8;
            }
        }
        // How many rows of queries one warp takes, and how many the block
        // does. A warp holds every key for the rows it has, so widening the
        // first multiplies what it keeps in registers, where widening the
        // second only adds warps.
        const int rows = tile * ty;
        const int block_rows = rows * wy;

        Var xo("xo"), yo("yo"), xio("xio"), yio("yio"), xi("xi"), yi("yi");
        Var yw("yw"), rxi("rxi"), ryi("ryi");
        RVar rro("rro"), rri("rri");

        out.bound(x, 0, out_depth)
            .bound(y, 0, queries)
            .tile(x, y, xo, yo, xi, yi, out_depth, block_rows)
            .split(yi, yw, yi, rows)
            .tile(xi, yi, xio, yio, xi, yi, tile, tile)
            .gpu_blocks(xo, yo)
            .unroll(xio)
            .unroll(yio)
            .tile_store(xi, yi);

        // Everything below lives in tensor core registers for the whole block.
        // The allocations sit at block level so that the staged panels can be
        // filled once above the warps, but a tile allocation is per-lane
        // already, so each warp still gets its own.
        soft.compute_at(out, xo)
            .store_in(MemoryType::Tile)
            .split(y, yw, y, rows)
            .tile(x, y, rxi, ryi, tile, tile)
            .unroll(x)
            .unroll(y)
            .tile_init(rxi, ryi);

        acc.compute_at(out, xo)
            .store_in(MemoryType::Tile)
            .split(y, yw, y, rows)
            .tile(x, y, rxi, ryi, tile, tile)
            .unroll(x)
            .unroll(y)
            .tile_init(rxi, ryi);
        acc.update()
            .split(y, yw, y, rows)
            .tile(x, y, rxi, ryi, tile, tile)
            .split(rv, rro, rri, tile)
            .reorder(x, y, rro, yw)
            .unroll(x)
            .unroll(y)
            // This operand comes out of a fragment rather than out of memory,
            // so which tile of it each step reads has to be known here.
            .unroll(rro)
            .tile_matmul(rri, rxi, ryi);

        e.compute_at(out, xo)
            .store_in(MemoryType::Tile)
            .split(y, yw, y, rows)
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
                .split(y, yw, y, rows)
                .split(y, y, ryi, tile)
                .unroll(y)
                .vectorize(ryi);
            f.update()
                .split(y, yw, y, rows)
                .split(y, y, ryi, tile)
                .unroll(y)
                .tile_reduce(r, ryi);
        }

        s.compute_at(out, xo)
            .store_in(MemoryType::Tile)
            .split(y, yw, y, rows)
            .tile(x, y, rxi, ryi, tile, tile)
            .unroll(x)
            .unroll(y)
            .tile_init(rxi, ryi);
        s.update()
            .split(y, yw, y, rows)
            .tile(x, y, rxi, ryi, tile, tile)
            .split(k, rro, rri, tile)
            .reorder(x, y, rro, yw)
            .unroll(x)
            .unroll(y)
            .tile_matmul(rri, rxi, ryi);

        if (wy > 1) {
            // With one warp there is nothing for a loop over warps to do, and
            // leaving it serial lets Halide drop it.
            out.gpu_threads(yw);
            for (Func f : {soft, acc, e, s, m, sum_e}) {
                f.gpu_threads(yw);
            }
            for (Func f : {acc, s, m, sum_e}) {
                f.update().gpu_threads(yw);
            }
        }

        if (stage) {
            // K and V are the whole of what the warps of a block share: every
            // warp reduces over every key, so each reads all of both. Staging
            // them turns one global load per tensor core operand into one
            // shared load, and spreads what it costs to fetch them over the
            // warps. They are the same for every block too, but there is
            // nowhere above a block to put them.
            //
            // Each thread moves sixteen bytes at a time along the dense
            // dimension, so that the reads from global memory coalesce and the
            // writes to shared memory can be done as asynchronous copies.
            const int vec = 8;
            const int p = pad ? (int)pad : vec;
            Var ko("ko"), kv("kv"), t("t"), ti("ti"), to("to"), tw("tw");

            // K is dense in the reduction dimension, which is its _0.
            K.in()
                .compute_at(out, xo)
                .store_in(MemoryType::GPUSharedAsync)
                .align_storage(_0, depth + p)
                .split(_0, ko, kv, vec)
                .fuse(ko, _1, t)
                .split(t, t, ti, 32)
                .split(t, to, tw, wy)
                .gpu_lanes(ti)
                .gpu_threads(tw)
                .vectorize(kv);

            // V is dense in the free dimension, which is its _0.
            V.in()
                .compute_at(out, xo)
                .store_in(MemoryType::GPUSharedAsync)
                .align_storage(_0, out_depth + p)
                .split(_0, ko, kv, vec)
                .fuse(ko, _1, t)
                .split(t, t, ti, 32)
                .split(t, to, tw, wy)
                .gpu_lanes(ti)
                .gpu_threads(tw)
                .vectorize(kv);
        }
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
// Measured in place, between the two multiplies, it is 29.3us at keys=64 and
// 124.8us at keys=128. Doubling the keys doubles the scores it reads and the
// result it writes, and takes it from 24MB against a 32MB cache to 48MB, so
// four times the time for twice the data is what falling out of the cache
// looks like rather than anything the kernel does differently.
//
// The kernel itself has nothing spare in it at either size: one tile load and
// one tile store per column of tiles, one butterfly per reduction rather than
// one per tile, every narrowing convert paired, and no spills.

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

// The same attention, with the keys walked in tiles and the softmax rescaled
// as it goes, which is what flash attention is. The filter above holds every
// key of a row at once, because a softmax needs a whole row to normalise it;
// this one holds one key tile at a time and carries three things across the
// walk - the largest score seen so far, the sum of the exponentials so far,
// and the output so far - rescaling the latter two whenever the maximum moves.
//
// The carried state is what makes this interesting to schedule. It is two
// values per query, held in tensor core fragments the same way the row
// statistics above are, and each step reads the step before it. So it wants to
// slide: two tiles of it are live, and the walk should keep those two rather
// than the whole prefix. A fragment is registers, which cannot be indexed at
// run time, so which of the two slots a step reads has to be a constant where
// the code is generated. Unrolling the walk by two gives that, and naming the
// dimension the state slides along keeps the split from widening the window to
// the pair of steps.
//
// The running maximum and the running normalizer are inductive Funcs: each is
// defined by what it was one step ago, with the first step given separately.
// Written that way the compiler can see that only the last step is live, which
// is what lets the storage fold to two tiles. Asked for one tile before the
// first, the maximum gives the same answer as the first, so the rescaling on
// that step is by exp(0) and costs nothing to leave in.
//
// Normalising rides along on the last step of the walk rather than being a
// pass of its own, which is also what makes the normalizer a producer of the
// accumulator and so gives it a loop to be computed in.
//
// On an RTX 5060 Ti at queries=65536, depth=out_depth=64, against the filter
// above and against the unfused pair:
//
//     keys    flash        fused          unfused
//       64   47.4us  22630   37.7us  28451    126.6us   8479
//      128   79.9us  26868   71.1us  30204    272.1us   7891
//      256  222.9us  19270   would not launch   563.5us   7621
//      512  352.4us  24373   would not launch  1171.2us   7334
//     1024  699.5us  24560   would not launch  5350.4us   3211
//     2048 1398.3us  24572   would not launch 15193.4us   2261
//
// The point of the table is the middle column running out. The filter above
// holds a block's worth of scores in registers, so the key count sets how many
// tensor core tiles a warp keeps, and past 128 keys the launch is rejected for
// wanting more registers than a thread has. This one keeps a step's worth
// whatever the key count is, so it goes on running, while the unfused pair
// falls away as the scores matrix it writes and reads back outgrows the cache -
// most of the last row is its softmax, at 4.3ms of the 5.3.
//
// For scale, torch's flash backend on the same part and the same shapes holds
// about 45800 GFlop/s from 128 keys up, which is 89% of what apps/cuda_mat_mul
// measures for back to back half precision multiplies. At 64 keys it does not
// win: it takes the same time at 64 keys as at 128, so it is latency bound
// there, and both filters here beat it.
//
// What decides the time is how much work a step does, and nothing else that was
// measured. The shape above keeps 480 floats per lane live - the scores and the
// weights 128 each, the two accumulators 64 each, the carried pair 32 each -
// against the 255 registers a thread has, so it spills, and registers hold it
// to 8 warps per SM where every other limit allows 24 or more. Both of those
// look like the answer and neither is:
//
//  - Spilling is not it. Every local memory access is a spill, but local memory
//    is under 4% of the sectors L1 is asked for.
//  - Occupancy is not it. One tile of queries per warp fits in 164 registers,
//    which buys half again as many warps per SM, and measures 18686 against the
//    28153 of the shape that spills.
//
// What does correlate is rows per warp times keys per step: halving either one
// costs about a third of the throughput. So what a step pays that does not
// scale with it is the thing to chase, and the operand loads are the candidate.
// Past the key count where whole panels fit in shared memory nothing is staged,
// so every warp re-reads K and V from global memory on every step. Staging a
// key tile at a time rather than whole panels is what would fix that, and it
// wants a loop order this cannot currently express: the panel has to be filled
// above the loop over warps, and the carried state has to live inside it.
class AttentionFlash : public Halide::Generator<AttentionFlash> {
public:
    GeneratorParam<int> queries{"queries", 16384};
    GeneratorParam<int> keys{"keys", 64};
    GeneratorParam<int> depth{"depth", 64};
    GeneratorParam<int> out_depth{"out_depth", 64};

    // How many tensor core tiles of queries each warp takes, and how many
    // warps a block has. Unlike the filter above, what a warp keeps in
    // registers here does not grow with the number of keys, so the shape that
    // suits is one tile of queries per warp and as many warps as the staging
    // will feed.
    GeneratorParam<int> tiles_y{"tiles_y", 0};
    GeneratorParam<int> warps{"warps", 0};
    // How many keys one step of the walk takes. Zero means as many as the
    // measurements below want, or half the keys if there are too few for that.
    // Sixty four is what this was tuned to at a key count large enough for the
    // walk to be a real loop, and is what flash attention implementations
    // generally use.
    GeneratorParam<int> chunk{"chunk", 0};
    // Whether to stage K and V into shared memory for the block to share.
    GeneratorParam<bool> stage{"stage", true};
    // Stage one key tile at a time rather than whole panels, which is what a
    // key count too large for whole panels wants. Off, because it does not
    // compile yet - see the note above. Needs one warp, so that there is no
    // loop over warps for the fill to have to sit above.
    GeneratorParam<bool> stage_tile{"stage_tile", true};
    // Whether to stage Q into shared memory. A block's rows of it are the same
    // at every step of the walk, so unstaged it is re-read from global memory
    // once per step.
    GeneratorParam<bool> stage_q{"stage_q", true};
    // Extra elements per row of the staged panels, to spread consecutive rows
    // across banks.
    GeneratorParam<int> pad{"pad", 0};

    Input<Buffer<float16_t, 2>> Q{"Q"};
    Input<Buffer<float16_t, 2>> K{"K"};
    Input<Buffer<float16_t, 2>> V{"V"};

    Output<Buffer<float, 2>> out{"out"};

    void generate() {
        // How many keys a step of the walk takes. Everything that is per step
        // rather than per key - the two reductions along a row, and rescaling
        // the carried state and the accumulator - is paid once per step, so a
        // wider step amortises it. What it costs is the scores, which are the
        // one thing here that grows with it.
        key_tile = chunk ? (int)chunk : std::min(64, (int)keys / 2);
        _halide_user_assert(key_tile % 16 == 0 && keys % key_tile == 0 &&
                            keys / key_tile >= 2)
            << "chunk must be a multiple of 16 that divides keys at least twice";
        num_tiles = keys / key_tile;
        key_pad = 2 * key_tile;

        k = RDom(0, depth, "k");
        rj_max = RDom(0, key_tile, "rj_max");
        rj = RDom(0, key_tile, "rj");
        rt = RDom(0, num_tiles, "rt");

        // One key tile's worth of scores.
        s(x, y, t) = 0.f;
        s(x, y, t) += cast<float>(Q(k, y)) * cast<float>(K(k, t * key_tile + x));

        tile_max(y, t) = -1e30f;
        tile_max(y, t) = max(tile_max(y, t), s(rj_max, y, t));

        // The largest score up to and including this tile. It depends on
        // nothing but itself and the tile maximum.
        m(y, t) = select(t <= 0,
                         tile_max(y, t),
                         likely(max(m(y, t - 1), tile_max(y, t))));

        // The weights are taken against the running maximum, so what this step
        // produces is already on the right scale and only what is carried has
        // to be rescaled.
        //
        e(x, y, t) = exp(s(x, y, t) - m(y, t));

        tile_l(y, t) = 0.f;
        tile_l(y, t) += e(rj, y, t);

        l(y, t) = select(t <= 0, tile_l(y, t),
                         likely(l(y, t - 1) * exp(m(y, t - 1) - m(y, t)) +
                                tile_l(y, t)));

        tile_acc(x, y, t) = 0.f;
        tile_acc(x, y, t) +=
            cast<float>(e(rj, y, t)) * cast<float>(V(x, t * key_tile + rj));

        acc(x, y) = 0.f;
        acc(x, y) =
            (tile_acc(x, y, rt) +
             select(rt <= 0,
                    0.f,
                    likely(acc(x, y) * exp(m(y, rt - 1) - m(y, rt))))) /
            select(rt < num_tiles - 1, 1.f, l(y, rt));

        out(x, y) = acc(x, y);
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
        // The walk is warmed up by rewinding, so it reads whole steps before
        // the first key. Rather than clamping the index, which would leave
        // which of the two folded slots of the carried state a step reads
        // dependent on the step, the caller hands over key panels with room
        // before them. What is in there only has to be finite: the first step
        // rescales an accumulator that is still zero.
        K.set_host_alignment(16)
            .dim(0)
            .set_bounds(0, depth)
            .dim(1)
            .set_bounds(-key_pad, keys + key_pad)
            .set_stride(depth);
        V.set_host_alignment(16)
            .dim(0)
            .set_bounds(0, out_depth)
            .dim(1)
            .set_bounds(-key_pad, keys + key_pad)
            .set_stride(out_depth);
        set_bounds(out, out_depth, queries);

        const int tile = 16;
        // Staging holds all of K and V, so it only fits while the key count is
        // small, and it is what the warp count buys. Whether it is on is what
        // decides the shape: staged, the warps have something to share and are
        // worth having; unstaged, they measure no better than one, and one warp
        // per block leaves the most registers for the walk to keep the scores
        // in, which is what is scarce there.
        const int vec = 8;
        const int p = pad ? (int)pad : vec;
        const int staged_bytes = 2 * (int)keys * ((int)depth + (int)out_depth + 2 * p);
        const bool staging = stage && staged_bytes <= 40 * 1024;
        // Staging one key tile at a time instead, which is what a key count too
        // large for whole panels wants. The fill then sits inside the walk, so
        // every stage in there needs a loop over warps of its own: all the
        // thread loops in the walk have to be the same 32 by 4 shape for
        // lowering to fuse them into one thread block. A stage left outside the
        // warp loop gets serialized onto one of them instead.
        const bool stage_per_tile = stage_tile && !staging;

        int ty = tiles_y, wy = warps;
        if (ty == 0 || wy == 0) {
            // Measured on an RTX 5060 Ti at queries=65536.
            if (stage_per_tile) {
                // The panel is filled once and read by every warp, so the block
                // wants warps to share it with rather than rows per warp.
                ty = 1;
                wy = 4;
            } else {
                ty = 2;
                wy = staging ? 4 : 1;
                if (key_tile <= 32) {
                    // Too few keys for a step to be worth much, so spend the
                    // block on warps rather than on rows per warp.
                    ty = 1;
                    wy = 8;
                }
            }
        }
        const int rows = tile * ty;
        const int block_rows = rows * wy;
        const bool warp_loop_inside = stage_per_tile && wy > 1;

        Var xo("xo"), yo("yo"), xio("xio"), yio("yio"), xi("xi"), yi("yi");
        Var yw("yw"), rxi("rxi"), ryi("ryi");
        RVar rro("rro"), rri("rri"), rto("rto"), rti("rti");

        // Left to itself the backend spends registers covering the latency of
        // the tensor core operand loads, issuing them far ahead of their use
        // and holding the results until then. That pushes the kernel to two
        // blocks per processor when the shared memory would allow three.
        // Measured on an RTX 5060 Ti: 128 is worth 3.5% over letting it
        // choose, and neighbouring values are worth less.
        out.gpu_max_registers(128);

        // A block owns a strip of queries and every column of the output.
        out.bound(x, 0, out_depth)
            .bound(y, 0, queries)
            .tile(x, y, xo, yo, xi, yi, out_depth, block_rows)
            .split(yi, yw, yi, rows)
            .tile(xi, yi, xio, yio, xi, yi, tile, tile)
            .gpu_blocks(xo, yo)
            .unroll(xio)
            .unroll(yio)
            .tile_store(xi, yi);

        acc.compute_at(out, xo)
            .store_in(MemoryType::Tile)
            .split(y, yw, y, rows)
            .tile(x, y, rxi, ryi, tile, tile)
            .unroll(x)
            .unroll(y)
            .tile_init(rxi, ryi);
        // Two steps at a time, so that which of the two live slots of the
        // carried state a step reads is a constant here.
        acc.update()
            .split(y, yw, y, rows)
            .tile(x, y, rxi, ryi, tile, tile)
            .split(rt, rto, rti, 2)
            .always_partition(rto)
            .unroll(x)
            .unroll(y)
            .unroll(rti)
            .tile_init(rxi, ryi);
        if (warp_loop_inside) {
            acc.update().reorder(x, y, yw, rti, rto);
        } else {
            acc.update().reorder(x, y, rti, rto, yw);
        }

        // Everything below is per key tile, so it lives inside the walk.
        // With the walk outside the loop over warps, a stage in here needs a
        // loop over warps of its own: every thread loop in the walk has to be
        // the same 32 by 4 shape for lowering to fuse them into one thread
        // block, and a stage outside them all is serialized onto one warp.
        //
        // Sharing acc's loop instead saves the barriers that would sit between
        // a stage and its neighbours, but a stage can only do it once all of
        // its consumers are in that loop. tile_acc is read by acc's update
        // directly, so it can; the rest are read by each other.
        for (Func f : {s, e}) {
            f.compute_at(acc, yw)
                .store_in(MemoryType::Tile)
                .hoist_storage(out, xo)
                .tile(x, y, rxi, ryi, tile, tile)
                .unroll(x)
                .unroll(y)
                .tile_init(rxi, ryi);
        }
        tile_acc
            .compute_at(acc, yw)
            .store_in(MemoryType::Tile)
            .hoist_storage(out, xo)
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
            .unroll(rro)
            .tile_matmul(rri, rxi, ryi);

        // Shares acc's loop over warps, so it splits off none of its own.
        tile_acc.update()
            .tile(x, y, rxi, ryi, tile, tile)
            .split(rj, rro, rri, tile)
            .reorder(x, y, rro)
            .unroll(x)
            .unroll(y)
            // This operand comes out of a fragment rather than out of memory,
            // so which tile of it each step reads has to be known here.
            .unroll(rro)
            .tile_matmul(rri, rxi, ryi);

        for (Func f : {tile_max, tile_l}) {
            f.store_in(MemoryType::Tile)
                .compute_at(acc, yw)
                .hoist_storage(out, xo)
                .split(y, y, ryi, tile)
                .unroll(y)
                .vectorize(ryi);
        }
        tile_max.update().split(y, y, ryi, tile).unroll(y).tile_reduce(rj_max, ryi);
        tile_l.update().split(y, y, ryi, tile).unroll(y).tile_reduce(rj, ryi);

        // The carried state. It is stored for the whole walk but computed one
        // step at a time, and folded down to the two tiles that are live. Each
        // warp carries the state for its own rows: with the walk inside the
        // loop over warps that means storing it in there, and with the walk
        // outside it that means storing it above the walk and splitting off a
        // loop over warps here, so each warp writes only its own.
        for (Func f : {m, l}) {
            f.store_in(MemoryType::Tile)
                .store_at(out, xo)
                .compute_at(acc, yw)
                .slide(acc, rt)
                .fold_storage(t, 2)
                .split(y, y, ryi, tile)
                .unroll(y)
                .vectorize(ryi);
        }

        if (wy > 1) {
            out.gpu_threads(yw);
            acc.gpu_threads(yw);
            acc.update().gpu_threads(yw);
        }

        // Every warp walks every key tile, so they all read the same K and V.
        // Staging them into shared memory turns one global load per tensor core
        // operand into one shared load, and spreads what it costs to fetch them
        // over the warps, which is the only thing the warp count buys.
        //
        // They are staged whole rather than a tile at a time, so this only
        // applies while both panels fit in shared memory. A per-tile panel
        // would be the thing to want at a larger key count, but it would have
        // to be filled above the loop over warps, and the carried state has to
        // live inside it. Past the point where they fit, the walk reads its
        // operands from global memory, which is what the numbers above measure.
        if (stage_q) {
            // Q does not depend on the step, so a block's rows of it are
            // fetched once and read from shared memory by every step.
            Var qo("qo"), qv("qv"), qt("qt"), qi("qi"), qto("qto"), qw("qw");
            Q.in()
                .compute_at(out, xo)
                .store_in(MemoryType::GPUSharedAsync)
                .split(_0, qo, qv, vec)
                .fuse(qo, _1, qt)
                .split(qt, qt, qi, 32)
                .split(qt, qto, qw, wy)
                .gpu_lanes(qi)
                .gpu_threads(qw)
                .reorder(qv, qto, qi, qw)
                .unroll(qto)
                .vectorize(qv)
                .align_storage(_0, depth + p);
        }

        if (staging || stage_per_tile) {
            Var ko("ko"), kv("kv"), tt("tt"), ti("ti"), to("to"), tw("tw");

            for (Func f : {K.in(), V.in()}) {
                f.compute_at(acc, rti)
                    .hoist_storage(out, xo)
                    .store_in(MemoryType::GPUSharedAsync)
                    .split(_0, ko, kv, vec)
                    .fuse(ko, _1, tt)
                    .split(tt, tt, ti, 32)
                    .split(tt, to, tw, wy)
                    .gpu_lanes(ti)
                    .gpu_threads(tw)
                    // A thread moves sixteen bytes per asynchronous copy, so a
                    // panel takes more than one of them. Put the loop over them
                    // inside the loops over threads and unroll it, so they are
                    // all issued before anything waits: left outside, each copy
                    // is its own loop over threads and gets its own wait and a
                    // barrier between.
                    .reorder(kv, to, ti, tw)
                    .unroll(to)
                    .vectorize(kv);
            }
            K.in().compute_with(V.in(), to);
            K.in().align_storage(_0, depth + p);
            V.in().align_storage(_0, out_depth + p);
        }
    }

private:
    Var x{"x"}, y{"y"}, t{"t"};
    RDom k, rj_max, rj, rt;
    int num_tiles = 0, key_tile = 0, key_pad = 0;
    Func s{"s"}, tile_max{"tile_max"}, e{"e"}, tile_l{"tile_l"};
    Func tile_acc{"tile_acc"}, acc{"acc"};
    Func m{Float(32), "m"}, l{Float(32), "l"};
};


// The flash filter with no inductive Funcs: the running maximum cannot
// advance alongside the accumulator's walk (an update definition owns its
// whole walk, compute_with forbids dependent stages, and one Tuple would
// force every reduction inline and off the tensor cores), so the softmax
// takes two passes. The first walks the key tiles to find each row's
// maximum; the second walks them again with that maximum known, so the
// weights need no rescaling and the row sum rides as a second component
// of the accumulator. Same tile verbs, staging, and warp layout as the
// flash filter; the price is the scores computed twice.
class AttentionFlashRDom : public Halide::Generator<AttentionFlashRDom> {
public:
    GeneratorParam<int> queries{"queries", 16384};
    GeneratorParam<int> keys{"keys", 64};
    GeneratorParam<int> depth{"depth", 64};
    GeneratorParam<int> out_depth{"out_depth", 64};
    GeneratorParam<int> tiles_y{"tiles_y", 0};
    GeneratorParam<int> warps{"warps", 0};
    GeneratorParam<int> chunk{"chunk", 0};
    GeneratorParam<bool> stage{"stage", true};
    GeneratorParam<bool> stage_tile{"stage_tile", true};
    GeneratorParam<bool> stage_q{"stage_q", true};
    GeneratorParam<int> pad{"pad", 0};

    Input<Buffer<float16_t, 2>> Q{"Q"};
    Input<Buffer<float16_t, 2>> K{"K"};
    Input<Buffer<float16_t, 2>> V{"V"};

    Output<Buffer<float, 2>> out{"out"};

    void generate() {
        key_tile = chunk ? (int)chunk : std::min(64, (int)keys / 2);
        _halide_user_assert(key_tile % 16 == 0 && keys % key_tile == 0 &&
                            keys / key_tile >= 2)
            << "chunk must be a multiple of 16 that divides keys at least twice";
        num_tiles = keys / key_tile;
        key_pad = 2 * key_tile;

        k = RDom(0, depth, "k");
        rj_max = RDom(0, key_tile, "rj_max");
        rj = RDom(0, key_tile, "rj");
        rt = RDom(0, num_tiles, "rt");

        // The scores of a key tile, defined once. Both passes read them;
        // that the second recomputes them rather than keeping the first's
        // is a scheduling decision (clone_in, below).
        s(x, y, t) = 0.f;
        s(x, y, t) += cast<float>(Q(k, y)) * cast<float>(K(k, t * key_tile + x));

        // Pass one: each tile's row maximum, and the running maximum over
        // the walk.
        tile_max(y, t) = -1e30f;
        tile_max(y, t) = max(tile_max(y, t), s(rj_max, y, t));

        mfin(y) = -1e30f;
        mfin(y) = max(mfin(y), tile_max(y, rt));

        // Pass two: the weights against the final maximum, so nothing
        // carried needs rescaling.
        e(x, y, t) = exp(s(x, y, t) - mfin(y));

        tile_l(y, t) = 0.f;
        tile_l(y, t) += e(rj, y, t);

        tile_acc(x, y, t) = 0.f;
        tile_acc(x, y, t) +=
            cast<float>(e(rj, y, t)) * cast<float>(V(x, t * key_tile + rj));

        // The row sum and the accumulator are two update definitions over
        // the same walk; they do not depend on each other, so their loops
        // fuse (compute_with) and each step's weights serve both. Neither
        // can read the other mid-walk, so the normalization is its own
        // fragment pass after the walk.
        l(y) = 0.f;
        l(y) += tile_l(y, rt);

        acc(x, y) = 0.f;
        acc(x, y) += tile_acc(x, y, rt);

        outf(x, y) = acc(x, y) / l(y);
        out(x, y) = outf(x, y);
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
        // Nothing here reads before the first key or after the last, so the
        // panels are taken as they are, unpadded.
        set_bounds(K, depth, keys);
        set_bounds(V, out_depth, keys);
        set_bounds(out, out_depth, queries);

        const int tile = 16;
        const int vec = 8;
        const int p = pad ? (int)pad : vec;
        const int staged_bytes = 2 * (int)keys * ((int)depth + (int)out_depth + 2 * p);
        const bool staging = stage && staged_bytes <= 40 * 1024;
        const bool stage_per_tile = stage_tile && !staging;

        int ty = tiles_y, wy = warps;
        if (ty == 0 || wy == 0) {
            // The flash filter's shape, measured to suit this form as well
            // except for the step, which is narrower (see the Makefile).
            if (stage_per_tile) {
                ty = 1;
                wy = 4;
            } else {
                ty = 2;
                wy = staging ? 4 : 1;
                if (key_tile <= 32) {
                    ty = 1;
                    wy = 8;
                }
            }
        }
        const int rows = tile * ty;
        const int block_rows = rows * wy;
        const bool warp_loop_inside = stage_per_tile && wy > 1;

        Var xo("xo"), yo("yo"), xio("xio"), yio("yio"), xi("xi"), yi("yi");
        Var yw("yw"), rxi("rxi"), ryi("ryi");
        RVar rro("rro"), rri("rri"), rto("rto"), rti("rti"), r1o("r1o"), r1i("r1i");

        out.gpu_max_registers(128);

        out.bound(x, 0, out_depth)
            .bound(y, 0, queries)
            .tile(x, y, xo, yo, xi, yi, out_depth, block_rows)
            .split(yi, yw, yi, rows)
            .tile(xi, yi, xio, yio, xi, yi, tile, tile)
            .gpu_blocks(xo, yo)
            .unroll(xio)
            .unroll(yio)
            .tile_store(xi, yi);

        // ---- pass one: the row maxima, one walk over the key tiles ----
        mfin.store_in(MemoryType::Tile)
            .compute_at(out, xo)
            .split(y, yw, y, rows)
            .split(y, y, ryi, tile)
            .unroll(y)
            .vectorize(ryi);
        mfin.update()
            .split(y, yw, y, rows)
            .split(y, y, ryi, tile)
            .split(rt, r1o, r1i, 2)
            .unroll(y)
            .unroll(r1i)
            .vectorize(ryi);
        if (warp_loop_inside) {
            mfin.update().reorder(ryi, y, yw, r1i, r1o);
        } else {
            mfin.update().reorder(ryi, y, r1i, r1o, yw);
        }

        // The first pass gets its own copy of the scores, recomputed
        // rather than kept: a block's strip of them is 256 KB, too big for
        // shared memory and a write-back of the whole matrix if held in
        // global memory, where one more tensor-core product per tile is
        // cheaper.
        Func s1 = s.clone_in(tile_max);
        s1.compute_at(mfin, yw)
            .store_in(MemoryType::Tile)
            .hoist_storage(out, xo)
            .tile(x, y, rxi, ryi, tile, tile)
            .unroll(x)
            .unroll(y)
            .tile_init(rxi, ryi);
        s1.update()
            .tile(x, y, rxi, ryi, tile, tile)
            .split(k, rro, rri, tile)
            .reorder(x, y, rro)
            .unroll(x)
            .unroll(y)
            .unroll(rro)
            .tile_matmul(rri, rxi, ryi);

        tile_max.store_in(MemoryType::Tile)
            .compute_at(mfin, yw)
            .hoist_storage(out, xo)
            .split(y, y, ryi, tile)
            .unroll(y)
            .vectorize(ryi);
        tile_max.update().split(y, y, ryi, tile).unroll(y).tile_reduce(rj_max, ryi);

        // ---- pass two: the weighted walk ----
        acc.compute_at(out, xo)
            .store_in(MemoryType::Tile)
            .split(y, yw, y, rows)
            .tile(x, y, rxi, ryi, tile, tile)
            .unroll(x)
            .unroll(y)
            .tile_init(rxi, ryi);
        acc.update()
            .split(y, yw, y, rows)
            .tile(x, y, rxi, ryi, tile, tile)
            .split(rt, rto, rti, 2)
            .unroll(x)
            .unroll(y)
            .unroll(rti)
            .tile_init(rxi, ryi);
        if (warp_loop_inside) {
            acc.update().reorder(x, y, yw, rti, rto);
        } else {
            acc.update().reorder(x, y, rti, rto, yw);
        }

        for (Func f : {s, e}) {
            f.compute_at(acc, yw)
                .store_in(MemoryType::Tile)
                .hoist_storage(out, xo)
                .tile(x, y, rxi, ryi, tile, tile)
                .unroll(x)
                .unroll(y)
                .tile_init(rxi, ryi);
        }
        tile_acc
            .compute_at(acc, yw)
            .store_in(MemoryType::Tile)
            .hoist_storage(out, xo)
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
            .unroll(rro)
            .tile_matmul(rri, rxi, ryi);

        tile_acc.update()
            .tile(x, y, rxi, ryi, tile, tile)
            .split(rj, rro, rri, tile)
            .reorder(x, y, rro)
            .unroll(x)
            .unroll(y)
            .unroll(rro)
            .tile_matmul(rri, rxi, ryi);

        tile_l.store_in(MemoryType::Tile)
            .compute_at(acc, yw)
            .hoist_storage(out, xo)
            .split(y, y, ryi, tile)
            .unroll(y)
            .vectorize(ryi);
        tile_l.update().split(y, y, ryi, tile).unroll(y).tile_reduce(rj, ryi);

        // The row sum's walk, shaped like the accumulator's from the outside
        // in so the two fuse at the step; acc is the parent, so the per-step
        // Funcs above sit in its loop and l is computed after it each step.
        l.store_in(MemoryType::Tile)
            .compute_at(out, xo)
            .split(y, yw, y, rows)
            .split(y, y, ryi, tile)
            .unroll(y)
            .vectorize(ryi);
        l.update()
            .split(y, yw, y, rows)
            .split(y, y, ryi, tile)
            .split(rt, rto, rti, 2)
            .unroll(y)
            .unroll(rti)
            .vectorize(ryi);
        if (warp_loop_inside) {
            l.update().reorder(ryi, y, yw, rti, rto);
        } else {
            l.update().reorder(ryi, y, rti, rto, yw);
        }
        l.update().compute_with(acc.update(), yw);

        // Normalize once the walk is done: an elementwise pass over the
        // accumulator fragments with the row sums broadcast.
        outf.compute_at(out, xo)
            .store_in(MemoryType::Tile)
            .split(y, yw, y, rows)
            .tile(x, y, rxi, ryi, tile, tile)
            .unroll(x)
            .unroll(y)
            .tile_init(rxi, ryi);

        if (wy > 1) {
            out.gpu_threads(yw);
            mfin.gpu_threads(yw);
            mfin.update().gpu_threads(yw);
            acc.gpu_threads(yw);
            acc.update().gpu_threads(yw);
            l.gpu_threads(yw);
            l.update().gpu_threads(yw);
            outf.gpu_threads(yw);
        }

        if (stage_q) {
            Var qo("qo"), qv("qv"), qt("qt"), qi("qi"), qto("qto"), qw("qw");
            Q.in()
                .compute_at(out, xo)
                .store_in(MemoryType::GPUSharedAsync)
                .split(_0, qo, qv, vec)
                .fuse(qo, _1, qt)
                .split(qt, qt, qi, 32)
                .split(qt, qto, qw, wy)
                .gpu_lanes(qi)
                .gpu_threads(qw)
                .reorder(qv, qto, qi, qw)
                .unroll(qto)
                .vectorize(qv)
                .align_storage(_0, depth + p);
        }

        if (staging || stage_per_tile) {
            Var ko("ko"), kv("kv"), tt("tt"), ti("ti"), to("to"), tw("tw");
            // Each pass stages the key tiles it walks; the second also the
            // value tiles. K.in(s1) is the first pass's own wrapper.
            auto stage_panel = [&](Func f, Func at, RVar level, int width) {
                f.compute_at(at, level)
                    .hoist_storage(out, xo)
                    .store_in(MemoryType::GPUSharedAsync)
                    .split(_0, ko, kv, vec)
                    .fuse(ko, _1, tt)
                    .split(tt, tt, ti, 32)
                    .split(tt, to, tw, wy)
                    .gpu_lanes(ti)
                    .gpu_threads(tw)
                    .reorder(kv, to, ti, tw)
                    .unroll(to)
                    .vectorize(kv)
                    .align_storage(_0, width + p);
            };
            Func K1 = K.in(s1);
            stage_panel(K1, mfin, r1i, depth);
            Func K2 = K.in(s);
            Func V2 = V.in();
            stage_panel(K2, acc, rti, depth);
            stage_panel(V2, acc, rti, out_depth);
            K2.compute_with(V2, to);
        }
    }

private:
    Var x{"x"}, y{"y"}, t{"t"};
    RDom k, rj_max, rj, rt;
    int num_tiles = 0, key_tile = 0, key_pad = 0;
    Func tile_max{"tile_max"}, mfin{Float(32), "mfin"};
    Func s{"s"}, e{"e"}, tile_l{"tile_l"}, l{Float(32), "l"}, tile_acc{"tile_acc"}, acc{"acc"}, outf{"outf"};
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Attention, attention)
HALIDE_REGISTER_GENERATOR(AttentionSoftmax, attention_softmax)
HALIDE_REGISTER_GENERATOR(AttentionFlash, attention_flash)
HALIDE_REGISTER_GENERATOR(AttentionFlashRDom, attention_flash_rdom)
