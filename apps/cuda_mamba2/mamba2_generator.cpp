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
// whole sequence, holding H in tensor core registers between chunks.

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

    // Channels by sequence by head.
    Input<Buffer<float, 3>> X{"X"};
    // State by sequence by head.
    Input<Buffer<float, 3>> Bm{"Bm"};
    Input<Buffer<float, 3>> Cm{"Cm"};
    // The step size, one per timestep, and the decay parameter, one per head.
    Input<Buffer<float, 2>> Delta{"Delta"};
    Input<Buffer<float, 1>> A{"A"};

    // Channels by position in a chunk by chunk by head.
    Output<Buffer<float, 4>> out{"out"};

    void generate() {
        Var d("d"), p("p"), k("k"), idx("idx"), jj("jj"), t("t"), b("b");

        const int T = chunk;
        const int num_chunks = (int)seq / (int)chunk;

        // The input already scaled by the step size.
        Func Xb("Xb");
        Xb(d, t, b) = Delta(t, b) * X(d, t, b);

        // How much decay has accumulated from the start of a chunk to each
        // position in it, in the log domain, where the ratio every use wants
        // is a difference and a long chunk cannot underflow. Written as a
        // reduction, which costs a chunk's length per position; see the
        // inductive version for what that ought to be.
        RDom rm(0, T, "rm");
        Func cumdelta("cumdelta");
        cumdelta(k, t, b) += select(rm <= k, Delta(t * T + rm, b), 0.f);

        // The decay from just after position j to position i.
        auto decay = [&](Expr from, Expr to, Expr tt, Expr bb) {
            return exp(A(bb) * (cumdelta(to, tt, bb) - cumdelta(from, tt, bb)));
        };

        // The chunk's own scores: C^T B, masked to be causal and weighted by
        // how much the state decays between the two positions.
        RDom rp(0, state, "rp");
        rp_var = rp.x;
        Func qk("qk");
        qk(jj, idx, t, b) += Cm(rp, t * T + idx, b) * Bm(rp, t * T + jj, b);

        // Held at the precision the tensor cores want for the multiply that
        // consumes it, which also keeps it inside the shared memory a block
        // gets.
        Func score("score");
        score(jj, idx, t, b) =
            cast<float16_t>(select(jj <= idx, qk(jj, idx, t, b) * decay(jj, idx, t, b), 0.f));

        RDom rj(0, T, "rj");
        rj_var = rj.x;
        Func y_intra("y_intra");
        y_intra(d, idx, t, b) += cast<float>(score(rj, idx, t, b)) * Xb(d, t * T + rj, b);

        // What this chunk leaves behind at its last position.
        Func chunk_state("chunk_state");
        chunk_state(p, d, t, b) += Bm(p, t * T + rj, b) * Xb(d, t * T + rj, b) *
                                   decay(rj, T - 1, t, b);

        // Every earlier chunk's state, decayed to the end of this one. This is
        // the only thing that crosses a chunk boundary, and the only recurrence
        // left in the pipeline.
        Func H = Func(Float(32), "H");
        H(p, d, t, b) = select(t <= 0,
                               0.f,
                               likely(H(p, d, t - 1, b) *
                                      exp(A(b) * cumdelta(T - 1, t, b)))) +
                        chunk_state(p, d, t, b);

        // That state read back out at each position of this chunk, decayed
        // from the end of the previous chunk to here.
        Func y_inter("y_inter");
        y_inter(d, idx, t, b) += Cm(rp, t * T + idx, b) * H(rp, d, max(t - 1, 0), b);

        out(d, idx, t, b) =
            y_intra(d, idx, t, b) +
            select(t > 0,
                   y_inter(d, idx, t, b) * exp(A(b) * cumdelta(idx, t, b)),
                   0.f);

        // ---------------------------------------------------------------
        // Estimates and bounds
        // ---------------------------------------------------------------
        out.bound(d, 0, channels)
            .bound(idx, 0, T)
            .bound(t, 0, num_chunks)
            .bound(b, 0, heads);

        if (!using_autoscheduler()) {
            schedule_gpu(d, p, k, idx, jj, t, b, Xb, cumdelta, qk, score,
                         y_intra, chunk_state, H, y_inter);
        }
    }

private:
    // One block per head, walking that head's chunks in order and carrying
    // the state between them. Everything a chunk needs is computed inside the
    // walk, so the only thing that leaves the block is the output.
    void schedule_gpu(Var d, Var p, Var k, Var idx, Var jj, Var t, Var b,
                      Func Xb, Func cumdelta, Func qk, Func score,
                      Func y_intra, Func chunk_state, Func H, Func y_inter) {
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
        // One value per position in the chunk, each an independent reduction,
        // so the threads can share one copy.
        cumdelta.compute_at(out, t)
            .store_in(MemoryType::GPUShared)
            .gpu_threads(k);
        cumdelta.update().gpu_threads(k);
    }

    RVar rp_var, rj_var;

};

}  // namespace

HALIDE_REGISTER_GENERATOR(Mamba2, mamba2)
