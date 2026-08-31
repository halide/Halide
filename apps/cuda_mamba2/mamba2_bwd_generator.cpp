// The backward pass of mamba2's SSD recurrence.
//
// The forward walks
//
//   h_n = a_n h_{n-1} + (delta_n B_n) x_n^T      y_n = C_n^T h_n
//
// so the gradient of the state walks the sequence the other way:
//
//   dh_n = C_n dy_n^T + a_{n+1} dh_{n+1}
//
// and every quantity the forward chunked into matrix multiplies has a
// mirror here. Chunked, the pass needs: the forward's own state carried
// between chunks (recomputed, not saved), the gradient state carried
// between chunks in the opposite direction, and per chunk the multiplies
//
//   xy       = X^T dY          the mirror of qk, but one per head
//   dG       = Cd dY^T         what a chunk contributes to the gradient state
//   dX       = score^T dY + Bmd dHnext
//   dC       = scoreT B + (decay) Hop dY, summed over the heads of a group
//   dB       = scoreT^T C + (decay) X dHnext, summed likewise
//
// where scoreT is the forward's score with xy in place of qk. The step
// size and decay gradients collapse through the adjoint identity
//
//   ddAcs(k) = sum_d dY(d,k) y(d,k) - sum_d X(d,k) dX(d,k)
//
// (every decay is exp of a difference of cumulative sums, the state's
// derivative with respect to its own log-decay is itself, and the two
// dot products are what remains after the telescoping) so ddt is that
// plus a suffix sum of it, and dA a weighted total - two more scans,
// also walked backwards.
//
// Every reverse walk is written as an ordinary inductive Func over a
// flipped index, tt = last - t, so the loops still count upwards.

#include "Halide.h"

#include <algorithm>
#include <vector>

namespace {

using namespace Halide;

class Mamba2Bwd : public Halide::Generator<Mamba2Bwd> {
public:
    GeneratorParam<int> seq{"seq", 4096};
    GeneratorParam<int> state{"state", 64};
    GeneratorParam<int> channels{"channels", 64};
    GeneratorParam<int> chunk{"chunk", 64};
    GeneratorParam<int> heads{"heads", 128};
    GeneratorParam<int> groups{"groups", 1};
    // Whether the multiplies go to the tensor cores. Off, everything gets a
    // plain GPU schedule, which is slow but exercises the same algorithm.
    GeneratorParam<bool> wmma{"wmma", false};

    Input<Buffer<float16_t, 3>> X{"X"};
    Input<Buffer<float16_t, 3>> Bm{"Bm"};
    Input<Buffer<float16_t, 3>> Cm{"Cm"};
    Input<Buffer<float, 2>> Delta{"Delta"};
    Input<Buffer<float, 1>> A{"A"};
    // The saved output of the forward pass and the gradient arriving at it,
    // both in the forward output's chunked layout.
    Input<Buffer<float16_t, 4>> Y{"Y"};
    Input<Buffer<float16_t, 4>> dY{"dY"};

    Output<Buffer<float16_t, 4>> dX{"dX"};    // channels x pos x chunk x head
    Output<Buffer<float16_t, 4>> dB{"dB"};    // state x pos x chunk x group
    Output<Buffer<float16_t, 4>> dC{"dC"};    // state x pos x chunk x group
    Output<Buffer<float, 2>> dDT{"dDT"};      // seq x head
    Output<Buffer<float, 1>> dA_out{"dA_out"};  // head

