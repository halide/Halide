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
        // output positions instead of the inputs.
        Func score2("score2");
        score2(i, j, t, b) =
            select(j <= i,
                   qk(j, i, t, group_of(b)) * decay(j, i, t, b) *
                       Delta(t * L + j, b),
                   0.f);
        Func score2_h("score2_h");
        score2_h(i, j, t, b) = cast<float16_t>(score2(i, j, t, b));

        RDom riy(0, L, "riy");
        riy.where(riy / 16 >= j / 16);
        Func dxi("dxi");
        dxi(d, j, t, b) = 0.f;
        dxi(d, j, t, b) += cast<float>(score2_h(riy, j, t, b)) *
                           cast<float>(dY(d, riy, t, b));

        Func dxe("dxe");
        dxe(d, j, t, b) = 0.f;
        dxe(d, j, t, b) += cast<float>(Bmd2(rp, j, t, b)) *
                           cast<float>(dHopr(d, rp, nt - 1 - t, b));

        // The last chunk's inputs reach no later chunk, so nothing flows
        // back into them through the state.
        Func dxs("dxs");
        dxs(d, j, t, b) = cast<float16_t>(
            dxi(d, j, t, b) +
            select(t >= nt - 1, 0.f, likely(dxe(d, j, t, b))));

        dX(d, j, t, b) = dxs(d, j, t, b);

        // ---------------- dC, summed over the heads of a group ----------

        RDom rd(0, channels, "rd");
        Func xy("xy");
        xy(j, i, t, b) = 0.f;
        xy(j, i, t, b) += cast<float>(X(rd, t * L + j, b)) *
                          cast<float>(dY(rd, i, t, b));
        Func scoreT("scoreT");
        scoreT(j, i, t, b) =
            select(j <= i,
                   xy(j, i, t, b) * decay(j, i, t, b) * Delta(t * L + j, b),
                   0.f);
        Func scoreT_h("scoreT_h");
        scoreT_h(j, i, t, b) = cast<float16_t>(scoreT(j, i, t, b));

        Func dYH("dYH");
        dYH(p, i, t, b) = 0.f;
        dYH(p, i, t, b) += cast<float>(Hop(rd, p, t, b)) *
                           cast<float>(dY(rd, i, t, b));

        RDom rch(0, L, 0, hpg, "rch");
        rch.where(rch.x / 16 <= i / 16);
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
            select(t <= 0, 0.f,
                   likely(exp(A(g * hpg + rhh) * cumdelta(i, t, g * hpg + rhh)) *
                          dYH(p, i, t, g * hpg + rhh)));

        dC(p, i, t, g) = cast<float16_t>(dCa(p, i, t, g) + dCb(p, i, t, g));

        // ---------------- dB, summed over the heads of a group ----------

        // xy again, oriented for a reduction over the output positions.
        Func xy2("xy2");
        xy2(i, j, t, b) = 0.f;
        xy2(i, j, t, b) += cast<float>(dY(rd, i, t, b)) *
                           cast<float>(X(rd, t * L + j, b));
        Func scoreT2("scoreT2");
        scoreT2(i, j, t, b) =
            select(j <= i,
                   xy2(i, j, t, b) * decay(j, i, t, b) * Delta(t * L + j, b),
                   0.f);
        Func scoreT2_h("scoreT2_h");
        scoreT2_h(i, j, t, b) = cast<float16_t>(scoreT2(i, j, t, b));

        Func XdH("XdH");
        XdH(p, j, t, b) = 0.f;
        XdH(p, j, t, b) += cast<float>(X(rd, t * L + j, b)) *
                           cast<float>(dHopr(rd, p, nt - 1 - t, b));

        RDom rbh(0, L, 0, hpg, "rbh");
        rbh.where(rbh.x / 16 >= j / 16);
        RVar rbi = rbh.x;
        RVar rbhh = rbh.y;
        Func dBa("dBa");
        dBa(p, j, t, g) = 0.f;
        dBa(p, j, t, g) += cast<float>(scoreT2_h(rbi, j, t, g * hpg + rbhh)) *
                           cast<float>(Cm(p, t * L + rbi, g));
        Func dBb("dBb");
        dBb(p, j, t, g) = 0.f;
        dBb(p, j, t, g) +=
            select(t >= nt - 1, 0.f,
                   likely(Delta(t * L + j, g * hpg + rhh) *
                          decay(j, L - 1, t, g * hpg + rhh) *
                          XdH(p, j, t, g * hpg + rhh)));

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
                schedule_simple(cumdelta, cdpre, qk, chunk_state, H, Hop,
                                dG, dHnr, dHopr, dxi, dxe, xy, scoreT_h, dYH,
                                dCa, dCb, xy2, scoreT2_h, XdH, dBa, dBb,
                                dYY, XdX, ddAcs, csum, sfxcr, sfxr, pdA,
                                d, p, k, i, j, t, b, g, tt, kk);
            }
        } catch (Halide::CompileError &e) {
            std::cerr << e.what() << "\n";
        }
    }

private:
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
        flat(xy, j, i);
        flat(dYH, p, i);
        flat(dCa, p, i);
        flat(dCb, p, i);
        flat(xy2, i, j);
        flat(XdH, p, j);
        flat(dBa, p, j);
        flat(dBb, p, j);

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
            f.compute_root().reorder(k, t, b).gpu_blocks(t, b).gpu_threads(k);
            f.update().reorder(k, t, b).gpu_blocks(t, b).gpu_threads(k);
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
