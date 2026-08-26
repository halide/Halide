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
//   y_intra     = (qk * decay) Xb  chunk x chunk x channels
//   chunk_state = B (decay Xb)     state x chunk x channels
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
//  - The tensor core schedule (wmma=true) is not finished. Three of the four
//    multiplies are recognised; the state's own contribution is not, because
//    the prover that checks a fragment's accesses do not partially overlap
//    cannot evaluate the chunk it is indexed by:
//      chunk_state.s0.t - out.s0.t.$n
//    It is held two chunks wide because it feeds the state, which slides, and
//    the prover cannot see that the difference above is zero. This is the same
//    prover that rejects the slid producer in PR 9376.
//  - The narrowed state has to go out to shared memory and back every chunk,
//    because an accumulator fragment and a multiply's second operand are held
//    in different register layouts. Attention never pays this: it reads its
//    accumulator once at the end, where this feeds it back in every chunk.

#include "Halide.h"

namespace {

using namespace Halide;


class Mamba2 : public Halide::Generator<Mamba2> {
public:
    GeneratorParam<int> seq{"seq", 4096};        // sequence length
    GeneratorParam<int> state{"state", 64};      // P, the state dimension
    GeneratorParam<int> channels{"channels", 64};  // D, channels in a head
    GeneratorParam<int> chunk{"chunk", 64};      // how long a chunk is
    GeneratorParam<int> heads{"heads", 128};     // batch * heads
    // Whether the two scans are written as inductive Funcs or ground out into
    // reductions. Off, each one costs what it is scanning over per element
    // instead of one step, which is what this app is here to show.
    GeneratorParam<bool> inductive{"inductive", true};
    // Whether the four multiplies go to the tensor cores.
    GeneratorParam<bool> wmma{"wmma", false};
    GeneratorParam<int> warps{"warps", 4};

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

        const int T = chunk;
        const int num_chunks = (int)seq / (int)chunk;

        // Everything that feeds a multiply is held at half precision, and
        // every multiply accumulates at single. That is what the tensor cores
        // do, and what the reference implementation does: the state is carried
        // at single precision and narrowed only where it meets a matmul.
        Func Xb("Xb");
        Xb(d, n, b) = cast<float16_t>(Delta(n, b) * cast<float>(X(d, n, b)));

        // How much decay has accumulated from the start of a chunk to each
        // position in it, in the log domain, where the ratio every use wants
        // is a difference and a long chunk cannot underflow.
        //
        // This is a scan, and writing it as one is the point: as an ordinary
        // reduction each position would re-add every earlier step, costing a
        // chunk's length per position instead of one step.
        Func cumdelta = Func(Float(32), "cumdelta");
        RDom rm(0, T, "rm");
        if (inductive) {
            cumdelta(k, t, b) = Delta(t * T + k, b) +
                                select(k <= 0, 0.f, likely(cumdelta(k - 1, t, b)));
        } else {
            cumdelta(k, t, b) += select(rm <= k, Delta(t * T + rm, b), 0.f);
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
        qk(jj, idx, t, b) += cast<float>(Cm(rp, t * T + idx, b)) *
                             cast<float>(Bm(rp, t * T + jj, b));

        // Masked to be causal and weighted by the decay between the two
        // positions, then narrowed for the multiply that consumes it.
        Func score("score");
        score(jj, idx, t, b) =
            cast<float16_t>(select(jj <= idx, qk(jj, idx, t, b) * decay(jj, idx, t, b), 0.f));

        RDom rj(0, T, "rj");
        rj_var = rj.x;
        Func y_intra("y_intra");
        y_intra(d, idx, t, b) = 0.f;
        y_intra(d, idx, t, b) += cast<float>(score(rj, idx, t, b)) *
                                 cast<float>(Xb(d, t * T + rj, b));

        // The input decayed to the end of its chunk, so that what this chunk
        // leaves behind is a plain product of two operands.
        Func Xbd("Xbd");
        Xbd(d, jj, t, b) = cast<float16_t>(cast<float>(Xb(d, t * T + jj, b)) *
                                           decay(jj, T - 1, t, b));

        Func chunk_state("chunk_state");
        chunk_state(p, d, t, b) = 0.f;
        chunk_state(p, d, t, b) += cast<float>(Bm(p, t * T + rj, b)) *
                                   cast<float>(Xbd(d, rj, t, b));

        // Every earlier chunk's state, decayed to the end of this one. Carried
        // at single precision, and the only recurrence that crosses a chunk.
        Func H = Func(Float(32), "H");
        H(p, d, t, b) = select(t <= 0,
                               0.f,
                               likely(H(p, d, t - 1, b) *
                                      exp(A(b) * cumdelta(T - 1, t, b)))) +
                        chunk_state(p, d, t, b);

        // The state is narrowed where it meets the multiply, not where it is
        // carried, which is what keeps the recurrence itself at full precision.
        // It is its own Func so that the multiply below reads it plainly: an
        // operand that is a cast of a load is not a matrix multiply operand.
        Func H16("H16");
        H16(p, d, t, b) = cast<float16_t>(H(p, d, t - 1, b));

        Func y_inter("y_inter");
        y_inter(d, idx, t, b) = 0.f;
        y_inter(d, idx, t, b) += cast<float>(Cm(rp, t * T + idx, b)) *
                                 cast<float>(H16(rp, d, t, b));

        out(d, idx, t, b) = cast<float16_t>(
            y_intra(d, idx, t, b) +
            select(t > 0,
                   y_inter(d, idx, t, b) * exp(A(b) * cumdelta(idx, t, b)),
                   0.f));

        // ---------------------------------------------------------------
        // Estimates and bounds
        // ---------------------------------------------------------------
        out.bound(d, 0, channels)
            .bound(idx, 0, T)
            .bound(t, 0, num_chunks)
            .bound(b, 0, heads);

        if (!using_autoscheduler()) {
            if (wmma) {
                schedule_wmma(d, p, k, idx, jj, t, b, Xb, Xbd, cumdelta, qk,
                              score, y_intra, chunk_state, H, H16, y_inter);
            } else {
                schedule_gpu(d, p, k, idx, jj, t, b, Xb, cumdelta, qk, score,
                             y_intra, chunk_state, H, H16, y_inter);
            }
        }
    }

private:
    // One block per head, walking that head's chunks in order and carrying
    // the state between them. Everything a chunk needs is computed inside the
    // walk, so the only thing that leaves the block is the output.
    void schedule_gpu(Var d, Var p, Var k, Var idx, Var jj, Var t, Var b,
                      Func Xb, Func cumdelta, Func qk, Func score,
                      Func y_intra, Func chunk_state, Func H, Func H16, Func y_inter) {
        Var xo("xo"), yo("yo"), xi("xi"), yi("yi");

        // The threads sit above the serial tiles each of them walks, so that
        // a per-thread value can be computed once for a whole chunk.
        out.reorder(d, idx, t, b)
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
        y_intra.update().reorder(rj_var, d, idx);
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
    }