    void generate() {
        const int L = chunk;
        const int nt = (int)seq / L;
        const int hpg = (int)heads / (int)groups;

        Var d("d"), p("p"), k("k"), i("i"), j("j"), t("t"), b("b"), g("g");
        Var tt("tt"), kk("kk");

        auto group_of = [&](Expr head) {
            return (int)groups == (int)heads ? head : head / hpg;
        };

        Delta.dim(0).set_min(0);
        Delta.dim(1).set_stride((int)seq);

        // ---------------- Recomputed forward quantities ----------------

        Func cumdelta = Func(Float(32), "cumdelta");
        cumdelta(k, t, b) = Delta(t * L + k, b) +
                            select(k <= 0, 0.f, likely(cumdelta(k - 1, t, b)));

        // The running total of whole chunks of decay before chunk t, for the
        // gradient of A. A forward walk over chunks.
        Func cdpre = Func(Float(32), "cdpre");
        cdpre(t, b) = select(t <= 0, 0.f,
                             likely(cdpre(t - 1, b)) + cumdelta(L - 1, t - 1, b));

        Var from("from"), to("to");
        Func decay("decay");
        decay(from, to, t, b) =
            exp(A(b) * (cumdelta(to, t, b) - cumdelta(from, t, b)));

        RDom rp(0, state, "rp");
        Func qk("qk");
        qk(j, i, t, g) = 0.f;
        qk(j, i, t, g) += cast<float>(Cm(rp, t * L + i, g)) *
                          cast<float>(Bm(rp, t * L + j, g));

        // The decayed state operand, in both orientations: reduction-first
        // for the forward state multiply, state-first for the gradient's.
        Func Bmdf("Bmdf");
        Bmdf(j, p, t, b) = cast<float>(Bm(p, t * L + j, group_of(b))) *
                           Delta(t * L + j, b) * decay(j, L - 1, t, b);
        Func Bmd("Bmd");
        Bmd(p, j, t, b) = cast<float16_t>(Bmdf(j, p, t, b));
        Func Bmdf2("Bmdf2");
        Bmdf2(p, j, t, b) = cast<float>(Bm(p, t * L + j, group_of(b))) *
                            Delta(t * L + j, b) * decay(j, L - 1, t, b);
        Func Bmd2("Bmd2");
        Bmd2(p, j, t, b) = cast<float16_t>(Bmdf2(p, j, t, b));

        RDom rj(0, L, "rj");
        Func chunk_state("chunk_state");
        chunk_state(d, p, t, b) = 0.f;
        chunk_state(d, p, t, b) += cast<float>(Bmd(p, rj, t, b)) *
                                   cast<float>(X(d, t * L + rj, b));

        Func H = Func(Float(32), "H");
        H(d, p, t, b) = select(t <= 0,
                               0.f,
                               likely(H(d, p, t - 1, b) *
                                      exp(A(b) * cumdelta(L - 1, t - 1, b))) +
                                   chunk_state(d, p, t - 1, b));
        Func Hop("Hop");
        Hop(d, p, t, b) = cast<float16_t>(H(d, p, t, b));

        // ---------------- The gradient state, walked backwards ----------

        // C decayed from the start of its chunk, the mirror of Bmd.
        Func Cdf("Cdf");
        Cdf(i, p, t, b) = cast<float>(Cm(p, t * L + i, group_of(b))) *
                          exp(A(b) * cumdelta(i, t, b));
        Func Cd("Cd");
        Cd(p, i, t, b) = cast<float16_t>(Cdf(i, p, t, b));

        // What chunk t's output gradient contributes to the state before it.
        RDom ri(0, L, "ri");
        Func dG("dG");
        dG(d, p, t, b) = 0.f;
        dG(d, p, t, b) += cast<float>(Cd(p, ri, t, b)) *
                          cast<float>(dY(d, ri, t, b));

        // The carried gradient of the state at the END of chunk t, indexed
        // by flipped chunk so the walk counts upwards: dHnr(tt) is the
        // gradient at the end of chunk nt-1-tt, fed by every later chunk.
        Func dHnr = Func(Float(32), "dHnr");
        dHnr(d, p, tt, b) =
            select(tt <= 0,
                   0.f,
                   likely(dHnr(d, p, tt - 1, b) *
                          exp(A(b) * cumdelta(L - 1, nt - tt, b))) +
                       dG(d, p, nt - tt, b));
        Func dHopr("dHopr");
        dHopr(d, p, tt, b) = cast<float16_t>(dHnr(d, p, tt, b));

        // ---------------- dX ----------------

        // The forward's score, arguments ordered for a reduction over the
        // output positions instead of the inputs. Its raw scores come from a
        // transposed copy of qk, so the tile loads read them in the
        // orientation the reduction wants.
        Func qk2("qk2");
        qk2(i, j, t, g) = qk(j, i, t, g);
        // The mask multiplies rather than selects, so the raw scores are
        // read unconditionally and the masked-off region doesn't clamp the
        // bounds of what feeds the fragment.
        Func score2("score2");
        score2(i, j, t, b) = qk2(i, j, t, group_of(b)) * decay(j, i, t, b) *
                             Delta(t * L + j, b) *
                             select(j <= i, 1.f, 0.f);
        Func score2_h("score2_h");
        score2_h(i, j, t, b) = cast<float16_t>(score2(i, j, t, b));

        // The reduction runs over the positions at or after j, which is a
        // suffix - walked as a prefix from the far end, a tile at a time
        // with the order inside a tile kept forward, so the mask is an
        // upper bound on the loop the way the machinery likes.
        // TODO: prune the walk to the unmasked tiles. Written as a where
        // clause the mask's refinement clamps the fragment regions from
        // below, which the subtile analysis cannot see through; the full
        // walk multiplies by zeros instead.
        RDom riy(0, L, "riy");
        Expr rpos = (L - 16) - (riy / 16) * 16 + (riy % 16);
        Func dxi("dxi");
        dxi(d, j, t, b) = 0.f;
        dxi(d, j, t, b) += cast<float>(score2_h(rpos, j, t, b)) *
                           cast<float>(dY(d, rpos, t, b));

        Func dxe("dxe");
        dxe(d, j, t, b) = 0.f;
        dxe(d, j, t, b) += cast<float>(Bmd2(rp, j, t, b)) *
                           cast<float>(dHopr(d, rp, nt - 1 - t, b));

        // The last chunk's inputs reach no later chunk, but no masking is
        // needed to say so: the carried gradient is zero there, so the
        // inter-chunk half vanishes by itself.
        Func dxs("dxs");
        dxs(d, j, t, b) = cast<float16_t>(dxi(d, j, t, b) + dxe(d, j, t, b));

        dX(d, j, t, b) = dxs(d, j, t, b);

        // ---------------- dC, summed over the heads of a group ----------

        RDom rd(0, channels, "rd");
        Func xy("xy");
        xy(j, i, t, b) = 0.f;
        xy(j, i, t, b) += cast<float>(X(rd, t * L + j, b)) *
                          cast<float>(dY(rd, i, t, b));
        Func scoreT("scoreT");
        scoreT(j, i, t, b) = xy(j, i, t, b) * decay(j, i, t, b) *
                             Delta(t * L + j, b) * select(j <= i, 1.f, 0.f);
        Func scoreT_h("scoreT_h");
        scoreT_h(j, i, t, b) = cast<float16_t>(scoreT(j, i, t, b));

        Func dYH("dYH");
        dYH(p, i, t, b) = 0.f;
        dYH(p, i, t, b) += cast<float>(Hop(rd, p, t, b)) *
                           cast<float>(dY(rd, i, t, b));

        RDom rch(0, L, 0, hpg, "rch");
        RVar rcj = rch.x;
        RVar rchh = rch.y;
        Func dCa("dCa");
        dCa(p, i, t, g) = 0.f;
        dCa(p, i, t, g) += cast<float>(scoreT_h(rcj, i, t, g * hpg + rchh)) *
                           cast<float>(Bm(p, t * L + rcj, g));
        RDom rhh(0, hpg, "rhh");
        Func dCb("dCb");
        dCb(p, i, t, g) = 0.f;
        dCb(p, i, t, g) +=
            exp(A(g * hpg + rhh) * cumdelta(i, t, g * hpg + rhh)) *
            dYH(p, i, t, g * hpg + rhh);

        dC(p, i, t, g) = cast<float16_t>(dCa(p, i, t, g) + dCb(p, i, t, g));

        // ---------------- dB, summed over the heads of a group ----------

        // xy again, oriented for a reduction over the output positions.
        Func xy2("xy2");
        xy2(i, j, t, b) = 0.f;
        xy2(i, j, t, b) += cast<float>(dY(rd, i, t, b)) *
                           cast<float>(X(rd, t * L + j, b));
        Func scoreT2("scoreT2");
        scoreT2(i, j, t, b) = xy2(i, j, t, b) * decay(j, i, t, b) *
                              Delta(t * L + j, b) * select(j <= i, 1.f, 0.f);
        Func scoreT2_h("scoreT2_h");
        scoreT2_h(i, j, t, b) = cast<float16_t>(scoreT2(i, j, t, b));

        Func XdH("XdH");
        XdH(p, j, t, b) = 0.f;
        XdH(p, j, t, b) += cast<float>(X(rd, t * L + j, b)) *
                           cast<float>(dHopr(rd, p, nt - 1 - t, b));

        RDom rbh(0, L, 0, hpg, "rbh");
        RVar rbi = rbh.x;
        RVar rbhh = rbh.y;
        Func dBa("dBa");
        dBa(p, j, t, g) = 0.f;
        dBa(p, j, t, g) += cast<float>(scoreT2_h(rbi, j, t, g * hpg + rbhh)) *
                           cast<float>(Cm(p, t * L + rbi, g));
        Func dBb("dBb");
        dBb(p, j, t, g) = 0.f;
        dBb(p, j, t, g) += Delta(t * L + j, g * hpg + rhh) *
                           decay(j, L - 1, t, g * hpg + rhh) *
                           XdH(p, j, t, g * hpg + rhh);

        dB(p, j, t, g) = cast<float16_t>(dBa(p, j, t, g) + dBb(p, j, t, g));

        // ---------------- ddt and dA ----------------

        Func dYY("dYY");
        dYY(k, t, b) = 0.f;
        dYY(k, t, b) += cast<float>(dY(rd, k, t, b)) * cast<float>(Y(rd, k, t, b));
        Func XdX("XdX");
        XdX(k, t, b) = 0.f;
        XdX(k, t, b) += cast<float>(X(rd, t * L + k, b)) * cast<float>(dX(rd, k, t, b));

        Func ddAcs("ddAcs");
        ddAcs(k, t, b) = dYY(k, t, b) - XdX(k, t, b);

        // Suffix sums of ddAcs: whole chunks first, then within a chunk,
        // both walked backwards over flipped indices.
        RDom rk(0, L, "rk");
        Func csum("csum");
        csum(t, b) = 0.f;
        csum(t, b) += ddAcs(rk, t, b);

        Func sfxcr = Func(Float(32), "sfxcr");
        sfxcr(tt, b) = select(tt <= 0, 0.f,
                              likely(sfxcr(tt - 1, b)) + csum(nt - tt, b));

        Func sfxr = Func(Float(32), "sfxr");
        sfxr(kk, t, b) = ddAcs(L - 1 - kk, t, b) +
                         select(kk <= 0, sfxcr(nt - 1 - t, b),
                                likely(sfxr(kk - 1, t, b)));

        Func ddtF("ddtF");
        ddtF(k, t, b) = XdX(k, t, b) / Delta(t * L + k, b) +
                        A(b) * sfxr(L - 1 - k, t, b);

        dDT(k, b) = ddtF(k % L, k / L, b);

        Func pdA("pdA");
        pdA(t, b) = 0.f;
        pdA(t, b) += ddAcs(rk, t, b) * (cumdelta(rk, t, b) + cdpre(t, b));
        RDom rt(0, nt, "rt");
        dA_out(b) = 0.f;
        dA_out(b) += pdA(rt, b);

        try {
            if (!using_autoscheduler()) {
                if (wmma) {
                    schedule_wmma(cumdelta, cdpre, qk, qk2, Bmdf, Bmdf2,
                                  chunk_state,
                                  H, Hop, Cdf, dG, dHnr, dHopr, score2, score2_h,
                                  dxi, dxe, dxs, xy, scoreT, scoreT_h, dYH,
                                  dCa, dCb, xy2, scoreT2, scoreT2_h, XdH, dBa,
                                  dBb, dYY, XdX, ddAcs, csum, sfxcr, sfxr, pdA,
                                  d, p, k, i, j, t, b, g, tt, kk);
                } else {
                    schedule_simple(cumdelta, cdpre, qk, chunk_state, H, Hop,
                                    dG, dHnr, dHopr, dxi, dxe, xy, scoreT_h, dYH,
                                    dCa, dCb, xy2, scoreT2_h, XdH, dBa, dBb,
                                    dYY, XdX, ddAcs, csum, sfxcr, sfxr, pdA,
                                    d, p, k, i, j, t, b, g, tt, kk);
                }
            }
        } catch (Halide::CompileError &e) {
            std::cerr << e.what() << "\n";
        }
    }

private:
    static constexpr int tile = 16;

