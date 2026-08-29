// Mamba-2's SSD recurrence, scheduled for the tensor cores.
//
// The recurrence is
//
//   h_n = a_n h_{n-1} + (delta_n B_n) x_n^T      h_n is a state x channels matrix
//   y_n = C_n^T h_n
//
// with a_n = exp(delta_n A) a scalar per timestep. Written out step by step it
// is a scan, but blocked into chunks it becomes four matrix multiplies per
// chunk plus a small amount of state carried between them, which is what makes
// it worth putting on tensor cores at all. Within a chunk:
//
//   qk          = C^T B            chunk x state x chunk
//   y_intra     = (qk * decay * dt) X   chunk x chunk x channels
//   chunk_state = (B * dt * decay) X     state x chunk x channels
//   y_inter     = C^T H            chunk x state x channels
//
// where H is the state left by every earlier chunk. The first two are the same
// shape as attention's QK^T and scores*V; the second two are what attention
// does not have, and they are what carries the sequence forward.
//
// The chunks of one sequence have to be walked in order, so all of the
// parallelism is across sequences: one block per (batch * head), walking the
// whole sequence, holding H between chunks.
//
// Two things here are scans, and both are written as inductive Funcs: the
// decay accumulated across a chunk, and the state carried between chunks.
// Ground either one out into a reduction and it costs what it is scanning over
// per element rather than one step - see the inductive generator param.
//
// TODO before this app is fit to land:
//  - The chunk scan has to sit above the loops over threads, because the state
//    below reads it there, so every thread runs its own copy of it into one
//    shared buffer. It belongs in Register memory, one copy per thread, and is
//    rejected there: check_gpu_cross_talk cannot see that a scan reading its
//    own previous element is reading something this thread wrote.
//  - The scores are held at half precision sooner than the arithmetic wants,
//    because a block is capped at 48KB of shared memory: the CUDA runtime
//    never asks for the larger carveout with cuFuncSetAttribute, so the ~100KB
//    the hardware has is out of reach.
//  - The tensor core schedule (wmma=true) compiles and launches. It traps in
//    the kernel on
//      assert(t <= 0, halide_error_fold_factor_too_small("H", "t", 1, t, 2))
//    The state wants one slot that each chunk reads and then overwrites.
//    Storage folding keeps a window of the last N values instead, and a
//    recurrence that reads the value before it needs two of those, so
//    fold_storage(t, 1) is not refused but checked at runtime, and the check
//    fails from the second chunk on. Left at two, which slot holds the state
//    alternates with the chunk, and reading a tensor core accumulator at an
//    alternating slice is not supported.
//
//    So either storage folding learns that a producer which reads only the
//    value before it can overwrite it in place, or sliding learns to rewind
//    when the loop it slides over has been split with the inner part
//    unrolled, which would make the alternating slice a constant per copy.

#include "Halide.h"

namespace {

using namespace Halide;

class Mamba2 : public Halide::Generator<Mamba2> {
public:
    GeneratorParam<int> seq{"seq", 4096};          // sequence length
    GeneratorParam<int> state{"state", 64};        // P, the state dimension
    GeneratorParam<int> channels{"channels", 64};  // D, channels in a head
    GeneratorParam<int> chunk{"chunk", 64};        // how long a chunk is
    GeneratorParam<int> heads{"heads", 128};       // batch * heads
    // Whether the two scans are written as inductive Funcs or ground out into
    // reductions. Off, each one costs what it is scanning over per element
    // instead of one step, which is what this app is here to show.
    GeneratorParam<bool> inductive{"inductive", true};
    // Whether the four multiplies go to the tensor cores.
    GeneratorParam<bool> wmma{"wmma", false};
    GeneratorParam<int> warps{"warps", 4};
    // Whether to take the reference implementation's shape instead: a kernel
    // per stage, each of them parallel over chunks as well as heads, with the
    // state's recurrence in a kernel of its own and everything that crosses a
    // stage going through memory. Fused, a head's chunks are walked in order
    // by one block, which is where the parallelism goes.
    GeneratorParam<bool> split{"split", false};