    // Every multiply is a tile of this size on the tensor cores.
    static constexpr int tile = 16;

    // The four multiplies on the tensor cores. The block is one head; its
    // warps split the scores by output position, and everything after them by
    // channel, so that a warp holds every state row for the channels it owns
    // and never has to reduce across warps.
    void schedule_wmma(Var d, Var p, Var k, Var idx, Var jj, Var t, Var b,
                       Func Xb, Func Xbd, Func cumdelta, Func qk, Func score,
                       Func y_intra, Func chunk_state, Func H, Func H16, Func y_inter) {
        Var xo("xo"), yo("yo"), xi("xi"), yi("yi"), w("w");
        Var rxi("rxi"), ryi("ryi");
        RVar rro("rro"), rri("rri");


        // A warp takes a tile of channels and every position of the chunk.
        out.reorder(d, idx, t, b)
            .tile(d, idx, xo, yo, xi, yi, tile, tile)
            .gpu_blocks(b)
            .gpu_threads(xo)
            .unroll(yo)
            .tile_store(xi, yi);

        // The scores, split across the warps by output position, and left in
        // shared memory because every warp reduces over all of them.
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
            .store_in(MemoryType::GPUShared)
            .tile(jj, idx, rxi, ryi, tile, tile)
            .unroll(jj)
            .gpu_threads(idx)
            .tile_store(rxi, ryi);

        // Everything past the scores is per channel, so a warp owns a tile of
        // channels and keeps its share of the state in its own fragments.
        for (Func f : {y_intra, y_inter}) {
            f.compute_at(out, t)
                .store_in(MemoryType::Tile)
                .tile(d, idx, rxi, ryi, tile, tile)
                .gpu_threads(d)
                .unroll(idx)
                .tile_init(rxi, ryi);
        }
        // The state is indexed the other way round, so the warps go on its
        // second dimension to own the same channels they own everywhere else.
        chunk_state.compute_at(out, t)
            .store_in(MemoryType::Tile)
            .tile(p, d, rxi, ryi, tile, tile)
            .unroll(p)
            .gpu_threads(d)
            .tile_init(rxi, ryi);

        y_intra.update()
            .tile(d, idx, rxi, ryi, tile, tile)
            .split(rj_var, rro, rri, tile)
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
        chunk_state.update()
            .tile(p, d, rxi, ryi, tile, tile)
            .split(rj_var, rro, rri, tile)
            .reorder(p, d, rro)
            .unroll(p)
            .gpu_threads(d)
            .tile_matmul(rri, rxi, ryi);

        H.compute_at(out, t)
            .store_at(out, b)
            .store_in(MemoryType::Tile)
            .tile(p, d, rxi, ryi, tile, tile)
            .unroll(p)
            .gpu_threads(d)
            .tile_init(rxi, ryi);

        // The narrowed state goes out to shared memory rather than staying in
        // fragments: an accumulator and a multiply's second operand are held
        // in different register layouts, so one fragment cannot be both. The
        // state is an accumulator, and this is the operand read from it.
        H16.compute_at(out, t)
            .store_in(MemoryType::GPUShared)
            .tile(p, d, rxi, ryi, tile, tile)
            .unroll(p)
            .gpu_threads(d)
            .tile_store(rxi, ryi);

        // The scan, and the two staged operands the multiplies read.
        cumdelta.compute_at(out, t).store_in(MemoryType::GPUShared);
        if (!inductive) {
            cumdelta.gpu_threads(k);
            cumdelta.update().gpu_threads(k);
        }
        for (Func f : {Xb, Xbd}) {
            f.compute_at(out, t)
                .store_in(MemoryType::GPUShared)
                .split(f.args()[0], xo, xi, tile)
                .gpu_lanes(xi);
        }
    }

    RVar rp_var, rj_var;

};

}  // namespace

HALIDE_REGISTER_GENERATOR(Mamba2, mamba2)