    // The forward's tensor core structures, mirrored. The fused walks carry
    // their state in registers and compute each chunk's contribution on the
    // tensor cores; the dX kernel is the forward's output kernel with the
    // causal mask transposed; the group-summed gradients walk the heads of
    // a group serially inside their blocks, accumulating on fragments.
    void schedule_wmma(Func cumdelta, Func cdpre, Func qk, Func qk2,
                       Func Bmdf, Func Bmdf2, Func chunk_state, Func H, Func Hop,
                       Func Cdf, Func dG, Func dHnr, Func dHopr, Func score2,
                       Func score2_h, Func dxi, Func dxe, Func dxs, Func xy,
                       Func scoreT, Func scoreT_h, Func dYH, Func dCa,
                       Func dCb, Func xy2, Func scoreT2, Func scoreT2_h,
                       Func XdH, Func dBa, Func dBb, Func dYY, Func XdX,
                       Func ddAcs, Func csum, Func sfxcr, Func sfxr, Func pdA,
                       Var d, Var p, Var k, Var i, Var j, Var t, Var b,
                       Var g, Var tt, Var kk) {
        const int L = chunk;
        const int nt = (int)seq / L;

        Var xo("xo"), xi_("xi"), rxi("rxi"), ryi("ryi");
        Var io("io"), ii("ii"), iw("iw"), ii2("ii2"), jo("jo"), ji("ji");
        RVar rro("rro"), rri("rri");
        Var x0("x0"), x1("x1"), y0("y0"), y1("y1");

        // ---- The scans ----
        cumdelta.compute_root().align_bounds(k, 2).align_storage(k, 2)
            .reorder(k, t, b).gpu_blocks(b).gpu_threads(t);
        cdpre.compute_root().reorder(t, b).gpu_blocks(b);
        sfxcr.compute_root().reorder(tt, b).gpu_blocks(b);
        sfxr.compute_root().reorder(kk, t, b).gpu_blocks(b).gpu_threads(t);

        // ---- qk, once per group, spread over blocks ----
        Func qk_at = qk.in();
        qk_at.compute_root()
            .tile(j, i, rxi, ryi, tile, tile)
            .split(j, jo, ji, 4)
            .reorder(rxi, ryi, ji, i, jo, t, g)
            .unroll(ji)
            .gpu_blocks(jo, t, g)
            .gpu_threads(i)
            .tile_store(rxi, ryi);
        qk.compute_at(qk_at, jo).store_in(MemoryType::Tile).unroll(t);
        qk.update().unroll(t);
        qk.tile(j, i, rxi, ryi, tile, tile)
            .unroll(j)
            .gpu_threads(i)
            .tile_init(rxi, ryi);
        qk.update()
            .tile(j, i, rxi, ryi, tile, tile)
            .split(rp_var(qk), rro, rri, tile)
            .reorder(j, i, rro)
            .unroll(j)
            .gpu_threads(i)
            .tile_matmul(rri, rxi, ryi);

        // ---- The forward state walk, fused, copied from the forward ----
        Var hto("hto"), hti("hti"), hw("hw"), hp("hp"), ddo("ddo"), ddi("ddi");
        Func cs = chunk_state.in();
        chunk_state.compute_at(Hop, hti).unroll(t);
        chunk_state.update().unroll(t);
        cs.compute_at(Hop, hti)
            .store_in(MemoryType::GPUShared)
            .unroll(cs.args()[2])
            .tile(d, p, rxi, ryi, tile, tile)
            .unroll(d)
            .gpu_threads(p)
            .tile_store(rxi, ryi);
        chunk_state
            .store_in(MemoryType::Tile)
            .tile(d, p, rxi, ryi, tile, tile)
            .unroll(d)
            .gpu_threads(p)
            .tile_init(rxi, ryi);
        chunk_state.update()
            .tile(d, p, rxi, ryi, tile, tile)
            .split(rj_var(chunk_state), RVar("rjo"), RVar("rji"), 4 * tile)
            .split(RVar("rji"), rro, rri, tile)
            .reorder(d, rro, p, RVar("rjo"))
            .unroll(d)
            .gpu_threads(p)
            .tile_matmul(rri, rxi, ryi);
        {
            Func Bml = Func(Bm).in(Bmdf);
            Bml.compute_at(chunk_state, rro)
                .store_in(MemoryType::Tile)
                .reorder_storage(Bml.args()[1], Bml.args()[0], Bml.args()[2])
                .tile(Bml.args()[0], Bml.args()[1], rxi, ryi, tile, tile)
                .tile_load(rxi, ryi);
            Bmdf.compute_at(chunk_state, rro)
                .store_in(MemoryType::Tile)
                .tile(j, p, rxi, ryi, tile, tile)
                .tile_init(rxi, ryi);
            Func xs = Func(X).in(chunk_state);
            Var so("so"), si("si"), fu("fu"), fo("fo"), fi("fi"), w("w"), l("l");
            xs.compute_at(chunk_state, RVar("rjo"))
                .store_in(MemoryType::GPUSharedAsync)
                .align_storage(xs.args()[0], (int)channels + 8)
                .split(xs.args()[0], so, si, 8)
                .fuse(so, xs.args()[1], fu)
                .split(fu, fo, fi, 32 * ((int)state / tile))
                .split(fi, w, l, 32)
                .reorder(si, l, w, fo)
                .vectorize(si)
                .gpu_lanes(l)
                .gpu_threads(w);
        }
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

        // ---- The gradient state walk, the mirror image ----
        Var gto("gto"), gti("gti");
        Func dgs = dG.in();
        dG.compute_at(dHopr, gti).unroll(t);
        dG.update().unroll(t);
        dgs.compute_at(dHopr, gti)
            .store_in(MemoryType::GPUShared)
            .unroll(dgs.args()[2])
            .tile(d, p, rxi, ryi, tile, tile)
            .unroll(d)
            .gpu_threads(p)
            .tile_store(rxi, ryi);
        dG.store_in(MemoryType::Tile)
            .tile(d, p, rxi, ryi, tile, tile)
            .unroll(d)
            .gpu_threads(p)
            .tile_init(rxi, ryi);
        dG.update()
            .tile(d, p, rxi, ryi, tile, tile)
            .split(ri_var(dG), RVar("rio"), RVar("rii"), 4 * tile)
            .split(RVar("rii"), rro, rri, tile)
            .reorder(d, rro, p, RVar("rio"))
            .unroll(d)
            .gpu_threads(p)
            .tile_matmul(rri, rxi, ryi);
        {
            Func Cml = Func(Cm).in(Cdf);
            Cml.compute_at(dG, rro)
                .store_in(MemoryType::Tile)
                .reorder_storage(Cml.args()[1], Cml.args()[0], Cml.args()[2])
                .tile(Cml.args()[0], Cml.args()[1], rxi, ryi, tile, tile)
                .tile_load(rxi, ryi);
            Cdf.compute_at(dG, rro)
                .store_in(MemoryType::Tile)
                .tile(i, p, rxi, ryi, tile, tile)
                .tile_init(rxi, ryi);
            Func dys = Func(dY).in(dG);
            Var so("so"), si("si"), fu("fu"), fo("fo"), fi("fi"), w("w"), l("l");
            dys.compute_at(dG, RVar("rio"))
                .store_in(MemoryType::GPUSharedAsync)
                .align_storage(dys.args()[0], (int)channels + 8)
                .split(dys.args()[0], so, si, 8)
                .fuse(so, dys.args()[1], fu)
                .split(fu, fo, fi, 32 * ((int)state / tile))
                .split(fi, w, l, 32)
                .reorder(si, l, w, fo)
                .vectorize(si)
                .gpu_lanes(l)
                .gpu_threads(w);
        }
        dHopr.compute_root()
            .split(tt, gto, gti, 1)
            .split(d, ddo, ddi, 2 * tile)
            .split(p, hw, hp, tile)
            .reorder(ddi, hp, hw, gti, gto, ddo, b)
            .gpu_blocks(ddo, b)
            .gpu_lanes(ddi)
            .gpu_threads(hw);
        dHnr.compute_at(dHopr, gti)
            .store_at(dHopr, ddo)
            .slide(dHopr, tt)
            .store_in(MemoryType::Stack)
            .split(p, hw, hp, tile)
            .reorder(d, hp, hw)
            .gpu_lanes(d)
            .gpu_threads(hw);

        // ---- dX: the forward's output kernel with the mask transposed ----
        const int pos_tiles = 4;
        const int idx_per_warp = 2;
        Func(dX).compute_root()
            .tile(d, dX.args()[1], rxi, ryi, tile, tile)
            .split(dX.args()[1], io, ii, pos_tiles)
            .reorder(rxi, ryi, d, ii, io, t, b)
            .gpu_blocks(io, t, b)
            .tile_store(rxi, ryi)
            .split(ii, iw, ii2, idx_per_warp)
            .unroll(d)
            .unroll(ii2)
            .gpu_threads(iw);
        for (Func f : {dxi, dxe, dxs}) {
            f.compute_at(Func(dX), io)
                .store_in(MemoryType::Tile)
                .tile(d, j, rxi, ryi, tile, tile)
                .tile_init(rxi, ryi)
                .split(j, iw, ii2, idx_per_warp)
                .unroll(d)
                .unroll(ii2)
                .gpu_threads(iw);
        }
        for (Func f : {dxi, dxe}) {
            f.update().tile(d, j, rxi, ryi, tile, tile);
        }
        dxi.update().split(riy_var(dxi), rro, rri, tile);
        dxi.update().prefetch(dY, rro, rro, std::max(1, 3 * ((int)chunk / tile) / 4));
        dxe.update().split(rp_var2(dxe), rro, rri, tile);
        dxi.update()
            .split(j, iw, ii2, idx_per_warp)
            .reorder(d, rro, ii2, iw)
            .unroll(d)
            .unroll(ii2)
            .gpu_threads(iw)
            .tile_matmul(rri, rxi, ryi);
        dxe.update()
            .split(j, iw, ii2, idx_per_warp)
            .reorder(ii2, d, rro, iw)
            .unroll(d)
            .unroll(ii2)
            .gpu_threads(iw)
            .tile_matmul(rri, rxi, ryi);
        for (Func f : {dxi, dxe, dxs}) {
            f.unroll(t);
        }
        for (Func f : {dxi, dxe}) {
            f.update().unroll(t);
        }
        {
            Func Bml2 = Func(Bm).in(Bmdf2);
            Bml2.compute_at(dxe, rro)
                .store_in(MemoryType::Tile)
                .tile(Bml2.args()[0], Bml2.args()[1], rxi, ryi, tile, tile)
                .unroll(Bml2.args()[1])
                .tile_load(rxi, ryi);
            Bmdf2.compute_at(dxe, rro)
                .store_in(MemoryType::Tile)
                .tile(p, j, rxi, ryi, tile, tile)
                .unroll(j)
                .tile_init(rxi, ryi);
        }
        qk2.compute_root()
            .split(i, x0, x1, 8)
            .split(j, y0, y1, 8)
            .reorder(x1, y1, x0, y0)
            .gpu_blocks(t, g)
            .gpu_threads(x1, y1);
        {
            Func qkl2 = qk2.in(score2);
            qkl2.compute_at(dxi, rro)
                .store_in(MemoryType::Tile)
                .bound_extent(i, tile)
                .bound_storage(i, tile)
                .tile(i, j, rxi, ryi, tile, tile)
                .tile_load(rxi, ryi);
            score2.compute_at(dxi, rro)
                .store_in(MemoryType::Tile)
                .bound_extent(i, tile)
                .bound_storage(i, tile)
                .tile(i, j, rxi, ryi, tile, tile)
                .tile_init(rxi, ryi);
        }

        // ---- The group-summed gradients ----
        // A block owns a strip of output positions of one chunk of one
        // group and sweeps the heads of the group serially, its accumulators
        // riding in fragments the whole way. Per head, the raw xy scores are
        // computed a tile at a time on the tensor cores, masked and decayed
        // where the multiply left them, and fed onward through the relayout;
        // the inter-chunk halves accumulate a scaled state multiply the same
        // way. All of the state's tiles ride in each warp, unrolled.
        {
            RVar rcjo("rcjo"), rcji("rcji"), hho("hho"), hhi("hhi");
            Var hb("hb"), tf("tf");
            const int hpg = (int)heads / (int)groups;
            // The sweep over a group's heads is split into partials, one per
            // block, or the group-summed kernels have too few blocks to fill
            // the card; a small reduction folds the partials afterwards.
            dCa.update().split(RVar("rch$y"), hho, hhi, std::max(1, hpg / 8));
            Func dCap = dCa.update().rfactor(hho, hb);
            Func dCapw = dCap.in();
            dCapw.compute_root()
                .tile(p, i, rxi, ryi, tile, tile)
                .split(i, io, ii, pos_tiles)
                .fuse(t, hb, tf)
                .reorder(rxi, ryi, p, ii, io, tf, g)
                .unroll(p)
                .unroll(ii)
                .gpu_blocks(io, tf, g)
                .gpu_threads(ii)
                .tile_store(rxi, ryi);
            dCap.compute_at(dCapw, io)
                .store_in(MemoryType::Tile)
                .tile(p, i, rxi, ryi, tile, tile)
                .unroll(p)
                .gpu_threads(i)
                .tile_init(rxi, ryi);
            dCap.update()
                .tile(p, i, rxi, ryi, tile, tile)
                .split(RVar("rch$x"), rcjo, rcji, tile)
                .reorder(p, hhi, rcjo, i)
                .unroll(p)
                .gpu_threads(i)
                .tile_matmul(rcji, rxi, ryi);
            // The B side of the matmul is per group, not per head, so its
            // fragments ride above the sweep over the heads.
            Func Bmf = Func(Bm).in(dCap);
            Bmf.compute_at(dCap, rcjo)
                .store_in(MemoryType::Tile)
                .tile(Bmf.args()[0], Bmf.args()[1], rxi, ryi, tile, tile)
                .unroll(Bmf.args()[0])
                .tile_load(rxi, ryi);
            dCa.compute_root()
                .split(p, x0, x1, 8)
                .split(i, y0, y1, 8)
                .reorder(x1, y1, x0, y0)
                .gpu_blocks(y0, t, g)
                .gpu_threads(x1, y1);
            dCa.update()
                .split(p, x0, x1, 8)
                .split(i, y0, y1, 8)
                .reorder(x1, y1, hho, x0, y0)
                .gpu_blocks(y0, t, g)
                .gpu_threads(x1, y1);
            xy.compute_at(dCap, hhi)
                .store_in(MemoryType::Tile)
                .bound_extent(j, tile)
                .bound_storage(j, tile)
                .bound_extent(i, tile)
                .bound_storage(i, tile)
                .tile(j, i, rxi, ryi, tile, tile)
                .tile_init(rxi, ryi);
            xy.update()
                .tile(j, i, rxi, ryi, tile, tile)
                .split(rd_var(xy), rro, rri, tile)
                .reorder(j, i, rro)
                .tile_matmul(rri, rxi, ryi);
            scoreT.compute_at(dCap, hhi)
                .store_in(MemoryType::Tile)
                .bound_extent(j, tile)
                .bound_storage(j, tile)
                .bound_extent(i, tile)
                .bound_storage(i, tile)
                .tile(j, i, rxi, ryi, tile, tile)
                .tile_init(rxi, ryi);
        }
        {
            RVar rbio("rbio"), rbii("rbii"), hho("hho"), hhi("hhi");
            Var hb("hb"), tf("tf");
            const int hpg = (int)heads / (int)groups;
            dBa.update().split(RVar("rbh$y"), hho, hhi, std::max(1, hpg / 8));
            Func dBap = dBa.update().rfactor(hho, hb);
            Func dBapw = dBap.in();
            dBapw.compute_root()
                .tile(p, j, rxi, ryi, tile, tile)
                .split(j, io, ii, pos_tiles)
                .fuse(t, hb, tf)
                .reorder(rxi, ryi, p, ii, io, tf, g)
                .unroll(p)
                .unroll(ii)
                .gpu_blocks(io, tf, g)
                .gpu_threads(ii)
                .tile_store(rxi, ryi);
            dBap.compute_at(dBapw, io)
                .store_in(MemoryType::Tile)
                .tile(p, j, rxi, ryi, tile, tile)
                .unroll(p)
                .gpu_threads(j)
                .tile_init(rxi, ryi);
            dBap.update()
                .tile(p, j, rxi, ryi, tile, tile)
                .split(RVar("rbh$x"), rbio, rbii, tile)
                .reorder(p, hhi, rbio, j)
                .unroll(p)
                .gpu_threads(j)
                .tile_matmul(rbii, rxi, ryi);
            Func Cmf = Func(Cm).in(dBap);
            Cmf.compute_at(dBap, rbio)
                .store_in(MemoryType::Tile)
                .tile(Cmf.args()[0], Cmf.args()[1], rxi, ryi, tile, tile)
                .unroll(Cmf.args()[0])
                .tile_load(rxi, ryi);
            dBa.compute_root()
                .split(p, x0, x1, 8)
                .split(j, y0, y1, 8)
                .reorder(x1, y1, x0, y0)
                .gpu_blocks(y0, t, g)
                .gpu_threads(x1, y1);
            dBa.update()
                .split(p, x0, x1, 8)
                .split(j, y0, y1, 8)
                .reorder(x1, y1, hho, x0, y0)
                .gpu_blocks(y0, t, g)
                .gpu_threads(x1, y1);
            xy2.compute_at(dBap, hhi)
                .store_in(MemoryType::Tile)
                .bound_extent(i, tile)
                .bound_storage(i, tile)
                .bound_extent(j, tile)
                .bound_storage(j, tile)
                .tile(i, j, rxi, ryi, tile, tile)
                .tile_init(rxi, ryi);
            xy2.update()
                .tile(i, j, rxi, ryi, tile, tile)
                .split(rd_var(xy2), rro, rri, tile)
                .reorder(i, j, rro)
                .tile_matmul(rri, rxi, ryi);
            scoreT2.compute_at(dBap, hhi)
                .store_in(MemoryType::Tile)
                .bound_extent(i, tile)
                .bound_storage(i, tile)
                .bound_extent(j, tile)
                .bound_storage(j, tile)
                .tile(i, j, rxi, ryi, tile, tile)
                .tile_init(rxi, ryi);
        }
        {
            Func dCbw = dCb.in();
            dCbw.compute_root()
                .tile(p, i, rxi, ryi, tile, tile)
                .split(i, io, ii, pos_tiles)
                .reorder(rxi, ryi, p, ii, io, t, g)
                .unroll(p)
                .unroll(ii)
                .gpu_blocks(io, t, g)
                .gpu_threads(ii)
                .tile_store(rxi, ryi);
            dCb.compute_at(dCbw, io)
                .store_in(MemoryType::Tile)
                .tile(p, i, rxi, ryi, tile, tile)
                .unroll(p)
                .gpu_threads(i)
                .tile_init(rxi, ryi);
            dCb.update()
                .tile(p, i, rxi, ryi, tile, tile)
                .reorder(p, i, RVar("rhh$x"))
                .unroll(p)
                .gpu_threads(i)
                .tile_init(rxi, ryi);
            dYH.compute_at(dCb, RVar("rhh$x"))
                .store_in(MemoryType::Tile)
                .tile(p, i, rxi, ryi, tile, tile)
                .unroll(p)
                .gpu_threads(i)
                .tile_init(rxi, ryi);
            dYH.update()
                .tile(p, i, rxi, ryi, tile, tile)
                .split(rd_var(dYH), rro, rri, tile)
                .reorder(p, i, rro)
                .unroll(p)
                .gpu_threads(i)
                .tile_matmul(rri, rxi, ryi);
        }
        {
            Func dBbw = dBb.in();
            dBbw.compute_root()
                .tile(p, j, rxi, ryi, tile, tile)
                .split(j, io, ii, pos_tiles)
                .reorder(rxi, ryi, p, ii, io, t, g)
                .unroll(p)
                .unroll(ii)
                .gpu_blocks(io, t, g)
                .gpu_threads(ii)
                .tile_store(rxi, ryi);
            dBb.compute_at(dBbw, io)
                .store_in(MemoryType::Tile)
                .tile(p, j, rxi, ryi, tile, tile)
                .unroll(p)
                .gpu_threads(j)
                .tile_init(rxi, ryi);
            dBb.update()
                .tile(p, j, rxi, ryi, tile, tile)
                .reorder(p, j, RVar("rhh$x"))
                .unroll(p)
                .gpu_threads(j)
                .tile_init(rxi, ryi);
            XdH.compute_at(dBb, RVar("rhh$x"))
                .store_in(MemoryType::Tile)
                .tile(p, j, rxi, ryi, tile, tile)
                .unroll(p)
                .gpu_threads(j)
                .tile_init(rxi, ryi);
            XdH.update()
                .tile(p, j, rxi, ryi, tile, tile)
                .split(rd_var(XdH), rro, rri, tile)
                .reorder(p, j, rro)
                .unroll(p)
                .gpu_threads(j)
                .tile_matmul(rri, rxi, ryi);
        }

        // ---- The small epilogue stages ----

        for (Func f : {Func(dB), Func(dC)}) {
            f.compute_root()
                .split(f.args()[0], x0, x1, 8)
                .split(f.args()[1], y0, y1, 8)
                .reorder(x1, y1, x0, y0)
                .gpu_blocks(f.args()[2], f.args()[3])
                .gpu_threads(x1, y1);
        }
        for (Func f : {dYY, XdX}) {
            RVar rdo("rdo"), rdi("rdi");
            Var ko("ko"), ki("ki");
            f.compute_root().reorder(k, t, b).gpu_blocks(t, b).gpu_threads(k);
            // A warp covers a row's channels: eight lanes of eight-wide
            // vectors span the 64 channels contiguously, and the row sum
            // finishes through atomic adds across those lanes.
            f.update()
                .split(rd_var(f), rdo, rdi, 8)
                .split(k, ko, ki, 64)
                .reorder(rdi, rdo, ki, ko, t, b)
                .atomic()
                .vectorize(rdi)
                .gpu_blocks(ko, t, b)
                .gpu_threads(rdo, ki);
        }
        ddAcs.compute_root().reorder(k, t, b).gpu_blocks(t, b).gpu_threads(k);
        csum.compute_root().gpu_blocks(b).gpu_threads(t);
        csum.update().gpu_blocks(b).gpu_threads(t);
        pdA.compute_root().gpu_blocks(b).gpu_threads(t);
        pdA.update().gpu_blocks(b).gpu_threads(t);
        Func(dDT).compute_root()
            .split(dDT.args()[0], x0, x1, 64)
            .gpu_blocks(x0, dDT.args()[1])
            .gpu_threads(x1);
        Func(dA_out).compute_root().gpu_blocks(dA_out.args()[0]);
        Func(dA_out).update().gpu_blocks(dA_out.args()[0]);

        const int L2 = chunk;
        Func(dX).bound(d, 0, channels)
            .bound(dX.args()[1], 0, L2)
            .bound(dX.args()[2], 0, nt)
            .bound(dX.args()[3], 0, heads);
        Func(dB).bound(dB.args()[0], 0, state)
            .bound(dB.args()[1], 0, L2)
            .bound(dB.args()[2], 0, nt)
            .bound(dB.args()[3], 0, groups);
        Func(dC).bound(dC.args()[0], 0, state)
            .bound(dC.args()[1], 0, L2)
            .bound(dC.args()[2], 0, nt)
            .bound(dC.args()[3], 0, groups);
        Func(dDT).bound(dDT.args()[0], 0, seq).bound(dDT.args()[1], 0, heads);
        Func(dA_out).bound(dA_out.args()[0], 0, heads);
    }