    // Channels by sequence by head.
    Input<Buffer<float16_t, 3>> X{"X"};
    // State by sequence by head.
    Input<Buffer<float16_t, 3>> Bm{"Bm"};
    Input<Buffer<float16_t, 3>> Cm{"Cm"};
    // The step size, one per timestep, and the decay parameter, one per head.
    Input<Buffer<float, 2>> Delta{"Delta"};
    Input<Buffer<float, 1>> A{"A"};

    // Channels by position in a chunk by chunk by head.
    Output<Buffer<float16_t, 4>> out{"out"};

    void generate() {
        Var d("d"), p("p"), k("k"), idx("idx"), jj("jj"), t("t"), b("b"), n("n");

        // A tile of the step size is read as a matrix with a leading dimension
        // of zero, which reads eight bytes at a time and so needs the row it
        // starts at to be on an eight byte boundary. Nothing about where a
        // buffer starts or how far apart its rows are is known otherwise, so
        // say it.
        Delta.dim(0).set_min(0);
        Delta.dim(1).set_stride((int)seq);

        // Everything that feeds a multiply is held at half precision, and
        // every multiply accumulates at single. That is what the tensor cores
        // do, and what the reference implementation does: the state is carried
        // at single precision and narrowed only where it meets a matmul.
        // The step size is folded into whichever operand of each multiply is
        // already being scaled - the scores for the chunk's own contribution,
        // and the state's B for what it leaves behind - rather than into the
        // input, which would be a tensor the size of X to write and read back.
        // The multiplies then read the input as it was given, which is what
        // the reference does too.

        // How much decay has accumulated from the start of a chunk to each
        // position in it, in the log domain, where the ratio every use wants
        // is a difference and a long chunk cannot underflow.
        //
        // This is a scan, and writing it as one is the point: as an ordinary
        // reduction each position would re-add every earlier step, costing a
        // chunk's length per position instead of one step.
        Func cumdelta = Func(Float(32), "cumdelta");
        RDom rm(0, chunk, "rm");
        if (inductive) {
            cumdelta(k, t, b) = Delta(t * chunk + k, b) +
                                select(k <= 0, 0.f, likely(cumdelta(k - 1, t, b)));
        } else {
            cumdelta(k, t, b) += select(rm <= k, Delta(t * chunk + rm, b), 0.f);
        }

        // The decay from just after position j to position i.
        auto decay = [&](Expr from, Expr to, Expr tt, Expr bb) {
            return exp(A(bb) * (cumdelta(to, tt, bb) - cumdelta(from, tt, bb)));
        };

        // The chunk's own scores: C^T B.
        RDom rp(0, state, "rp");
        rp_var = rp.x;
        Func qk("qk");
        qk(jj, idx, t, b) = 0.f;
        qk(jj, idx, t, b) += cast<float>(Cm(rp, t * chunk + idx, b)) *
                             cast<float>(Bm(rp, t * chunk + jj, b));

        // Masking is an elementwise multiply by a tile of the mask, rather
        // than a test on the coordinates of each entry, which the lanes of a
        // warp hold in an order the schedule does not name.
        // Masked to be causal and weighted by the decay between the two
        // positions, then narrowed for the multiply that consumes it.
        Func score("score");
        score(jj, idx, t, b) = cast<float16_t>(
            select(jj <= idx,
                   qk(jj, idx, t, b) * decay(jj, idx, t, b) *
                       Delta(t * chunk + jj, b),
                   0.f));

        RDom rj(0, chunk, "rj");
        rj_var = rj.x;
        RDom rjy(0, chunk, "rjy");
        rjy.where(rjy / 16 <= idx / 16);
        rjy_var = rjy.x;
        Func y_intra("y_intra");
        y_intra(d, idx, t, b) = 0.f;
        y_intra(d, idx, t, b) += cast<float>(score(rjy, idx, t, b)) *
                                 cast<float>(X(d, t * chunk + rjy, b));

        // The state's operand, scaled by the step size and decayed to the end
        // of its chunk, so that what the chunk leaves behind is a plain
        // product of two operands.
        Func Bmd("Bmd");
        Bmd(p, jj, t, b) = cast<float16_t>(cast<float>(Bm(p, t * chunk + jj, b)) *
                                           Delta(t * chunk + jj, b) *
                                           decay(jj, chunk - 1, t, b));

        Func chunk_state("chunk_state");
        chunk_state(p, d, t, b) = 0.f;
        chunk_state(p, d, t, b) += cast<float>(Bmd(p, rj, t, b)) *
                                   cast<float>(X(d, t * chunk + rj, b));

        // Every earlier chunk's state, decayed to the end of this one. Carried
        // at single precision, and the only recurrence that crosses a chunk.
        // Indexed by the chunk that reads it rather than the chunk that
        // finished it, so every consumer wants it at the chunk it is already
        // on. Written the other way round it is only ever read one chunk back,
        // which takes a conditional to say, and leaves the window it slides
        // over reaching past anything that gets written.
        Func H = Func(Float(32), "H");
        H(p, d, t, b) = select(t <= 0,
                               0.f,
                               likely(H(p, d, t - 1, b) *
                                      exp(A(b) * cumdelta(chunk - 1, t - 1, b))) +
                                   chunk_state(p, d, t - 1, b));

        // The state goes into the multiply as it is carried, at full
        // precision. Building an operand out of an accumulator narrows it to
        // the precision the matrix unit wants, so there is nothing to narrow
        // here, and the recurrence itself stays exact.
        //
        // Once the recurrence is a kernel of its own the state reaches the
        // multiply through memory rather than out of an accumulator, so
        // nothing narrows it on the way and it needs saying, which is what the
        // reference implementation does too.
        Func Hop("Hop");
        Hop(p, d, t, b) = cast<float16_t>(H(p, d, t, b));

        Func y_inter("y_inter");
        y_inter(d, idx, t, b) = 0.f;
        if (split) {
            y_inter(d, idx, t, b) += cast<float>(Cm(rp, t * chunk + idx, b)) *
                                     cast<float>(Hop(rp, d, t, b));
        } else {
            y_inter(d, idx, t, b) += cast<float>(Cm(rp, t * chunk + idx, b)) *
                                     H(rp, d, t, b);
        }

        // The two halves of the answer, summed and scaled. Kept apart from the
        // output so that a schedule can put the sum on a tile and leave the
        // output as the copy that takes it to global memory. Inlines into the
        // output when nothing says otherwise.
        Func y("y");
        y(d, idx, t, b) = cast<float16_t>(
            y_intra(d, idx, t, b) +
            y_inter(d, idx, t, b) * exp(A(b) * cumdelta(idx, t, b)));

        out(d, idx, t, b) = y(d, idx, t, b);

        try {
            if (!using_autoscheduler()) {
                if (split) {
                    schedule_triton(d, p, k, idx, jj, t, b, Bmd, cumdelta, qk,
                                    score, y_intra, chunk_state, H, Hop, y_inter, y);
                } else if (wmma) {
                    schedule_wmma(d, p, k, idx, jj, t, b, Bmd, cumdelta, qk,
                                  score, y_intra, chunk_state, H, y_inter, y);
                } else {
                    schedule_gpu(d, p, k, idx, jj, t, b, cumdelta, qk, score,
                                 y_intra, chunk_state, H, y_inter);
                }
            }
        } catch (Halide::CompileError &e) {
            std::cerr << e.what() << "\n";
        }
    }

private:
    // One block per head, walking that head's chunks in order and carrying
    // the state between them. Everything a chunk needs is computed inside the
    // walk, so the only thing that leaves the block is the output.
    void schedule_gpu(Var d, Var p, Var k, Var idx, Var jj, Var t, Var b,
                      Func cumdelta, Func qk, Func score,
                      Func y_intra, Func chunk_state, Func H, Func y_inter) {
        Var xo("xo"), yo("yo"), xi("xi"), yi("yi");

        // The threads sit above the serial tiles each of them walks, so that
        // a per-thread value can be computed once for a whole chunk.
        out
            .reorder(d, idx, t, b)
            .tile(d, idx, xo, yo, xi, yi, 4, 4)
            .gpu_blocks(b)
            .gpu_threads(xo, yo);

        // Only two things are shared by the threads of a block: the scores,
        // which every row of the output reduces over, and the state, which
        // every channel of the output reduces over. Both are written by one
        // set of threads and read by another.
        score.compute_at(out, t)
            .store_in(MemoryType::GPUShared)
            .tile(jj, idx, xo, yo, xi, yi, 4, 4)
            .gpu_threads(xo, yo);
        qk.compute_at(score, xi).store_in(MemoryType::Register);
        qk.update().reorder(rp_var, jj, idx);

        H.compute_at(out, t)
            .store_at(out, b)
            .store_in(MemoryType::GPUShared)
            .tile(p, d, xo, yo, xi, yi, 4, 4)
            .gpu_threads(xo, yo);

        // The rest is each thread's own business: it reduces into a value it
        // alone wants, so it can keep it in registers.
        chunk_state.compute_at(H, xi).store_in(MemoryType::Register);
        chunk_state.update().reorder(rj_var, p, d);
        y_intra.compute_at(out, xi).store_in(MemoryType::Register);
        y_intra.update().reorder(rjy_var, d, idx);
        y_inter.compute_at(out, xi).store_in(MemoryType::Register);
        y_inter.update().reorder(rp_var, d, idx);

        // The scan within a chunk is short, and every thread running its own
        // copy is cheaper than the barriers sharing one would need. a_f and Xb
        // are cheap enough to fold into their uses.
        // HACK: the scan has to live above the loops over threads, because
        // the state below reads it there. That leaves every thread running its
        // own copy of it into one shared buffer. Each thread writes a site
        // before it reads it and they all write the same value, so the answer
        // is right, but it is a race a reader has to reason about, and the
        // threads repeat work they could share. Putting it in Register memory
        // instead is what one would want, and is rejected: the cross-talk
        // check cannot see that a scan reading its own previous element is
        // reading something this thread wrote.
        cumdelta.compute_at(out, t).store_in(MemoryType::GPUShared);
        if (!inductive) {
            cumdelta.gpu_threads(k);
            cumdelta.update().gpu_threads(k);
        }

        // ---------------------------------------------------------------
        // Estimates and bounds
        // ---------------------------------------------------------------
        const int num_chunks = (int)seq / (int)chunk;
        out.bound(d, 0, channels)
            .bound(idx, 0, chunk)
            .bound(t, 0, num_chunks)
            .bound(b, 0, heads);
    }

