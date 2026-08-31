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
        score(jj, idx, t, b) =
            select(jj <= idx,
                   qk(jj, idx, t, group_of(b)) * decay(jj, idx, t, b) *
                       Delta(t * chunk + jj, b),
                   0.f);
        // Narrowed for the multiply that consumes it. Where the scores stay on
        // fragments this inlines into the multiply, whose operand conversion
        // is this same rounding; where they go through shared memory it is the
        // copy that narrows them on the way in.
        Func score_h("score_h");
        score_h(jj, idx, t, b) = cast<float16_t>(score(jj, idx, t, b));

        RDom rj(0, chunk, "rj");
        rj_var = rj.x;
        RDom rjy(0, chunk, "rjy");
        rjy.where(rjy / 16 <= idx / 16);
        rjy_var = rjy.x;
        Func y_intra("y_intra");
        y_intra(d, idx, t, b) = 0.f;
        y_intra(d, idx, t, b) += cast<float>(score_h(rjy, idx, t, b)) *
                                 cast<float>(X(d, t * chunk + rjy, b));

        // The state's operand, scaled by the step size and decayed to the end
        // of its chunk, so that what the chunk leaves behind is a plain
        // product of two operands.
        Func Bmdf("Bmdf");
        Bmdf(jj, p, t, b) = cast<float>(Bm(p, t * chunk + jj, group_of(b))) *
                            Delta(t * chunk + jj, b) *
                            decay(jj, chunk - 1, t, b);
        // Narrowed for the multiply, like the scores.
        Func Bmd("Bmd");
        Bmd(p, jj, t, b) = cast<float16_t>(Bmdf(jj, p, t, b));

        Func chunk_state("chunk_state");
        chunk_state(d, p, t, b) = 0.f;
        chunk_state(d, p, t, b) += cast<float>(Bmd(p, rj, t, b)) *
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
            H(d, p, t, b) = select(t <= 0,
                                   0.f,
                                   likely(H(d, p, t - 1, b) *
                                          exp(A(b) * cumdelta(chunk - 1, t - 1, b))) +
                                       chunk_state(d, p, t - 1, b));
        } else if (undef_init()) {
            // The same recurrence as an update definition. It owns the walk, so
            // it cannot be slid into the loop that consumes it: the whole array
            // has to exist, at the precision it is carried at, before anything
            // reads it.
            H(d, p, t, b) = undef<float>();
            H(d, p, rt, b) = select(rt <= 0,
                                    0.f,
                                    likely(H(d, p, rt - 1, b) *
                                           exp(A(b) * cumdelta(chunk - 1, rt - 1, b))) +
                                        chunk_state(d, p, rt - 1, b));
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
            H(d, p, t, b) = chunk_state(d, p, t - 1, b);
            H(d, p, rt + 1, b) += H(d, p, rt, b) *
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
        Hop(d, p, t, b) = cast<float16_t>(H(d, p, t, b));

        Func y_inter("y_inter");
        y_inter(d, idx, t, b) = 0.f;
        y_inter(d, idx, t, b) += cast<float>(Cm(rp, t * chunk + idx, group_of(b))) *
                                 cast<float>(Hop(d, rp, t, b));

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
                    schedule_triton(d, p, k, idx, jj, t, b, Bmdf, cumdelta, qk,
                                    score, score_h, y_intra, chunk_state, H, Hop,
                                    y_inter, y, g);
                } else {
                    schedule_gpu(d, p, k, idx, jj, t, b, cumdelta, qk, score_h,
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
                         Func Bmdf, Func cumdelta, Func qk, Func score,
                         Func score_h, Func y_intra, Func chunk_state, Func H,
                         Func Hop, Func y_inter, Func y, Var g) {
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
        // Fewer than four leaves a block with too little to do to cover the
        // latency of what it reads, so at the longer chunks it is the staging
        // that gives way rather than the block.
        const int pos_tiles = 4;
        // How many of the block's position tiles one warp owns. Owning two
        // means the state tiles the inter-chunk multiply reads serve two
        // multiplies per fetch; more than two costs more registers than it
        // saves in fetches.
        const int idx_per_warp = 2;
        // Computing the scores in the kernel that reads them only costs no
        // extra multiplies if that kernel is the only one that wants them,
        // which needs every head to have its own and a block to cover the whole
        // chunk. Otherwise they go in a kernel of their own, once per group,
        // and come back through fragments a tile at a time, costing no shared
        // memory here at all.
        const bool fuse_qk =
            (int)groups == (int)heads && (int)chunk <= pos_tiles * tile;
        const int score_bytes = (int)chunk * pos_tiles * tile * 2;
        const int staged_bytes = (int)chunk * (int)channels * 2;
        // With the scores through shared memory, staging what the multiply
        // reads is worth it where both fit. With them on fragments the block
        // is small and the panel's reuse is caught by the cache, so occupancy
        // is worth more than the staging.
        const bool stage_operand = fuse_qk && score_bytes + staged_bytes <= 40 * 1024;

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
        // Each thread's walk reads its own row of the step sizes, a whole
        // block's worth of rows apart, so the walks' reads are one sector per
        // thread per step. Staged for the block, the fill is coalesced and the
        // walks read shared memory instead.
        if (inductive() && (int)seq * 4 <= 40 * 1024) {
            Func ds = Func(Delta).in(cumdelta);
            Var fo("fo"), fi("fi");
            ds.compute_at(cumdelta, b)
                .store_in(MemoryType::GPUShared)
                .split(ds.args()[0], fo, fi, (int)seq / (int)chunk)
                .reorder(fi, fo)
                .gpu_threads(fi);
        }

        // What each chunk leaves behind, which is a plain product once the
        // input has been decayed to the end of the chunk. In the form whose
        // pure definition stores it, that definition is this kernel; otherwise
        // it needs a wrapper to copy the tile out to memory.
        const bool state_init_is_chunk_state = !inductive() && !undef_init();
        Func cs = state_init_is_chunk_state ? H : chunk_state.in();
        Var hto("hto"), hti("hti");
        if (inductive()) {
            // What each chunk leaves behind is computed inside the walk that
            // consumes it, a chunk per step, and crosses to the carried state
            // through a tile of shared memory rather than through global.
            // Locations are set here; the walk's own schedule is below.
            chunk_state.compute_at(Hop, hti).unroll(t);
            chunk_state.update().unroll(t);
            cs.compute_at(Hop, hti)
                .store_in(MemoryType::GPUShared)
                .unroll(cs.args()[2])
                .tile(d, p, rxi, ryi, tile, tile)
                .unroll(d)
                .gpu_threads(p)
                .tile_store(rxi, ryi);
        } else {
            cs.compute_root()
                .tile(d, p, rxi, ryi, tile, tile)
                .reorder(rxi, ryi, d, p, t, b)
                .unroll(d)
                .gpu_blocks(t, b)
                .gpu_threads(p)
                .tile_store(rxi, ryi);
            chunk_state.compute_at(cs, t);
        }
        chunk_state
            .store_in(MemoryType::Tile)
            .tile(d, p, rxi, ryi, tile, tile)
            .unroll(d)
            .gpu_threads(p)
            .tile_init(rxi, ryi);
        // The reduction is walked a few tiles at a time, so that the slice of
        // the input those tiles read can be staged for the block while the
        // whole chunk's worth would not be worth the occupancy. The slice loop
        // is split from the top so the tiles stay whole, and sits outside the
        // loop over warps so the block fills the stage together.
        chunk_state.update()
            .tile(d, p, rxi, ryi, tile, tile)
            .split(rj_var, RVar("rjo"), RVar("rji"),
                   (getenv("SLICE_TILES") ? atoi(getenv("SLICE_TILES")) : 4) * tile)
            .split(RVar("rji"), rro, rri, tile)
            .reorder(d, rro, p, RVar("rjo"))
            .unroll(d)
            .gpu_threads(p)
            .tile_matmul(rri, rxi, ryi);
        if (state_init_is_chunk_state) {
            // The chunk this is computing is one back from the chunk the state
            // is being initialized for. Left as a variable that offset stops
            // two accesses to the same tile being recognised as the same tile.
            chunk_state.unroll(t);
            chunk_state.update().unroll(t);
        }
        // The decayed operand comes in a tile at a time, decayed on the
        // fragment the load leaves it in: a warp owns a block of the state's
        // rows, so the tiles it multiplies by are its own. The input is read
        // through the cache: staging it in shared memory costs more occupancy
        // than the reuse gives back.
        {
            Func Bml = Func(Bm).in(Bmdf);
            Bml.compute_at(chunk_state, rro)
                .store_in(MemoryType::Tile)
                .reorder_storage(Bml.args()[1], Bml.args()[0], Bml.args()[2])
                .tile(Bml.args()[0], Bml.args()[1], rxi, ryi, tile, tile)
                .tile_load(rxi, ryi);
            Bmdf.compute_at(chunk_state, rro)
                .store_in(MemoryType::Tile)
                .tile(jj, p, rxi, ryi, tile, tile)
                .tile_init(rxi, ryi);
            Func xs = Func(X).in(chunk_state);
            Var so("so"), si("si"), fu("fu"), fo("fo"), fi("fi"), w("w"), l("l");
            xs.compute_at(chunk_state, RVar("rjo"))
                .store_in(MemoryType::GPUSharedAsync)
                // A row of the panel is a power of two wide, so without a
                // skew the rows land on the same banks and the operand loads
                // conflict almost every time.
                .align_storage(xs.args()[0], (int)channels + 8);
            if (const char *pipe = getenv("PIPE")) {
                xs.store_at(Hop, hti)
                    .slide(chunk_state, RVar("rjo"), atoi(pipe));
                if (const char *fold = getenv("FOLD")) {
                    xs.fold_storage(xs.args()[1], atoi(fold));
                }
                // The sliver the pipeline fills each step is one slice, so
                // nothing in the fill's loop structure may straddle a slice
                // boundary: sliding can only clip whole iterations of the
                // loops it finds, and a fused loop whose rounds cross the
                // boundary clips outward to cover two slices. Keep a loop
                // over slices outermost - one iteration per step - and deal
                // a single slice's runs to the threads inside it.
                const int slice_rows =
                    (getenv("SLICE_TILES") ? atoi(getenv("SLICE_TILES")) : 4) * tile;
                Var sl("sl"), sw("sw");
                xs.split(xs.args()[1], sl, sw, slice_rows, TailStrategy::GuardWithIf)
                    .split(xs.args()[0], so, si, 8)
                    .fuse(so, sw, fu)
                    .split(fu, fo, fi, 32 * ((int)state / tile))
                    .split(fi, w, l, 32)
                    .reorder(si, l, w, fo, sl)
                    .vectorize(si)
                    .gpu_lanes(l)
                    .gpu_threads(w);
            } else {
                xs.split(xs.args()[0], so, si, 8)
                    .fuse(so, xs.args()[1], fu)
                    .split(fu, fo, fi, 32 * ((int)state / tile))
                    .split(fi, w, l, 32)
                    .reorder(si, l, w, fo)
                    .vectorize(si)
                    .gpu_lanes(l)
                    .gpu_threads(w);
            }
        }

        // The recurrence over chunks. The chunks have to be walked in order,
        // so the parallelism comes from the state and the channels instead,
        // one serial walk per entry of the state. Only the narrowed state
        // reaches memory; the carry is the single precision Func slid over the
        // walk, which leaves it a few registers per thread. Its store level is
        // outside the thread loops, so it needs saying that it is a thread's
        // own and not something to put in shared.
        Var pt("pt"), pe("pe"), hw("hw"), hp("hp"), hpo("hpo"), hpi("hpi"), hl("hl");
        if (inductive()) {
            // A block owns one channel tile of one head and walks its chunks.
            // Each step computes the chunk's state on the tensor cores - one
            // state-row tile per warp - lands it in shared memory, and folds
            // it into the carried state, which lives in registers for the
            // whole walk. Only the narrowed state ever reaches global memory.
            Hop.compute_root()
                .split(t, hto, hti, 1)
                .split(d, ddo, ddi, 2 * tile)
                .split(p, hw, hp, tile)
                .reorder(ddi, hp, hw, hti, hto, ddo, b)
                .gpu_blocks(ddo, b)
                .gpu_lanes(ddi)
                .gpu_threads(hw);
            H.compute_at(Hop, hti)
                .store_at(Hop, ddo)
                .slide(Hop, t)
                .store_in(MemoryType::Stack)
                .split(p, hw, hp, tile)
                .reorder(d, hp, hw)
                .gpu_lanes(d)
                .gpu_threads(hw);
        } else {
            // Written as an update definition the walk is the Func, so there is
            // nothing to slide it into and nothing to narrow it on the way out.
            // The whole array is computed first, at the precision it is carried
            // at, and the multiply that reads it reads that.
            H.compute_root();
            Var hpt("hpt");
            H.update()
                .split(d, pt, pe, 4)
                .split(p, ddo, hpt, 8)
                .reorder(pe, pt, hpt, rt_var, ddo, b)
                .gpu_blocks(ddo, b)
                .gpu_threads(pt, hpt)
                .vectorize(pe);
            // The multiply still wants a half precision operand, so the state
            // is narrowed on the way in from memory rather than on the way out
            // to it. Memory holds the wide form either way round.
            Var so("so"), si("si"), to_("to"), ti_("ti");
            Hop.compute_at(out, io)
                .store_in(MemoryType::GPUShared)
                .align_storage(d, (int)channels + 8)
                .split(d, so, si, 32)
                .split(p, to_, ti_,
                       (int)state / (fuse_qk ? (int)channels / tile
                                             : pos_tiles / idx_per_warp))
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
        // Which dimension the warps own switches with how the scores arrive.
        // On fragments, a warp must own a tile of positions, so the scores it
        // computes are its own. Through shared memory the scores are the
        // block's, and owning a tile of channels instead keeps the state each
        // warp reads its own rather than every warp reading all of it.
        // Left to itself the backend keeps every accumulator's worth of
        // operand loads in flight and lands past two hundred registers, which
        // caps the kernel at four blocks per processor. Spilling a little to
        // fit six is worth more than the spills cost; below this the spills
        // win. Measured on an RTX 5060 Ti at the library-default shape - at
        // the smaller shapes, and with the state read back through shared
        // memory, the same cap only adds spills.
        if (!fuse_qk && inductive() && (int)state >= 128 && (int)chunk >= 256) {
            out.gpu_max_registers(168);
        }
        out
            .compute_root()
            .tile(d, idx, rxi, ryi, tile, tile)
            .split(idx, io, ii, pos_tiles)
            .reorder(rxi, ryi, d, ii, io, t, b)
            .gpu_blocks(io, t, b)
            .tile_store(rxi, ryi);
        Var iw("iw"), ii2("ii2");
        if (fuse_qk) {
            out.unroll(ii).gpu_threads(d);
        } else {
            out.split(ii, iw, ii2, idx_per_warp)
                .unroll(d)
                .unroll(ii2)
                .gpu_threads(iw);
        }
        for (Func f : {y_intra, y_inter, y}) {
            f.compute_at(out, io)
                .store_in(MemoryType::Tile)
                .tile(d, idx, rxi, ryi, tile, tile)
                .tile_init(rxi, ryi);
            if (fuse_qk) {
                f.gpu_threads(d).unroll(idx);
            } else {
                f.split(idx, iw, ii2, idx_per_warp)
                    .unroll(d)
                    .unroll(ii2)
                    .gpu_threads(iw);
            }
        }
        for (Func f : {y_intra, y_inter}) {
            f.update().tile(d, idx, rxi, ryi, tile, tile);
        }
        y_intra.update().split(rjy_var, rro, rri, tile);
        y_inter.update().split(rp_var, rro, rri, tile);
        for (Func f : {y_intra, y_inter}) {
            if (fuse_qk) {
                f.update().reorder(d, idx, rro).gpu_threads(d).unroll(idx);
            } else {
                f.update().split(idx, iw, ii2, idx_per_warp);
                if (f.name() == "y_intra") {
                    // Its walk is pruned by the causal mask per position tile,
                    // so each owned tile walks separately.
                    f.update().reorder(d, rro, ii2, iw);
                } else {
                    // A uniform walk: both owned tiles sit inside the step, so
                    // the state tiles it multiplies by are fetched once each.
                    f.update().reorder(ii2, d, rro, iw);
                }
                f.update().unroll(d).unroll(ii2).gpu_threads(iw);
            }
            f.update().tile_matmul(rri, rxi, ryi);
        }
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
            score_h.unroll(t);
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
            Var jo("jo"), ji("ji");
            qk_at = qk.in();
            qk_at.compute_root()
                .tile(jj, idx, rxi, ryi, tile, tile)
                .split(jj, jo, ji, 4)
                .reorder(rxi, ryi, ji, idx, jo, t, g)
                .unroll(ji)
                .gpu_blocks(jo, t, g)
                .gpu_threads(idx)
                .tile_store(rxi, ryi);
            qk.compute_at(qk_at, jo).store_in(MemoryType::Tile).unroll(t);
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
            // in, with the unscheduled full precision form inlined into the
            // narrowing, so the fragments hold the narrowed scores and only
            // those reach shared memory, where the multiply that consumes
            // them reads it as an operand.
            score_h.compute_at(out, io)
                .store_in(MemoryType::Tile)
                .tile(jj, idx, rxi, ryi, tile, tile)
                .unroll(jj)
                .gpu_threads(idx)
                .tile_init(rxi, ryi);
            score_h.in()
                .compute_at(out, io)
                .store_in(MemoryType::GPUShared)
                .tile(jj, idx, rxi, ryi, tile, tile)
                .unroll(jj)
                .gpu_threads(idx)
                .tile_store(rxi, ryi);
        } else {
            // The scores come back from memory one tile at a time, straight
            // into fragments: a warp owns a tile of output positions, so the
            // scores it needs are its own, and masking and decaying them is
            // elementwise work on the tile the load left them in. They never
            // touch shared memory, which leaves all of it for staging the
            // operand the multiply reads.
            Func qkl = qk_at.in(score);
            qkl.compute_at(y_intra, rro)
                .store_in(MemoryType::Tile)
                .bound_extent(jj, tile)
                .bound_storage(jj, tile)
                .tile(jj, idx, rxi, ryi, tile, tile)
                .tile_load(rxi, ryi);
            score.compute_at(y_intra, rro)
                .store_in(MemoryType::Tile)
                .bound_extent(jj, tile)
                .bound_storage(jj, tile)
                .tile(jj, idx, rxi, ryi, tile, tile)
                .tile_init(rxi, ryi);
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