    // The reduction variables, looked up by the update they belong to.
    RVar rd_var(Func f) { return RVar(f.update(0).get_schedule().rvars()[0].var); }
    RVar rp_var(Func f) { return RVar(f.update(0).get_schedule().rvars()[0].var); }
    RVar rj_var(Func f) { return RVar(f.update(0).get_schedule().rvars()[0].var); }
    RVar ri_var(Func f) { return RVar(f.update(0).get_schedule().rvars()[0].var); }
    RVar riy_var(Func f) { return RVar(f.update(0).get_schedule().rvars()[0].var); }
    RVar rp_var2(Func f) { return RVar(f.update(0).get_schedule().rvars()[0].var); }

    // A plain GPU schedule: every stage compute_root, blocks over the outer
    // dimensions, serial reductions per thread. Slow, but the algorithm and
    // the walks are all exercised.
    void schedule_simple(Func cumdelta, Func cdpre, Func qk, Func chunk_state,
                         Func H, Func Hop, Func dG, Func dHnr, Func dHopr,
                         Func dxi, Func dxe, Func xy, Func scoreT_h, Func dYH,
                         Func dCa, Func dCb, Func xy2, Func scoreT2_h,
                         Func XdH, Func dBa, Func dBb, Func dYY, Func XdX,
                         Func ddAcs, Func csum, Func sfxcr, Func sfxr,
                         Func pdA,
                         Var d, Var p, Var k, Var i, Var j, Var t, Var b,
                         Var g, Var tt, Var kk) {
        const int L = chunk;
        const int nt = (int)seq / L;

        Var x0("x0"), x1("x1"), y0("y0"), y1("y1");

        // The scans: one thread per independent walk.
        cumdelta.compute_root().reorder(k, t, b).gpu_blocks(b).gpu_threads(t);
        cdpre.compute_root().reorder(t, b).gpu_blocks(b);
        sfxcr.compute_root().reorder(tt, b).gpu_blocks(b);
        sfxr.compute_root().reorder(kk, t, b).gpu_blocks(b).gpu_threads(t);

        // The forward and backward state walks: serial over chunks, threads
        // over the state.
        for (Func f : {H, dHnr}) {
            Var w("w"), v("v");
            f.compute_root()
                .split(d, x0, x1, 8)
                .split(p, y0, y1, 8)
                .reorder(x1, y1, f.args()[2], x0, y0, b)
                .gpu_blocks(x0, y0, b)
                .gpu_threads(x1, y1);
        }
        for (Func f : {Hop, dHopr}) {
            f.compute_root()
                .split(d, x0, x1, 8)
                .split(p, y0, y1, 8)
                .reorder(x1, y1, x0, y0)
                .gpu_blocks(f.args()[2], f.args()[3])
                .gpu_threads(x1, y1);
        }

        // Everything else: a thread per output element, reductions serial.
        auto flat = [&](Func f, Var a0, Var a1) {
            f.compute_root()
                .split(a0, x0, x1, 8)
                .split(a1, y0, y1, 8)
                .reorder(x1, y1, x0, y0)
                .gpu_blocks(f.args()[2], f.args()[3])
                .gpu_threads(x1, y1);
            f.update()
                .split(a0, x0, x1, 8)
                .split(a1, y0, y1, 8)
                .reorder(x1, y1, x0, y0)
                .gpu_blocks(f.args()[2], f.args()[3])
                .gpu_threads(x1, y1);
        };
        flat(qk, j, i);
        flat(chunk_state, d, p);
        flat(dG, d, p);
        flat(dxi, d, j);
        flat(dxe, d, j);


        Func(dX).compute_root()
            .split(d, x0, x1, 8)
            .split(dX.args()[1], y0, y1, 8)
            .reorder(x1, y1, x0, y0)
            .gpu_blocks(dX.args()[2], dX.args()[3])
            .gpu_threads(x1, y1);
        for (Func f : {Func(dB), Func(dC)}) {
            f.compute_root()
                .split(f.args()[0], x0, x1, 8)
                .split(f.args()[1], y0, y1, 8)
                .reorder(x1, y1, x0, y0)
                .gpu_blocks(f.args()[2], f.args()[3])
                .gpu_threads(x1, y1);
        }

        // The row dots and small reductions.
        for (Func f : {dYY, XdX}) {
            RVar rdo("rdo"), rdi("rdi");
            f.compute_root().reorder(k, t, b).gpu_blocks(t, b).gpu_threads(k);
            f.update()
                .split(rd_var(f), rdo, rdi, 8)
                .reorder(rdi, rdo, k, t, b)
                .atomic()
                .vectorize(rdi)
                .gpu_blocks(t, b)
                .gpu_threads(k);
        }
        ddAcs.compute_root().reorder(k, t, b).gpu_blocks(t, b).gpu_threads(k);
        csum.compute_root().gpu_blocks(b).gpu_threads(t);
        csum.update().gpu_blocks(b).gpu_threads(t);
        pdA.compute_root().gpu_blocks(b).gpu_threads(t);
        pdA.update().gpu_blocks(b).gpu_threads(t);
        Func(dDT).compute_root()
            .split(dDT.args()[0], x0, x1, 64)
            .gpu_blocks(x0, dDT.args()[1])
            .gpu_threads(x1);
        Func(dA_out).compute_root().gpu_blocks(dA_out.args()[0]);
        Func(dA_out).update().gpu_blocks(dA_out.args()[0]);

        Func(dX).bound(d, 0, channels)
            .bound(dX.args()[1], 0, L)
            .bound(dX.args()[2], 0, nt)
            .bound(dX.args()[3], 0, heads);
        Func(dB).bound(dB.args()[0], 0, state)
            .bound(dB.args()[1], 0, L)
            .bound(dB.args()[2], 0, nt)
            .bound(dB.args()[3], 0, groups);
        Func(dC).bound(dC.args()[0], 0, state)
            .bound(dC.args()[1], 0, L)
            .bound(dC.args()[2], 0, nt)
            .bound(dC.args()[3], 0, groups);
        Func(dDT).bound(dDT.args()[0], 0, seq).bound(dDT.args()[1], 0, heads);
        Func(dA_out).bound(dA_out.args()[0], 0, heads);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Mamba2Bwd, mamba2_bwd)