    // The reference implementation's shape: a kernel per stage, each parallel
    // over chunks as well as heads, and the recurrence over chunks in a kernel
    // of its own where the parallelism is over the state and the channels
    // instead. Everything that crosses a stage goes through memory, which buys
    // the parallelism the fused walk gives up.
    void schedule_triton(Var d, Var p, Var k, Var idx, Var jj, Var t, Var b,
                         Func Bmd, Func cumdelta, Func qk, Func score,
                         Func y_intra, Func chunk_state, Func H, Func Hop,
                         Func y_inter, Func y) {
        const int tile = 16;
        Var xo("xo"), xi("xi"), rxi("rxi"), ryi("ryi"), n_var("n");
        Var po("po"), pi("pi"), ddo("ddo"), ddi("ddi");
        RVar rro("rro"), rri("rri");

        // The scan within a chunk, one thread per chunk. Its rows are padded to
        // an even length, so that a tile of the decay can be read as a matrix
        // with a leading dimension of zero rather than selected out of a
        // broadcast vector lane by lane, which wants an eight byte boundary.
        cumdelta.compute_root().align_bounds(k, 2).align_storage(k, 2)
            .reorder(k, t, b).gpu_blocks(b).gpu_threads(t);
        if (!inductive) {
            cumdelta.update().reorder(k, t, b).gpu_blocks(b).gpu_threads(t);
        }

        // What each chunk leaves behind, which is a plain product once the
        // input has been decayed to the end of the chunk.
        Func cs = chunk_state.in();
        cs.compute_root()
            .tile(p, d, rxi, ryi, tile, tile)
            .reorder(rxi, ryi, p, d, t, b)
            .unroll(p)
            .gpu_blocks(t, b)
            .gpu_threads(d)
            .tile_store(rxi, ryi);
        chunk_state.compute_at(cs, t)
            .store_in(MemoryType::Tile)
            .tile(p, d, rxi, ryi, tile, tile)
            .unroll(p)
            .gpu_threads(d)
            .tile_init(rxi, ryi);
        chunk_state.update()
            .tile(p, d, rxi, ryi, tile, tile)
            .split(rj_var, rro, rri, tile)
            .reorder(p, d, rro)
            .unroll(p)
            .gpu_threads(d)
            .tile_matmul(rri, rxi, ryi);
        {
            Var so("so"), si("si"), to_("to"), ti_("ti");
            Bmd.compute_at(cs, t)
                .store_in(MemoryType::GPUShared)
                .split(p, so, si, 32)
                .split(jj, to_, ti_, tile)
                .reorder(si, so, ti_, to_)
                .gpu_lanes(si)
                .gpu_threads(to_);
        }

        // The recurrence over chunks. The chunks have to be walked in order,
        // so the parallelism comes from the state and the channels instead,
        // one serial walk per entry of the state. Only the narrowed state
        // reaches memory; the carry is the single precision Func slid over the
        // walk, which leaves it a few registers per thread. Its store level is
        // outside the thread loops, so it needs saying that it is a thread's
        // own and not something to put in shared.
        Var pt("pt"), pe("pe"), hpt("hpt"), hpe("hpe"), hto("hto"), hti("hti");
        Hop.compute_root()
            .split(p, pt, pe, 4)
            .split(d, ddo, ddi, 8)
            .split(t, hto, hti, 1)
            .reorder(pe, pt, ddi, hti, hto, ddo, b)
            .gpu_blocks(ddo, b)
            .gpu_threads(pt, ddi)
            .vectorize(pe);
        H.compute_at(Hop, hti)
            .store_at(Hop, ddo)
            .slide(Hop, t)
            .store_in(MemoryType::Stack)
            .split(p, hpt, hpe, 4)
            .reorder(hpe, hpt, d)
            .gpu_threads(hpt, d)
            .vectorize(hpe);

        // The output. The scores are masked and decayed here, on the way in
        // from memory, and the state is narrowed here too. The sum of the two
        // halves happens on the tile the multiplies left them in, and the
        // output is the copy that takes it from there to global memory.
        out
            .compute_root()
            .tile(d, idx, rxi, ryi, tile, tile)
            .reorder(rxi, ryi, d, idx, t, b)
            .unroll(idx)
            .gpu_blocks(t, b)
            .gpu_threads(d)
            .tile_store(rxi, ryi);
        for (Func f : {y_intra, y_inter, y}) {
            f.compute_at(out, t)
                .store_in(MemoryType::Tile)
                .tile(d, idx, rxi, ryi, tile, tile)
                .gpu_threads(d)
                .unroll(idx)
                .tile_init(rxi, ryi);
        }
        y_intra.update()
            .tile(d, idx, rxi, ryi, tile, tile)
            .split(rjy_var, rro, rri, tile)
            .reorder(d, idx, rro)
            .gpu_threads(d)
            .unroll(idx)
            .tile_matmul(rri, rxi, ryi);
        y_inter.update()
            .tile(d, idx, rxi, ryi, tile, tile)
            .split(rp_var, rro, rri, tile)
            .reorder(d, idx, rro)
            .gpu_threads(d)
            .unroll(idx)
            .tile_matmul(rri, rxi, ryi);
        // The operands come out of global memory with the buffer's own row
        // stride, so a fragment is sixteen separate rows of it, and every
        // operand tile is read again by each tile of the output it meets.
        // Staging them makes the read from global one dense run per row and
        // leaves the multiplies reading a tile that is already dense. The copy
        // is asynchronous, so it wants sixteen bytes per thread and nothing in
        // the way between the load and the store.
        for (Func f : {Func(X).in(y_intra)}) {
            Var so("so"), si("si"), fu("fu"), fo("fo"), fi("fi"), w("w"), l("l");
            f.compute_at(out, t)
                .store_in(MemoryType::GPUSharedAsync)
                .split(f.args()[0], so, si, 8)
                .fuse(so, f.args()[1], fu)
                .split(fu, fo, fi, 128)
                .split(fi, w, l, 32)
                .reorder(si, l, w, fo)
                .vectorize(si)
                .gpu_lanes(l)
                .gpu_threads(w);
        }

        // The scores are a chunk by chunk matrix that the whole block reads,
        // and the block is one chunk of one head, so computing them here costs
        // no more multiplies than a kernel of their own would. Masking and
        // decaying them happens on the tile the multiply left them in, and
        // only the narrowed result reaches shared memory, where the multiply
        // that consumes them reads it as an operand.
        qk.compute_at(out, t)
            .store_in(MemoryType::Tile)
            .tile(jj, idx, rxi, ryi, tile, tile)
            .unroll(jj)
            .gpu_threads(idx)
            .tile_init(rxi, ryi);
        qk.update()
            .tile(jj, idx, rxi, ryi, tile, tile)
            .split(rp_var, rro, rri, tile)
            .reorder(jj, idx, rro)
            .unroll(jj)
            .gpu_threads(idx)
            .tile_matmul(rri, rxi, ryi);
        score.compute_at(out, t)
            .store_in(MemoryType::Tile)
            .tile(jj, idx, rxi, ryi, tile, tile)
            .unroll(jj)
            .gpu_threads(idx)
            .tile_init(rxi, ryi);
        score.in()
            .compute_at(out, t)
            .store_in(MemoryType::GPUShared)
            .tile(jj, idx, rxi, ryi, tile, tile)
            .unroll(jj)
            .gpu_threads(idx)
            .tile_store(rxi, ryi);

        const int num_chunks = (int)seq / (int)chunk;
        out.bound(d, 0, channels)
            .bound(idx, 0, chunk)
            .bound(t, 0, num_chunks)
            .bound(b, 0, heads);
    }

