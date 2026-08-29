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
// Writing them as update definitions over an RDom instead costs the same
// arithmetic but forces each one to be computed in one piece ahead of what
// reads it - see the inductive generator param.
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

#include "Halide.h"

#include <algorithm>
#include <vector>

namespace {

using namespace Halide;

class Mamba2 : public Halide::Generator<Mamba2> {
public:
    GeneratorParam<int> seq{"seq", 4096};          // sequence length
    GeneratorParam<int> state{"state", 64};        // P, the state dimension
    GeneratorParam<int> channels{"channels", 64};  // D, channels in a head
    GeneratorParam<int> chunk{"chunk", 64};        // how long a chunk is
    GeneratorParam<int> heads{"heads", 128};       // batch * heads
    // How many heads share a B and a C. The reference shares one set across
    // every head of a batch entry; giving each head its own is the other
    // extreme, and changes which multiplies are worth sharing a kernel.
    GeneratorParam<int> groups{"groups", 1};
    // How the two scans - the decay across a chunk and the state carried
    // between chunks - are written. All three forms do one step per element.
    //
    //  inductive:  a Func that refers to itself one step back, which can be
    //              slid into the loop that consumes it.
    //  rdom_undef: an update definition over an RDom that covers the base case
    //              itself, so the pure definition can be undef and stores
    //              nothing. Fewest kernels of the two RDom forms, but it takes
    //              care to write: nothing may read the values it leaves unset.
    //  rdom:       the same update definition with a pure definition that
    //              really does initialize, which is the straightforward thing
    //              to write and costs a kernel per scan to run it.
    //
    // An update definition owns the dimension it walks either way, so it has
    // to be computed in one piece ahead of its consumers: the state then
    // crosses a kernel boundary as the wide type it is carried at, rather than
    // being slid into the walk and narrowed on the way out. Only the tensor
    // core schedule can be built all three ways; the fused one needs the state
    // slid.
    enum class ScanForm { Inductive,
                          RDomUndef,
                          RDom };
    GeneratorParam<ScanForm> scan{"scan",
                                  ScanForm::Inductive,
                                  {{"inductive", ScanForm::Inductive},
                                   {"rdom_undef", ScanForm::RDomUndef},
                                   {"rdom", ScanForm::RDom}}};
    // Whether the four multiplies go to the tensor cores.
    GeneratorParam<bool> wmma{"wmma", false};
    // Whether to take the reference implementation's shape instead: a kernel
    // per stage, each of them parallel over chunks as well as heads, with the
    // state's recurrence in a kernel of its own and everything that crosses a
    // stage going through memory. Fused, a head's chunks are walked in order
    // by one block, which is where the parallelism goes.

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
        Var g("g");

        // Which group's B and C a head reads.
        auto group_of = [&](Expr head) {
            return (int)groups == (int)heads ? head : head / ((int)heads / (int)groups);
        };

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
        // The undef form's walk covers the first position too, so it starts one
        // earlier than the form whose pure definition supplies it.
        RDom rm(undef_init() ? 0 : 1, (int)chunk - (undef_init() ? 0 : 1), "rm");
        rm_var = rm.x;
        if (inductive()) {
            cumdelta(k, t, b) = Delta(t * chunk + k, b) +
                                select(k <= 0, 0.f, likely(cumdelta(k - 1, t, b)));
        } else if (undef_init()) {
            cumdelta(k, t, b) = undef<float>();
            cumdelta(rm, t, b) = Delta(t * chunk + rm, b) +
                                 select(rm <= 0, 0.f, likely(cumdelta(rm - 1, t, b)));
        } else {
            cumdelta(k, t, b) = Delta(t * chunk + k, b);
            cumdelta(rm, t, b) = cumdelta(rm - 1, t, b) + Delta(t * chunk + rm, b);
        }

        // The decay from just after position j to position i. Left to inline,
        // so every use of it is the expression above written out in place.
        Var from("from"), to("to");
        Func decay("decay");
        decay(from, to, t, b) =
            exp(A(b) * (cumdelta(to, t, b) - cumdelta(from, t, b)));

        // The chunk's own scores: C^T B.
        RDom rp(0, state, "rp");
        rp_var = rp.x;
        // One per group rather than one per head, which is what makes it a
        // kernel of its own: every head of a group reads the same matrix.
        Func qk("qk");
        qk(jj, idx, t, g) = 0.f;
        qk(jj, idx, t, g) += cast<float>(Cm(rp, t * chunk + idx, g)) *
                             cast<float>(Bm(rp, t * chunk + jj, g));