    // Every multiply is a tile of this size on the tensor cores.
    static constexpr int tile = 16;

    // The four multiplies on the tensor cores. The block is one head; its
    // warps split the scores by output position, and everything after them by
    // channel, so that a warp holds every state row for the channels it owns
    // and never has to reduce across warps.
    void schedule_wmma(Var d, Var p, Var k, Var idx, Var jj, Var t, Var b,
                       Func Bmd, Func cumdelta, Func qk, Func score,
                       Func y_intra, Func chunk_state, Func H, Func y_inter,
                       Func y) {
        Var xo("xo"), yo("yo"), xi("xi"), yi("yi"), w("w"), to("to"), ti("ti");
        Var rxi("rxi"), ryi("ryi");
        RVar rro("rro"), rri("rri");

        // A warp takes a tile of channels and every position of the chunk.
        // The state is two chunks wide, and which of the two a chunk reads
        // alternates with it. Take the walk two chunks at a time and unroll
        // that, so each copy names one of them: a fragment is only a register
        // if which register it is, is known.
        // Split the walk by one, so no loop is named after the chunk. The
        // state then slides over the chunk as a dimension rather than over a
        // loop, and the window it needs is measured as what outlives each step
        // rather than what each step asks for.
        out
            .reorder(d, idx, t, b)
            .split(t, to, ti, 1)
            .tile(d, idx, xo, yo, xi, yi, tile, tile)
            .reorder(xi, yi, xo, yo, ti, to, b)
            .gpu_blocks(b)
            .gpu_threads(xo)
            .unroll(yo)
            .tile_store(xi, yi);

        // The scores, split across the warps by output position, and left in
        // shared memory because every warp reduces over all of them.
        qk.compute_at(out, ti)
            .store_in(MemoryType::Tile)
            .tile(jj, idx, rxi, ryi, tile, tile)
            .unroll(jj)
            .gpu_threads(idx)
            .tile_init(rxi, ryi);
        qk.update()
            .tile(jj, idx, rxi, ryi, tile, tile)
            .split(rp_var, rro, rri, tile)
            .reorder(jj, idx, rro)
            .unroll(jj)
            .gpu_threads(idx)
            .tile_matmul(rri, rxi, ryi);

        // The masking and the decay happen on the tile the multiply left them
        // in, and the output is the copy that takes the result to memory.
        score.compute_at(out, ti)
            .store_in(MemoryType::Tile)
            .tile(jj, idx, rxi, ryi, tile, tile)
            .unroll(jj)
            .gpu_threads(idx)
            .tile_init(rxi, ryi);
        score.in()
            .compute_at(out, ti)
            .store_in(MemoryType::GPUShared)
            .tile(jj, idx, rxi, ryi, tile, tile)
            .unroll(jj)
            .gpu_threads(idx)
            .tile_store(rxi, ryi);

        // Everything past the scores is per channel, so a warp owns a tile of
        // channels and keeps its share of the state in its own fragments.
        //
        // The channel is the tile's first axis rather than its second, which
        // makes the state the first operand of the multiply that reads it. An
        // accumulator can be rebuilt in place as a first operand, but not as a
        // second one, so this is what lets the state stay in registers.
        for (Func f : {y_intra, y_inter, y}) {
            f.compute_at(out, ti)
                .store_in(MemoryType::Tile)
                .reorder_storage(idx, d)
                .tile(d, idx, rxi, ryi, tile, tile)
                .gpu_threads(d)
                .unroll(idx)
                .tile_init(ryi, rxi);
        }
        // The state is indexed the other way round, so the warps go on its
        // second dimension to own the same channels they own everywhere else.
        chunk_state.compute_at(out, ti)
            .store_in(MemoryType::Tile)
            .tile(p, d, rxi, ryi, tile, tile)
            .unroll(p)
            .gpu_threads(d)
            .tile_init(rxi, ryi);

        y_intra.update()
            .tile(d, idx, rxi, ryi, tile, tile)
            .split(rjy_var, rro, rri, tile)
            .reorder(d, idx, rro)
            .gpu_threads(d)
            .unroll(idx)
            .tile_matmul(rri, ryi, rxi);
        y_inter.update()
            .tile(d, idx, rxi, ryi, tile, tile)
            .split(rp_var, rro, rri, tile)
            .reorder(d, idx, rro)
            .unroll(rro)
            .gpu_threads(d)
            .unroll(idx)
            .tile_matmul(rri, ryi, rxi);
        chunk_state.update()
            .tile(p, d, rxi, ryi, tile, tile)
            .split(rj_var, rro, rri, tile)
            .reorder(p, d, rro)
            .unroll(p)
            .gpu_threads(d)
            .tile_matmul(rri, rxi, ryi);

        // The state slides over the walk: one chunk's worth is live at a
        // time, and saying so is what keeps which fragment holds it fixed
        // rather than alternating with the chunk.
        H.compute_at(out, ti)
            .store_at(out, b)
            .slide(out, t)
            .store_in(MemoryType::Tile)
            .tile(p, d, rxi, ryi, tile, tile)
            .unroll(p)
            .gpu_threads(d)
            .tile_init(rxi, ryi);

        // Each per-chunk Func has a loop over the chunk it is computing, which
        // runs once. Left alone it stays a variable, and that is enough to stop
        // two accesses from different stages of the same Func being recognised
        // as the same tile. Unrolled, they are constants - except that the slid
        // state's warm-up gives the first iteration a different extent, and a
        // loop whose extent is not constant cannot be unrolled.
        for (Func f : {qk, score, y_intra, y_inter, chunk_state, H}) {
            f.unroll(t);
        }
        for (Func f : {qk, y_intra, y_inter, chunk_state}) {
            f.update().unroll(t);
        }

        // The scan, and the two staged operands the multiplies read.
        cumdelta.compute_at(out, ti).store_in(MemoryType::GPUShared);
        if (!inductive) {
            cumdelta.gpu_threads(k);
            cumdelta.update().gpu_threads(k);
        }
        for (Func f : {X.in(), Bmd}) {
            f.compute_at(out, ti)
                .store_in(MemoryType::GPUShared)
                .split(f.args()[0], xo, xi, tile)
                .gpu_lanes(xi);
        }

        // ---------------------------------------------------------------
        // Estimates and bounds
        // ---------------------------------------------------------------
        const int num_chunks = (int)seq / (int)chunk;
        out.bound(d, 0, channels)
            .bound(idx, 0, chunk)
            .bound(t, 0, num_chunks)
            .bound(b, 0, heads);
    }

    RVar rp_var, rj_var, rjy_var;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Mamba2, mamba2)