        // Masking is an elementwise multiply by a tile of the mask, rather
        // than a test on the coordinates of each entry, which the lanes of a
        // warp hold in an order the schedule does not name.
        // Masked to be causal and weighted by the decay between the two
        // positions, then narrowed for the multiply that consumes it.
        Func score("score");
        score(jj, idx, t, b) = cast<float16_t>(
            select(jj <= idx,
                   qk(jj, idx, t, group_of(b)) * decay(jj, idx, t, b) *
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
        Bmd(p, jj, t, b) = cast<float16_t>(cast<float>(Bm(p, t * chunk + jj, group_of(b))) *
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
        const int num_chunks = (int)seq / (int)chunk;
        Func H = Func(Float(32), "H");
        RDom rt(undef_init() ? 0 : 1,
                undef_init() ? num_chunks : num_chunks - 2, "rt");
        rt_var = rt.x;
        if (inductive()) {
            H(p, d, t, b) = select(t <= 0,
                                   0.f,
                                   likely(H(p, d, t - 1, b) *
                                          exp(A(b) * cumdelta(chunk - 1, t - 1, b))) +
                                       chunk_state(p, d, t - 1, b));
        } else if (undef_init()) {
            // The same recurrence as an update definition. It owns the walk, so
            // it cannot be slid into the loop that consumes it: the whole array
            // has to exist, at the precision it is carried at, before anything
            // reads it.
            H(p, d, t, b) = undef<float>();
            H(p, d, rt, b) = select(rt <= 0,
                                    0.f,
                                    likely(H(p, d, rt - 1, b) *
                                           exp(A(b) * cumdelta(chunk - 1, rt - 1, b))) +
                                        chunk_state(p, d, rt - 1, b));
        } else {
            // The pure definition really does initialize, which is the
            // straightforward thing to write and costs a kernel to run it.
            // What it initializes to is the term of the recurrence that does
            // not depend on the walk - what the previous chunk left behind -
            // so the kernel running it is the one the chunk state would
            // otherwise need for itself, and what it stores is a plain copy of
            // a tile that goes out as a matrix store.
            //
            // The first chunk carries nothing in, so nothing reads the state
            // there and the walk starts a step later. Every index either
            // definition touches is then inside the domain, with no masking
            // and no special case anywhere.
            H(p, d, t, b) = chunk_state(p, d, t - 1, b);
            H(p, d, rt + 1, b) += H(p, d, rt, b) *
                                  exp(A(b) * cumdelta(chunk - 1, rt, b));
        }

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
        y_inter(d, idx, t, b) += cast<float>(Cm(rp, t * chunk + idx, group_of(b))) *
                                 cast<float>(Hop(rp, d, t, b));

        // The two halves of the answer, summed and scaled. Kept apart from the
        // output so that a schedule can put the sum on a tile and leave the
        // output as the copy that takes it to global memory. Inlines into the
        // output when nothing says otherwise.
        // The first chunk has no earlier state to carry in, so its answer is
        // just its own half. Saying it here is what keeps every read of the
        // state inside the domain: nothing ever asks for the state at the first
        // chunk, so nothing asks for what the chunk before it left behind.
        Func y("y");
        y(d, idx, t, b) = cast<float16_t>(
            y_intra(d, idx, t, b) +
            select(t <= 0,
                   0.f,
                   likely(y_inter(d, idx, t, b) *
                          exp(A(b) * cumdelta(idx, t, b)))));

        out(d, idx, t, b) = y(d, idx, t, b);

        try {
            if (!using_autoscheduler()) {
                if (wmma) {
                    schedule_triton(d, p, k, idx, jj, t, b, Bmd, cumdelta, qk,
                                    score, y_intra, chunk_state, H, Hop, y_inter,
                                    y, g);
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

        _halide_user_assert(inductive())
            << "This schedule fuses the recurrence over chunks into the loop "
            << "that consumes it, which is only expressible if the state is an "
            << "inductive Func: an update definition owns the whole walk, so "
            << "there is no per-chunk loop to fuse it into. Use wmma=true to "
            << "compare the two ways of writing it.\n";


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
                         Func y_inter, Func y, Var g) {
        const int tile = 16;
        Var xo("xo"), xi("xi"), rxi("rxi"), ryi("ryi"), n_var("n");
        Var po("po"), pi("pi"), ddo("ddo"), ddi("ddi");
        Var io("io"), ii("ii");
        RVar rro("rro"), rri("rri");
        // How many tiles of output positions a block covers, and what that
        // costs in shared memory: the scores are a whole chunk by that many
        // tiles. A block gets 48KB, and staging the operand the intra-chunk
        // multiply reads costs a chunk by the channels on top, so at the longer
        // chunks it is the staging that gives way rather than the block.
        const int pos_tiles = 4;
        const int score_bytes = (int)chunk * pos_tiles * tile * 2;
        const int staged_bytes = (int)chunk * (int)channels * 2;
        const bool stage_operand = score_bytes + staged_bytes <= 40 * 1024;
        // Computing the scores in the kernel that reads them only costs no
        // extra multiplies if that kernel is the only one that wants them,
        // which needs every head to have its own and a block to cover the whole
        // chunk. Otherwise they go in a kernel of their own, once per group.
        const bool fuse_qk =
            (int)groups == (int)heads && (int)chunk <= pos_tiles * tile;

        // The scan within a chunk, one thread per chunk. Its rows are padded to
        // an even length, so that a tile of the decay can be read as a matrix
        // with a leading dimension of zero rather than selected out of a
        // broadcast vector lane by lane, which wants an eight byte boundary.
        cumdelta.compute_root().align_bounds(k, 2).align_storage(k, 2);
        // The pure stage stores nothing in the undef form, so it has no loops
        // to schedule and no kernel is launched for it.
        if (inductive() || !undef_init()) {
            cumdelta.reorder(k, t, b).gpu_blocks(b).gpu_threads(t);
        }
        if (!inductive()) {
            cumdelta.update().reorder(rm_var, t, b).gpu_blocks(b).gpu_threads(t);
        }

        // What each chunk leaves behind, which is a plain product once the
        // input has been decayed to the end of the chunk. In the form whose
        // pure definition stores it, that definition is this kernel; otherwise
        // it needs a wrapper to copy the tile out to memory.
        const bool state_init_is_chunk_state = !inductive() && !undef_init();
        Func cs = state_init_is_chunk_state ? H : chunk_state.in();
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
        if (state_init_is_chunk_state) {
            // The chunk this is computing is one back from the chunk the state
            // is being initialized for. Left as a variable that offset stops
            // two accesses to the same tile being recognised as the same tile.
            chunk_state.unroll(t);
            chunk_state.update().unroll(t);
        }
        // The decayed operand is a chunk's worth of the state's B, which at the
        // longer chunks is more shared memory than a block has. Where it fits,
        // staging it is worth it; where it does not, the multiply reads it as
        // it is computed.
        {
            Var so("so"), si("si"), to_("to"), ti_("ti");
            const bool whole_chunk = (int)state * (int)chunk * 2 <= 40 * 1024;
            if (whole_chunk) {
                Bmd.compute_at(cs, t);
            } else {
                // Only the slice the multiply is reducing over right now.
                Bmd.compute_at(chunk_state, rro);
            }
            Bmd.store_in(MemoryType::GPUShared)
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
        if (inductive()) {
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
        } else {
            // Written as an update definition the walk is the Func, so there is
            // nothing to slide it into and nothing to narrow it on the way out.
            // The whole array is computed first, at the precision it is carried
            // at, and the multiply that reads it reads that.
            H.compute_root();
            H.update()
                .split(p, pt, pe, 4)
                .split(d, ddo, ddi, 8)
                .reorder(pe, pt, ddi, rt_var, ddo, b)
                .gpu_blocks(ddo, b)
                .gpu_threads(pt, ddi)
                .vectorize(pe);
            // The multiply still wants a half precision operand, so the state
            // is narrowed on the way in from memory rather than on the way out
            // to it. Memory holds the wide form either way round.
            Var so("so"), si("si"), to_("to"), ti_("ti");
            Hop.compute_at(out, io)
                .store_in(MemoryType::GPUShared)
                .split(p, so, si, 32)
                .split(d, to_, ti_, tile)
                .reorder(si, so, ti_, to_)
                .gpu_lanes(si)
                .gpu_threads(to_);
        }

        // The output. The scores are masked and decayed here, on the way in
        // from memory, and the state is narrowed here too. The sum of the two
        // halves happens on the tile the multiplies left them in, and the
        // output is the copy that takes it from there to global memory.
        // A block is a tile of output positions of one chunk of one head. At
        // the chunk sizes the reference uses a whole chunk's worth of them at
        // once is more tiles than a thread has registers for, and more scores
        // than a block has shared memory for.
        out
            .compute_root()
            .tile(d, idx, rxi, ryi, tile, tile)
            .split(idx, io, ii, pos_tiles)
            .reorder(rxi, ryi, d, ii, io, t, b)
            .unroll(ii)
            .gpu_blocks(io, t, b)
            .gpu_threads(d)
            .tile_store(rxi, ryi);
        for (Func f : {y_intra, y_inter, y}) {
            f.compute_at(out, io)
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
        for (Func f : (stage_operand ? std::vector<Func>{Func(X).in(y_intra)}
                                     : std::vector<Func>{})) {
            Var so("so"), si("si"), fu("fu"), fo("fo"), fi("fi"), w("w"), l("l");
            f.compute_at(out, io)
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

        // Each per-chunk Func has a loop over the chunk it is computing, which
        // runs once. Left alone it stays a variable, and that is enough to stop
        // two accesses from different stages of the same Func being recognised
        // as the same tile.
        for (Func f : {y_intra, y_inter, y}) {
            f.unroll(t);
        }
        if (fuse_qk) {
            score.unroll(t);
        }
        for (Func f : {y_intra, y_inter}) {
            f.update().unroll(t);
        }
        if (fuse_qk) {
            qk.unroll(t);
            qk.update().unroll(t);
        }

        // The scores are a chunk by chunk matrix that the whole block reads,
        // and the block is one chunk of one head, so computing them here costs
        // no more multiplies than a kernel of their own would. Masking and
        // decaying them happens on the tile the multiply left them in, and
        // only the narrowed result reaches shared memory, where the multiply
        // that consumes them reads it as an operand.
        Func qk_at = qk;
        if (fuse_qk) {
            qk.compute_at(out, io).store_in(MemoryType::Tile);
        } else {
            qk_at = qk.in();
            qk_at.compute_root()
                .tile(jj, idx, rxi, ryi, tile, tile)
                .reorder(rxi, ryi, jj, idx, t, g)
                .unroll(jj)
                .gpu_blocks(t, g)
                .gpu_threads(idx)
                .tile_store(rxi, ryi);
            qk.compute_at(qk_at, t).store_in(MemoryType::Tile).unroll(t);
            qk.update().unroll(t);
        }
        qk.tile(jj, idx, rxi, ryi, tile, tile)
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
        if (fuse_qk) {
            // Masking and decaying happen on the tile the multiply left them
            // in, and only the narrowed result reaches shared memory, where the
            // multiply that consumes them reads it as an operand.
            score.compute_at(out, io)
                .store_in(MemoryType::Tile)
                .tile(jj, idx, rxi, ryi, tile, tile)
                .unroll(jj)
                .gpu_threads(idx)
                .tile_init(rxi, ryi);
            score.in()
                .compute_at(out, io)
                .store_in(MemoryType::GPUShared)
                .tile(jj, idx, rxi, ryi, tile, tile)
                .unroll(jj)
                .gpu_threads(idx)
                .tile_store(rxi, ryi);
        } else {
            // The scores come back from memory rather than out of a fragment,
            // so masking and decaying them is ordinary elementwise work on the
            // way into shared memory, which is where the multiply reads them.
            Var so("so"), si("si"), to_("to"), ti_("ti");
            score.compute_at(out, io)
                .store_in(MemoryType::GPUShared)
                .split(jj, so, si, 32)
                .split(idx, to_, ti_, tile)
                .reorder(si, so, ti_, to_)
                .gpu_lanes(si)
                .gpu_threads(to_);
        }

        const int num_chunks = (int)seq / (int)chunk;
        out.bound(d, 0, channels)
            .bound(idx, 0, chunk)
            .bound(t, 0, num_chunks)
            .bound(b, 0, heads);
    }

    // Every multiply is a tile of this size on the tensor cores.
    static constexpr int tile = 16;

    bool inductive() const {
        return scan == ScanForm::Inductive;
    }
    // Whether the pure definition of a scan stores nothing, leaving the update
    // to supply the base case.
    bool undef_init() const {
        return scan == ScanForm::RDomUndef;
    }

    RVar rp_var, rj_var, rjy_var, rm_var, rt_var;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Mamba2, mamba2)
