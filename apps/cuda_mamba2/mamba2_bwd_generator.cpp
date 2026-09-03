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
// where scoreT is the forward's score with xy in place of qk. The
// gradient of each step's log-decay increment a_k = A dt_k is gathered
// the way mamba_ssm's stable path gathers it: as the sum over the pairs
// of positions n < k <= m whose decay the increment sits inside, split by
// which chunk each end lies in (both in this chunk: the head's scaled
// score-gradient plane, prefixed along the input position and summed over
// the output positions; one end outside: the inter-chunk halves of dX and
// of y dotted with X and dY, prefixed or suffixed within the chunk; both
// ends outside: the carried state against the carried gradient). The
// adjoint identity dY.y - X.dX suffix-summed gives the same number in
// exact arithmetic and cancels catastrophically in float. ddt is the
// direct term plus A times that gradient, dA its weighted total.
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
    // How the state walks and the scans are written: as inductive Funcs
    // that refer to themselves one step back and can be slid into the loops
    // that consume them, or as update definitions over RDoms, which own
    // their walks and must be computed whole, at the precision they are
    // carried at, before anything reads them.
    enum class ScanForm { Inductive,
                          RDom };
    GeneratorParam<ScanForm> scan{"scan",
                                  ScanForm::Inductive,
                                  {{"inductive", ScanForm::Inductive},
                                   {"rdom", ScanForm::RDom}}};

    Input<Buffer<float16_t, 3>> X{"X"};
    Input<Buffer<float16_t, 3>> Bm{"Bm"};
    Input<Buffer<float16_t, 3>> Cm{"Cm"};
    Input<Buffer<float, 2>> Delta{"Delta"};
    Input<Buffer<float, 1>> A{"A"};
    // The saved output of the forward pass and the gradient arriving at it,
    // both in the forward output's chunked layout.
    Input<Buffer<float16_t, 4>> Y{"Y"};
    Input<Buffer<float16_t, 4>> dY{"dY"};

    // The increment-gradient stages, defined in generate() and scheduled
    // by both schedule functions.
    Func Uh, Ih, ebd, xyq, Mq32, Mqhi, Mqlo, Ms, Q, Qm, Qd, SX, Sst, Cdf2, Cd2,
        yprev, E2x, XdXe, XdXeb, XdXest, CTst, Sb, cterm, chunk_term, nx, pv, ga, pdA;
    Var jj{"jj"}, u{"u"};
    // The pair-sum accumulators carry their tile indices as dimensions
    // of their own (within-tile, tile), so the sweeps' predicates mention
    // nothing the tile operations vectorize.
    Var ii{"ii"}, it{"it"}, jji{"jji"}, jjt{"jjt"};

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
        RDom rmc(1, L - 1, "rmc");
        if (inductive()) {
            cumdelta(k, t, b) = Delta(t * L + k, b) +
                                select(k <= 0, 0.f, likely(cumdelta(k - 1, t, b)));
        } else {
            cumdelta(k, t, b) = Delta(t * L + k, b);
            cumdelta(rmc, t, b) = cumdelta(rmc - 1, t, b) + Delta(t * L + rmc, b);
        }

        // The running total of whole chunks of decay before chunk t, for the
        // gradient of A. A forward walk over chunks.
        Var from("from"), to("to");
        Func decay("decay");
        decay(from, to, t, b) =
            exp(A(b) * (cumdelta(to, t, b) - cumdelta(from, t, b)));

        RDom rp(0, state, "rp");
        Func qk("qk");
        qk(j, i, t, g) = 0.f;
        qk(j, i, t, g) += cast<float>(Cm(rp, t * L + i, g)) *
                          cast<float>(Bm(rp, t * L + j, g));

        // The per-position decay factors, precomputed into small hot
        // tables so the fragment elementwise stages that apply them load
        // one value instead of exponentiating per entry: the decayed-B
        // column scale, referenced to the end of its chunk, and the
        // decayed-C scale, referenced to the start.
        Func bfac("bfac");
        bfac(k, t, b) = Delta(t * L + k, b) * decay(k, L - 1, t, b);
        Func cfac("cfac");
        cfac(k, t, b) = exp(A(b) * cumdelta(k, t, b));
        // The decay between two positions splits into a factor of each,
        // referenced to the start of the chunk: cfac for the later, this
        // for the earlier, with the step size folded in. Two small tables
        // instead of an exponential per entry of every score plane.
        ebd = Func("ebd");
        ebd(k, t, b) = Delta(t * L + k, b) * exp(-A(b) * cumdelta(k, t, b));

        // The decayed state operand, in both orientations: reduction-first
        // for the forward state multiply, state-first for the gradient's.
        Func Bmdf("Bmdf");
        Bmdf(j, p, t, b) = cast<float>(Bm(p, t * L + j, group_of(b))) *
                           bfac(j, t, b);
        Func Bmd("Bmd");
        Bmd(p, j, t, b) = cast<float16_t>(Bmdf(j, p, t, b));
        Func Bmdf2("Bmdf2");
        Bmdf2(p, j, t, b) = cast<float>(Bm(p, t * L + j, group_of(b))) *
                            bfac(j, t, b);
        Func Bmd2("Bmd2");
        Bmd2(p, j, t, b) = cast<float16_t>(Bmdf2(p, j, t, b));

        RDom rj(0, L, "rj");
        Func chunk_state("chunk_state");
        chunk_state(d, p, t, b) = 0.f;
        chunk_state(d, p, t, b) += cast<float>(Bmd(p, rj, t, b)) *
                                   cast<float>(X(d, t * L + rj, b));

        Func H = inductive() ? Func(Float(32), "H") : Func("H");
        RDom rth(1, nt - 1, "rth");
        Func Hop("Hop");
        if (inductive()) {
            H(d, p, t, b) = select(t <= 0,
                                   0.f,
                                   likely(H(d, p, t - 1, b) *
                                          exp(A(b) * cumdelta(L - 1, t - 1, b))) +
                                       chunk_state(d, p, t - 1, b));
            Hop(d, p, t, b) = cast<float16_t>(H(d, p, t, b));
        } else {
            // The same recurrence as an update definition: the first chunk
            // carries nothing in, and each step folds the decayed carry
            // onto what the chunk before it left behind. The walk owns its
            // storage, so the state is written out at every step; the
            // half-precision copy the consumers want rides along as a
            // second component, so narrowing it is not a pass of its own.
            // The pure definition is undefined: the walk writes every step
            // before it is read.
            H(d, p, t, b) = {undef<float>(), undef<float16_t>()};
            H(d, p, 0, b) = {0.f, cast<float16_t>(0.f)};
            Expr carried = H(d, p, rth - 1, b)[0] *
                               exp(A(b) * cumdelta(L - 1, rth - 1, b)) +
                           chunk_state(d, p, rth - 1, b);
            H(d, p, rth, b) = {carried, cast<float16_t>(carried)};
            Hop(d, p, t, b) = H(d, p, t, b)[1];
        }

        // ---------------- The gradient state, walked backwards ----------

        // C decayed from the start of its chunk, the mirror of Bmd.
        Func Cdf("Cdf");
        Cdf(i, p, t, b) = cast<float>(Cm(p, t * L + i, group_of(b))) *
                          cfac(i, t, b);
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
        Func dHnr = inductive() ? Func(Float(32), "dHnr") : Func("dHnr");
        RDom rtd(1, nt - 1, "rtd");
        Func dHopr("dHopr");
        if (inductive()) {
            dHnr(d, p, tt, b) =
                select(tt <= 0,
                       0.f,
                       likely(dHnr(d, p, tt - 1, b) *
                              exp(A(b) * cumdelta(L - 1, nt - tt, b))) +
                           dG(d, p, nt - tt, b));
            dHopr(d, p, tt, b) = cast<float16_t>(dHnr(d, p, tt, b));
        } else {
            dHnr(d, p, tt, b) = {undef<float>(), undef<float16_t>()};
            dHnr(d, p, 0, b) = {0.f, cast<float16_t>(0.f)};
            Expr carried = dHnr(d, p, rtd - 1, b)[0] *
                               exp(A(b) * cumdelta(L - 1, nt - rtd, b)) +
                           dG(d, p, nt - rtd, b);
            dHnr(d, p, rtd, b) = {carried, cast<float16_t>(carried)};
            dHopr(d, p, tt, b) = dHnr(d, p, tt, b)[1];
        }

        // ---------------- dX ----------------

        // The forward's score, arguments ordered for a reduction over the
        // output positions instead of the inputs. Its raw scores come from a
        // transposed copy of qk, so the tile loads read them in the
        // orientation the reduction wants.
        Func qk2("qk2");
        qk2(i, j, t, g) = cast<float16_t>(qk(j, i, t, g));
        // The mask multiplies rather than selects, so the raw scores are
        // read unconditionally and the masked-off region doesn't clamp the
        // bounds of what feeds the fragment.
        Func score2("score2");
        score2(i, j, t, b) = cast<float>(qk2(i, j, t, group_of(b))) *
                             cfac(i, t, b) * ebd(j, t, b) *
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
        // The walk runs the position tiles in reverse; the score tiles for
        // positions before this input position's tile are zero.
        riy.where(riy / tile <= (L / tile - 1) - j / tile);
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
        scoreT(j, i, t, b) = xy(j, i, t, b) * cfac(i, t, b) * ebd(j, t, b) *
                             select(j <= i, 1.f, 0.f);

        // B and C are shared by a group, so the masked, decayed score
        // gradients sum over the group's heads on the L x L plane first,
        // leaving one small state-by-position multiply per group instead
        // of one per head. Narrowed copies in both orientations feed the
        // two multiplies.
        RDom rsh(0, hpg, "rsh");
        Func sg("sg");
        sg(j, i, t, g) = 0.f;
        sg(j, i, t, g) += scoreT(j, i, t, g * hpg + rsh);
        Func sgh("sgh");
        sgh(j, i, t, g) = cast<float16_t>(sg(j, i, t, g));
        Func sg2h("sg2h");
        sg2h(i, j, t, g) = cast<float16_t>(sg(j, i, t, g));

        Func dYH("dYH");
        dYH(p, i, t, b) = 0.f;
        dYH(p, i, t, b) += cast<float>(Hop(rd, p, t, b)) *
                           cast<float>(dY(rd, i, t, b));

        RDom rch(0, L, "rch");
        // Whole score tiles above the diagonal are zero, so the sweep over
        // them stops at the output position's diagonal tile.
        rch.where(rch.x / tile <= i / tile);
        Func dCa("dCa");
        dCa(p, i, t, g) = 0.f;
        dCa(p, i, t, g) += cast<float>(sgh(rch.x, i, t, g)) *
                           cast<float>(Bm(p, t * L + rch.x, g));
        RDom rhh(0, hpg, "rhh");
        Func dCb("dCb");
        dCb(p, i, t, g) = 0.f;
        dCb(p, i, t, g) +=
            cfac(i, t, g * hpg + rhh) *
            dYH(p, i, t, g * hpg + rhh);

        dC(p, i, t, g) = cast<float16_t>(dCa(p, i, t, g) + dCb(p, i, t, g));

        // ---------------- dB, summed over the heads of a group ----------

        Func XdH("XdH");
        XdH(p, j, t, b) = 0.f;
        XdH(p, j, t, b) += cast<float>(X(rd, t * L + j, b)) *
                           cast<float>(dHopr(rd, p, nt - 1 - t, b));

        RDom rbh(0, L, "rbh");
        // Whole score tiles above the diagonal are zero, so the sweep over
        // the output positions starts at this input position's tile.
        rbh.where(j / tile <= rbh.x / tile);
        Func dBa("dBa");
        dBa(p, j, t, g) = 0.f;
        dBa(p, j, t, g) += cast<float>(sg2h(rbh.x, j, t, g)) *
                           cast<float>(Cm(p, t * L + rbh.x, g));
        Func dBb("dBb");
        dBb(p, j, t, g) = 0.f;
        dBb(p, j, t, g) += bfac(j, t, g * hpg + rhh) *
                           XdH(p, j, t, g * hpg + rhh);

        dB(p, j, t, g) = cast<float16_t>(dBa(p, j, t, g) + dBb(p, j, t, g));

        // ---------------- ddt and dA ----------------
        //
        // The gradient of each step's log-decay increment, as the sums over
        // the position pairs it sits between, in the four cases of where
        // the pair's ends lie (see the header).

        // (1) Both ends in this chunk. The head's score-gradient plane,
        // scaled by its scores, is recomputed here from X and dY; a multiply
        // by a triangle of ones prefixes it along the input position, and a
        // masked sum over the output positions at or after each boundary
        // gives the boundary's total.
        Uh = Func("Uh");
        Uh(j, jj) = cast<float16_t>(select(j < jj, 1.f, 0.f));
        Ih = Func("Ih");
        Ih(j, jj) = cast<float16_t>(select(j == jj, 1.f, 0.f));
        xyq = Func("xyq");
        xyq(j, i, t, b) = 0.f;
        xyq(j, i, t, b) += cast<float>(X(rd, t * L + j, b)) *
                           cast<float>(dY(rd, i, t, b));
        Mq32 = Func("Mq32");
        Mq32(j, i, t, b) = xyq(j, i, t, b) * cfac(i, t, b) * ebd(j, t, b) *
                           qk(j, i, t, group_of(b)) * select(j <= i, 1.f, 0.f);
        // The plane passes through shared memory on its way into the
        // multiplies, which put the output position on the tile's rows so
        // that the sums over it are reductions along the tile's columns.
        // It goes as a pair of halves, the value and its rounding residual,
        // so the sums come out at single precision.
        Mqhi = Func("Mqhi");
        Mqhi(j, i, t, b) = cast<float16_t>(Mq32(j, i, t, b));
        Mqlo = Func("Mqlo");
        Mqlo(j, i, t, b) = cast<float16_t>(Mq32(j, i, t, b) - cast<float>(Mqhi(j, i, t, b)));
        Ms = Func("Ms");
        Ms(j, i, t, b) = {Mqhi(j, i, t, b), Mqlo(j, i, t, b)};
        // The prefix along the input position, from the triangle of ones,
        // and the plane's transpose, from the identity: the boundary sums
        // and the per-position row sums are then both reductions over the
        // output position. The diagonal never enters a boundary sum, since
        // it lies strictly inside neither half of the pair.
        // Input tiles past either the output tile or the boundary tile hold
        // only zeros: the sweep skips them, its predicate on tile indices
        // alone.
        RDom rq(0, tile, 0, L / tile, "rq");
        rq.where(rq.y <= min(it, jjt));
        Expr jq = rq.y * tile + rq.x, iq = it * tile + ii, jjq = jjt * tile + jji;
        Q = Func("Q");
        Q(ii, it, jji, jjt, t, b) = 0.f;
        // The boundary sums take the plane's half-precision value alone;
        // the row sums, which feed the step-size term divided by the step,
        // take its rounding residual too.
        Q(ii, it, jji, jjt, t, b) += cast<float>(Ms(jq, iq, t, b)[0]) * cast<float>(Uh(jq, jjq));
        // The identity's only nonzero tile is the diagonal one, so the
        // transpose sweeps just the input tile of its own boundaries.
        RDom rjd(0, tile, "rjd");
        Expr jd = jjt * tile + rjd;
        Qd = Func("Qd");
        Qd(ii, it, jji, jjt, t, b) = 0.f;
        Qd(ii, it, jji, jjt, t, b) += cast<float>(Ms(jd, iq, t, b)[0]) * cast<float>(Ih(jd, jjq));
        Qd(ii, it, jji, jjt, t, b) += cast<float>(Ms(jd, iq, t, b)[1]) * cast<float>(Ih(jd, jjq));
        // The mask multiplies rather than selects, so the whole plane is
        // read and the masked-off region doesn't clamp the tile's bounds.
        Qm = Func("Qm");
        Qm(ii, it, jji, jjt, t, b) = Q(ii, it, jji, jjt, t, b) * select(iq >= jjq, 1.f, 0.f);
        // The boundary sums, and the direct step-size term's intra-chunk
        // half: the row sums of the plane, diagonal included. One value of
        // each per position, held as tiles with the value repeated along
        // the tile's rows, and stored that way.
        RDom ri2(0, tile, 0, L / tile, "ri2");
        SX = Func("SX");
        SX(jji, jjt, t, b) = {0.f, 0.f};
        SX(jji, jjt, t, b) = {SX(jji, jjt, t, b)[0] + Qm(ri2.x, ri2.y, jji, jjt, t, b),
                              SX(jji, jjt, t, b)[1] + Qd(ri2.x, ri2.y, jji, jjt, t, b)};
        Sb = Func("Sb");
        Sb(u, jji, jjt, t, b) = {SX(jji, jjt, t, b)[0], SX(jji, jjt, t, b)[1]};
        Sst = Func("Sst");
        Sst(u, jji, jjt, t, b) = {Sb(u, jji, jjt, t, b)[0], Sb(u, jji, jjt, t, b)[1]};

        // (2) The input end in this chunk, the output end after it: the
        // inter-chunk half of dX, dotted with X, summed over the inputs
        // before the boundary. The half is a small state multiply,
        // recomputed rather than kept from the dX kernel.
        Cdf2 = Func("Cdf2");
        Cdf2(p, i, t, b) = cast<float>(Cm(p, t * L + i, group_of(b))) *
                           cfac(i, t, b);
        Cd2 = Func("Cd2");
        Cd2(p, i, t, b) = cast<float16_t>(Cdf2(p, i, t, b));
        yprev = Func("yprev");
        yprev(d, i, t, b) = 0.f;
        yprev(d, i, t, b) += cast<float>(Cd2(rp, i, t, b)) *
                             cast<float>(Hop(d, rp, t, b));
        // Both dot products, reduced along the channels into one value of
        // each per position, held as tiles with the value repeated along
        // the tile's rows, and stored that way.
        // Both dot products are taken in the dX kernel: the input-side one
        // from the inter-chunk half of dX it computes anyway, the
        // output-side one from the mirror multiply, and the two reduced
        // along the channels together. (Named over j, as dX is, so the
        // loop nests can be fused.)
        E2x = Func("E2x");
        E2x(d, j, t, b) = {dxe(d, j, t, b) * cast<float>(X(d, t * L + j, b)),
                           yprev(d, j, t, b) * cast<float>(dY(d, j, t, b))};
        XdXe = Func("XdXe");
        XdXe(j, t, b) = {0.f, 0.f};
        XdXe(j, t, b) = {XdXe(j, t, b)[0] + E2x(rd, j, t, b)[0], XdXe(j, t, b)[1] + E2x(rd, j, t, b)[1]};
        XdXeb = Func("XdXeb");
        XdXeb(u, j, t, b) = {XdXe(j, t, b)[0], XdXe(j, t, b)[1]};
        XdXest = Func("XdXest");
        XdXest(u, j, t, b) = {XdXeb(u, j, t, b)[0], XdXeb(u, j, t, b)[1]};

        // (4) Both ends outside this chunk: the state carried in against
        // the gradient carried out, through the whole chunk's decay.
        // Their dot product is taken in the dX kernel too, where both
        // states are resident from its multiplies: one thread per channel
        // dots its row of each, in the blocks that own the first channel
        // positions, and the pieces are summed here.
        RDom rcp(0, state, "rcp");
        CTst = Func("CTst");
        CTst(j, t, b) = 0.f;
        CTst(j, t, b) += select(j < (int)channels,
                                cast<float>(Hop(min(j, (int)channels - 1), rcp, t, b)) *
                                    cast<float>(dHopr(min(j, (int)channels - 1), rcp, nt - 1 - t, b)),
                                0.f);
        RDom rcs(0, channels, "rcs");
        cterm = Func("cterm");
        cterm(t, b) = 0.f;
        cterm(t, b) += CTst(rcs, t, b);
        chunk_term = Func("chunk_term");
        chunk_term(t, b) = cterm(t, b) * exp(A(b) * cumdelta(L - 1, t, b));

        // The prefix of (2) and the suffix of (3) within the chunk, walked
        // as the form walks its scans: the suffix over a flipped index.
        // The prefix of (2) and the suffix of (3) within the chunk, each
        // as a sum over the chunk's stored row: a thread's own serial loop
        // per position, its loads coalesced across the block, instead of
        // a walk with a load in every step of its chain.
        RDom rsk(0, L, "rsk");
        nx = Func("nx");
        nx(k, t, b) = 0.f;
        nx(k, t, b) += select(rsk < k, XdXest(0, rsk, t, b)[0], 0.f);
        pv = Func("pv");
        pv(k, t, b) = 0.f;
        pv(k, t, b) += select(rsk >= k, XdXest(0, rsk, t, b)[1], 0.f);
        ga = Func("ga");
        ga(k, t, b) = Sst(0, k % tile, k / tile, t, b)[0] + pv(k, t, b) + nx(k, t, b) +
                      chunk_term(t, b);

        // The direct step-size term, X dotted with both halves of dX at
        // single precision.
        Func ddtF("ddtF");
        ddtF(k, t, b) = (Sst(0, k % tile, k / tile, t, b)[1] + XdXest(0, k, t, b)[0]) / Delta(t * L + k, b) +
                        A(b) * ga(k, t, b);

        dDT(k, b) = ddtF(k % L, k / L, b);

        RDom rk(0, L, "rk");
        pdA = Func("pdA");
        pdA(t, b) = 0.f;
        pdA(t, b) += ga(rk, t, b) * Delta(t * L + rk, b);
        RDom rt(0, nt, "rt");
        dA_out(b) = 0.f;
        dA_out(b) += pdA(rt, b);

        try {
            if (!using_autoscheduler()) {
                if (wmma) {
                    schedule_wmma(cumdelta, bfac, cfac, qk, qk2,
                                  Bmdf, Bmdf2,
                                  chunk_state,
                                  H, Hop, Cdf, dG, dHnr, dHopr, score2, score2_h,
                                  dxi, dxe, dxs, xy, sg, sgh, sg2h, dYH,
                                  dCa, dCb, XdH, dBa,
                                  dBb,
                                  d, p, k, i, j, t, b, g, tt, kk);
                } else {
                    schedule_simple(cumdelta, qk, chunk_state, H, Hop,
                                    dG, dHnr, dHopr, dxi, dxe, dYH,
                                    dCa, dCb, XdH, dBa, dBb,
                                    d, p, k, i, j, t, b, g, tt, kk);
                }
            }
        } catch (Halide::CompileError &e) {
            std::cerr << e.what() << "\n";
        }
    }

private:
    static constexpr int tile = 16;

    bool inductive() const {
        return scan == ScanForm::Inductive;
    }

    // The forward's tensor core structures, mirrored. The fused walks carry
    // their state in registers and compute each chunk's contribution on the
    // tensor cores; the dX kernel is the forward's output kernel with the
    // causal mask transposed; the group-summed gradients walk the heads of
    // a group serially inside their blocks, accumulating on fragments.
    void schedule_wmma(Func cumdelta, Func bfac, Func cfac,
                       Func qk, Func qk2,
                       Func Bmdf, Func Bmdf2, Func chunk_state, Func H, Func Hop,
                       Func Cdf, Func dG, Func dHnr, Func dHopr, Func score2,
                       Func score2_h, Func dxi, Func dxe, Func dxs, Func xy,
                       Func sg, Func sgh, Func sg2h, Func dYH, Func dCa,
                       Func dCb,
                       Func XdH, Func dBa, Func dBb,
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
        for (Func f : {bfac, cfac, ebd}) {
            f.compute_root().reorder(k, t, b).gpu_blocks(t, b).gpu_threads(k);
        }
        cfac.compute_with(bfac, k);
        ebd.compute_with(cfac, k);
        if (!inductive()) {
            cumdelta.update()
                .reorder(RVar("rmc$x"), t, b)
                .gpu_blocks(b)
                .gpu_threads(t);
        }
        // The within-chunk sums: a thread's own serial loop each.
        nx.compute_at(ga, k);
        pv.compute_at(ga, k);

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

        // ---- The forward state walk: fused when the state is inductive,
        // or a chunk-state kernel plus a flat stored walk when it is not ----
        Var hto("hto"), hti("hti"), hw("hw"), hp("hp"), ddo("ddo"), ddi("ddi");
        Var tb("tb"), po("po"), po3("po3"), pw3("pw3");
        if (inductive()) {
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
        } else {
            // The walk is the kernel, shaped as the fused form's: a block
            // owns a slice of the state for every chunk, computes the chunk
            // state a step at a time into shared memory, and folds it onto
            // the carry it wrote the step before.
            Func cs = chunk_state.in();
            chunk_state.compute_at(H, RVar("rth$x")).unroll(t);
            chunk_state.update().unroll(t);
            cs.compute_at(H, RVar("rth$x"))
                .store_in(MemoryType::GPUShared)
                .unroll(cs.args()[2])
                .tile(d, p, rxi, ryi, tile, tile)
                .unroll(d)
                .gpu_threads(p)
                .tile_store(rxi, ryi);
            H.compute_root();
            H.update(0)
                .split(d, x0, x1, 8)
                .split(p, y0, y1, 8)
                .reorder(x1, y1, x0, y0)
                .gpu_blocks(y0, b)
                .gpu_threads(x1, y1);
            H.update(1)
                .split(d, ddo, ddi, 2 * tile)
                .split(p, hw, hp, tile)
                .reorder(ddi, hp, hw, RVar("rth$x"), ddo, b)
                .gpu_blocks(ddo, b)
                .gpu_lanes(ddi)
                .gpu_threads(hw);
        }
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
            .unroll(rro)
            .unroll(RVar("rjo"))
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
        if (inductive()) {
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
        }

        // ---- The gradient state walk, the mirror image ----
        Var gto("gto"), gti("gti");
        if (inductive()) {
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
        } else {
            Func dgs = dG.in();
            dG.compute_at(dHnr, RVar("rtd$x")).unroll(t);
            dG.update().unroll(t);
            dgs.compute_at(dHnr, RVar("rtd$x"))
                .store_in(MemoryType::GPUShared)
                .unroll(dgs.args()[2])
                .tile(d, p, rxi, ryi, tile, tile)
                .unroll(d)
                .gpu_threads(p)
                .tile_store(rxi, ryi);
            dHnr.compute_root();
            dHnr.update(0)
                .split(d, x0, x1, 8)
                .split(p, y0, y1, 8)
                .reorder(x1, y1, x0, y0)
                .gpu_blocks(y0, b)
                .gpu_threads(x1, y1);
            dHnr.update(1)
                .split(d, ddo, ddi, 2 * tile)
                .split(p, hw, hp, tile)
                .reorder(ddi, hp, hw, RVar("rtd$x"), ddo, b)
                .gpu_blocks(ddo, b)
                .gpu_lanes(ddi)
                .gpu_threads(hw);
        }
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
            .unroll(rro)
            .unroll(RVar("rio"))
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
        if (inductive()) {
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
        }

        // ---- dX: the forward's output kernel with the mask transposed ----
        const int pos_tiles = 4;
        const int idx_per_warp = 2;
        Func(dX).compute_root()
            .tile(d, dX.args()[1], rxi, ryi, tile, tile)
            .split(dX.args()[1], io, ii, 2)
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
            .gpu_blocks(y0, t, g)
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

        // ---- The input-side dot product, in the dX kernel ----
        // XdXest's loop nest is fused with dX's at the block level, so
        // the inter-chunk half of dX is dotted with X where it is made.
        XdXest.compute_root()
            .tile(u, XdXest.args()[1], rxi, ryi, tile, tile)
            .split(XdXest.args()[1], io, ii, 2)
            .split(ii, iw, ii2, idx_per_warp)
            .reorder(rxi, ryi, u, ii2, iw, io, t, b)
            .unroll(u)
            .unroll(ii2)
            .gpu_blocks(io, t, b)
            .gpu_threads(iw)
            .tile_store(rxi, ryi);
        XdXest.compute_with(Func(dX), io);
        // The trace pieces: a plain stage in the same kernel, its loop
        // nest split the way dX's is so the two can be fused.
        CTst.bound(j, 0, L).bound(t, 0, nt).bound(b, 0, heads);
        // A plain stage beside tile operations needs a lane loop of the
        // warp's width to share their thread dimension.
        Var lane("lane");
        CTst.compute_root()
            .split(j, j, lane, 2 * tile)
            .split(j, io, ii, 1)
            .reorder(lane, ii, io, t, b)
            .gpu_blocks(io, t, b)
            .gpu_lanes(lane);
        CTst.update()
            .split(j, j, lane, 2 * tile)
            .split(j, io, ii, 1)
            .reorder(RVar("rcp$x"), lane, ii, io, t, b)
            .gpu_blocks(io, t, b)
            .gpu_lanes(lane);
        CTst.compute_with(XdXest, io);
        CTst.update().compute_with(XdXest, io);
        XdXeb.compute_at(XdXest, iw)
            .store_in(MemoryType::Tile)
            .tile(u, XdXeb.args()[1], rxi, ryi, tile, tile)
            .unroll(XdXeb.args()[1])
            .tile_init(rxi, ryi);
        XdXe.compute_at(XdXest, iw)
            .store_in(MemoryType::Tile)
            .split(XdXe.args()[0], XdXe.args()[0], ryi, tile)
            .unroll(XdXe.args()[0])
            .vectorize(ryi);
        XdXe.update()
            .split(rd_var(XdXe), rro, rri, tile)
            .split(XdXe.args()[0], XdXe.args()[0], ryi, tile)
            .reorder(ryi, XdXe.args()[0], rro)
            .unroll(XdXe.args()[0])
            .unroll(rro)
            .tile_reduce(rri, ryi);
        {
            for (Func ld : {Func(X).in(E2x), Func(dY).in(E2x)}) {
                ld.compute_at(XdXe, rro)
                    .store_in(MemoryType::Tile)
                    .bound_extent(ld.args()[0], tile)
                    .bound_storage(ld.args()[0], tile)
                    .tile(ld.args()[0], ld.args()[1], rxi, ryi, tile, tile)
                    .unroll(ld.args()[1])
                    .tile_load(rxi, ryi);
            }
            // The mirror multiply for the output side, alongside dX's own.
            yprev.compute_at(XdXest, iw)
                .store_in(MemoryType::Tile)
                .tile(d, i, rxi, ryi, tile, tile)
                .unroll(d)
                .unroll(i)
                .tile_init(rxi, ryi);
            yprev.update()
                .tile(d, i, rxi, ryi, tile, tile)
                .split(rd_var(yprev), rro, rri, tile)
                .reorder(d, i, rro)
                .unroll(d)
                .unroll(i)
                .tile_matmul(rri, rxi, ryi);
            Func Cml2 = Func(Cm).in(Cdf2);
            Cml2.compute_at(yprev, rro)
                .store_in(MemoryType::Tile)
                .tile(Cml2.args()[0], Cml2.args()[1], rxi, ryi, tile, tile)
                .unroll(Cml2.args()[1])
                .tile_load(rxi, ryi);
            Cdf2.compute_at(yprev, rro)
                .store_in(MemoryType::Tile)
                .tile(p, i, rxi, ryi, tile, tile)
                .unroll(i)
                .tile_init(rxi, ryi);
            E2x.compute_at(XdXe, rro)
                .store_in(MemoryType::Tile)
                .bound_extent(d, tile)
                .bound_storage(d, tile)
                .tile(d, E2x.args()[1], rxi, ryi, tile, tile)
                .unroll(E2x.args()[1])
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
        // ---- The group score-gradient plane ----
        // A block owns a strip of output position tiles and sweeps its
        // share of the group's heads, computing each head's raw scores on
        // the tensor cores and accumulating the masked, decayed gradients
        // into fragments that tile the whole plane.
        {
            RVar hso("hso"), hsi("hsi");
            Var hb("hb"), tf("tf");
            const int hpg = (int)heads / (int)groups;
            sg.update().split(RVar("rsh$x"), hso, hsi, std::max(1, hpg / 8));
            Func sgp = sg.update().rfactor(hso, hb);
            Func sgpw = sgp.in();
            // The same kernel in both builds; left to itself the RDom build's
            // takes 255 registers to the inductive build's 168 and halves its
            // occupancy.
            sgpw.gpu_max_registers(168);
            sgpw.compute_root()
                .tile(j, i, rxi, ryi, tile, tile)
                .split(i, io, ii, 1)
                .fuse(t, hb, tf)
                .reorder(rxi, ryi, j, ii, io, tf, g)
                .unroll(j)
                .unroll(ii)
                .gpu_blocks(io, tf, g)
                .gpu_threads(ii)
                .tile_store(rxi, ryi);
            sgp.compute_at(sgpw, io)
                .store_in(MemoryType::Tile)
                .tile(j, i, rxi, ryi, tile, tile)
                .unroll(j)
                .gpu_threads(i)
                .tile_init(rxi, ryi);
            sgp.update()
                .tile(j, i, rxi, ryi, tile, tile)
                .reorder(j, i, hsi)
                .unroll(j)
                .gpu_threads(i)
                .tile_init(rxi, ryi);
            sg.compute_root()
                .split(j, x0, x1, 8)
                .split(i, y0, y1, 8)
                .reorder(x1, y1, x0, y0)
                .fuse(x0, y0, x0)
                .gpu_blocks(x0, t, g)
                .gpu_threads(x1, y1);
            sg.update()
                .split(j, x0, x1, 8)
                .split(i, y0, y1, 8)
                .reorder(x1, y1, hso, x0, y0)
                .fuse(x0, y0, x0)
                .gpu_blocks(x0, t, g)
                .gpu_threads(x1, y1);
            xy.compute_at(sgp, j)
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
            // The narrowed planes, in both orientations.
            for (Func f : {sgh, sg2h}) {
                f.compute_root()
                    .split(f.args()[0], x0, x1, 8)
                    .split(f.args()[1], y0, y1, 8)
                    .reorder(x1, y1, x0, y0)
                    .fuse(x0, y0, x0)
                    .gpu_blocks(x0, t, g)
                    .gpu_threads(x1, y1);
            }
        }
        // ---- The per-group state-by-position multiplies ----
        {
            RVar rcjo("rcjo"), rcji("rcji");
            Func dCaw = dCa.in();
            dCaw.compute_root()
                .tile(p, i, rxi, ryi, tile, tile)
                .split(i, io, ii, pos_tiles)
                .reorder(rxi, ryi, p, ii, io, t, g)
                .unroll(p)
                .unroll(ii)
                .gpu_blocks(io, t, g)
                .gpu_threads(ii)
                .tile_store(rxi, ryi);
            dCa.compute_at(dCaw, io)
                .store_in(MemoryType::Tile)
                .tile(p, i, rxi, ryi, tile, tile)
                .unroll(p)
                .gpu_threads(i)
                .tile_init(rxi, ryi);
            dCa.update()
                .tile(p, i, rxi, ryi, tile, tile)
                .split(RVar("rch$x"), rcjo, rcji, tile)
                .reorder(p, rcjo, i)
                .unroll(p)
                .gpu_threads(i)
                .tile_matmul(rcji, rxi, ryi);
        }
        {
            RVar rbio("rbio"), rbii("rbii");
            Func dBaw = dBa.in();
            dBaw.compute_root()
                .tile(p, j, rxi, ryi, tile, tile)
                .split(j, io, ii, pos_tiles)
                .reorder(rxi, ryi, p, ii, io, t, g)
                .unroll(p)
                .unroll(ii)
                .gpu_blocks(io, t, g)
                .gpu_threads(ii)
                .tile_store(rxi, ryi);
            dBa.compute_at(dBaw, io)
                .store_in(MemoryType::Tile)
                .tile(p, j, rxi, ryi, tile, tile)
                .unroll(p)
                .gpu_threads(j)
                .tile_init(rxi, ryi);
            dBa.update()
                .tile(p, j, rxi, ryi, tile, tile)
                .split(RVar("rbh$x"), rbio, rbii, tile)
                .reorder(p, rbio, j)
                .unroll(p)
                .gpu_threads(j)
                .tile_matmul(rbii, rxi, ryi);
        }
        {
            RVar hho("hho"), hhi("hhi");
            Var hb("hb"), tf("tf");
            const int hpg = (int)heads / (int)groups;
            // The heads sweep is split into partials across blocks, like the
            // intra-chunk halves, with a small fold afterwards.
            dCb.update().split(RVar("rhh$x"), hho, hhi, std::max(1, hpg / 8));
            Func dCbp = dCb.update().rfactor(hho, hb);
            Func dCbpw = dCbp.in();
            dCbpw.compute_root()
                .tile(p, i, rxi, ryi, tile, tile)
                .split(i, io, ii, 2)
                .fuse(t, hb, tf)
                .reorder(rxi, ryi, p, ii, io, tf, g)
                .unroll(p)
                .unroll(ii)
                .gpu_blocks(io, tf, g)
                .gpu_threads(ii)
                .tile_store(rxi, ryi);
            dCbp.compute_at(dCbpw, io)
                .store_in(MemoryType::Tile)
                .tile(p, i, rxi, ryi, tile, tile)
                .unroll(p)
                .gpu_threads(i)
                .tile_init(rxi, ryi);
            dCbp.update()
                .tile(p, i, rxi, ryi, tile, tile)
                .reorder(p, i, hhi)
                .unroll(p)
                .gpu_threads(i)
                .tile_init(rxi, ryi);
            // The fold of the partials rides inside the output kernel, one
            // sum per thread, so it is not a kernel or an array of its own.
            dCb.compute_at(Func(dC), x1);
            dYH.compute_at(dCbp, hhi)
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
            RVar hho("hho"), hhi("hhi");
            Var hb("hb"), tf("tf");
            const int hpg = (int)heads / (int)groups;
            dBb.update().split(RVar("rhh$x"), hho, hhi, std::max(1, hpg / 8));
            Func dBbp = dBb.update().rfactor(hho, hb);
            Func dBbpw = dBbp.in();
            dBbpw.compute_root()
                .tile(p, j, rxi, ryi, tile, tile)
                .split(j, io, ii, 2)
                .fuse(t, hb, tf)
                .reorder(rxi, ryi, p, ii, io, tf, g)
                .unroll(p)
                .unroll(ii)
                .gpu_blocks(io, tf, g)
                .gpu_threads(ii)
                .tile_store(rxi, ryi);
            dBbp.compute_at(dBbpw, io)
                .store_in(MemoryType::Tile)
                .tile(p, j, rxi, ryi, tile, tile)
                .unroll(p)
                .gpu_threads(j)
                .tile_init(rxi, ryi);
            dBbp.update()
                .tile(p, j, rxi, ryi, tile, tile)
                .reorder(p, j, hhi)
                .unroll(p)
                .gpu_threads(j)
                .tile_init(rxi, ryi);
            dBb.compute_at(Func(dB), x1);
            XdH.compute_at(dBbp, hhi)
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
                .fuse(x0, y0, x0)
                .gpu_blocks(x0, f.args()[2], f.args()[3])
                .gpu_threads(x1, y1);
        }
        // ---- The increment gradients ----
        {
            // (1) A block owns one head's chunk and walks its output
            // position tiles; for each, the input tiles sweep by, the raw
            // scores recomputed on the tensor cores and multiplied by the
            // triangle of ones, and the masked column sums fold into the
            // per-boundary totals.
            RVar rio("rio"), rii("rii"), rjo("rjo"), rji("rji");
            // Exact extents, so the tile splits carry no tail guards.
            Sst.bound(u, 0, tile).bound(jji, 0, tile).bound(jjt, 0, L / tile).bound(t, 0, nt).bound(b, 0, heads);
            XdXest.bound(u, 0, tile).bound(j, 0, L).bound(t, 0, nt).bound(b, 0, heads);
            for (Func f : {Uh, Ih}) {
                f.compute_root().gpu_blocks(jj).gpu_threads(j);
            }
            // A block owns one head's chunk with eight warps, one per tile
            // of boundaries. The plane is staged into shared memory half of
            // its output positions at a time, four warps computing a tile
            // row each; then every warp sweeps the staged tiles for its own
            // boundaries, accumulating one tile of each sum.
            RVar rh("rh"), rqo("rqo"), rqi("rqi");
            Sst.compute_root()
                .tile(u, jji, rxi, ryi, tile, tile)
                .reorder(rxi, ryi, jjt, jji, u, t, b)
                .gpu_blocks(t, b)
                .gpu_threads(jjt)
                .tile_store(rxi, ryi);
            Sb.compute_at(Sst, jjt)
                .store_in(MemoryType::Tile)
                .tile(u, jji, rxi, ryi, tile, tile)
                .tile_init(rxi, ryi);
            SX.compute_at(Sst, u)
                .store_in(MemoryType::Tile)
                .reorder(jji, jjt)
                .gpu_threads(jjt)
                .vectorize(jji);
            SX.update()
                .split(RVar("ri2$y"), rh, rio, L / tile / 2)
                .reorder(jji, rio, jjt, rh)
                .gpu_threads(jjt)
                .tile_reduce(RVar("ri2$x"), jji);
            for (Func f : {Q, Qd, Qm}) {
                f.compute_at(SX, rio)
                    .store_in(MemoryType::Tile)
                    .tile(ii, jji, rxi, ryi, tile, tile)
                    .tile_init(rxi, ryi);
            }
            // The triangle's tiles for a warp's boundaries, and the
            // identity's one, are the same for every output tile: loaded
            // once per warp and held in registers.
            {
                Func Uhl = Uh.in(Q);
                // All eight tiles, whatever the predicate leaves of the
                // sweep, so the fragment array's shape is fixed.
                Uhl.compute_at(SX, jjt)
                    .store_in(MemoryType::Tile)
                    .bound_extent(j, L)
                    .bound_storage(j, L)
                    .bound_extent(jj, tile)
                    .bound_storage(jj, tile)
                    .tile(j, jj, rxi, ryi, tile, tile)
                    .unroll(j)
                    .tile_load(rxi, ryi);
                Func Ihl = Ih.in(Qd);
                Ihl.compute_at(SX, jjt)
                    .store_in(MemoryType::Tile)
                    .bound_extent(j, tile)
                    .bound_storage(j, tile)
                    .bound_extent(jj, tile)
                    .bound_storage(jj, tile)
                    .tile(j, jj, rxi, ryi, tile, tile)
                    .tile_load(rxi, ryi);
            }
            // The prefix's multiplies sweep the staged plane's tiles under
            // the predicate; the transpose's take one tile each.
            Q.update()
                .tile(ii, jji, rxi, ryi, tile, tile)
                // The triangle's tile comes out of a fragment, so which
                // tile each step reads has to be known here. The
                // predicate has become the sweep's loop bound; splitting
                // by the full tile count with a guarded tail gives an
                // inner loop of constant extent to unroll, each step
                // under the guard.
                .split(RVar("rq$y"), rqo, rqi, L / tile, TailStrategy::GuardWithIf)
                .unroll(rqi)
                .tile_matmul(RVar("rq$x"), rxi, ryi);
            for (int k2 = 0; k2 < 2; k2++) {
                Qd.update(k2)
                    .tile(ii, jji, rxi, ryi, tile, tile)
                    .tile_matmul(RVar("rjd$x"), rxi, ryi);
            }
            // The staged rows are padded by half a tile so that a tile's
            // rows fall on different shared memory banks.
            // All eight warps stage: two per tile row, each half its tiles.
            Var jh("jh"), jt("jt"), w("w");
            Ms.compute_at(SX, rh)
                .store_in(MemoryType::GPUShared)
                .bound_storage(j, L + tile / 2)
                .bound_extent(i, L / 2)
                .bound_storage(i, L / 2)
                .tile(j, i, rxi, ryi, tile, tile)
                .split(j, jh, jt, L / tile / 2)
                .reorder(rxi, ryi, jt, jh, i)
                .fuse(jh, i, w)
                .gpu_threads(w)
                .tile_store(rxi, ryi);
            // The scores enter the plane's fragment through a tile load.
            Func qkl3 = qk.in().in(Mq32);
            qkl3.compute_at(Ms, jt)
                .store_in(MemoryType::Tile)
                .bound_extent(j, tile)
                .bound_storage(j, tile)
                .bound_extent(i, tile)
                .bound_storage(i, tile)
                .tile(j, i, rxi, ryi, tile, tile)
                .tile_load(rxi, ryi);
            for (Func f : {Mq32, Mqhi, Mqlo}) {
                f.compute_at(Ms, jt)
                    .store_in(MemoryType::Tile)
                    .bound_extent(j, tile)
                    .bound_storage(j, tile)
                    .bound_extent(i, tile)
                    .bound_storage(i, tile)
                    .tile(j, i, rxi, ryi, tile, tile)
                    .tile_init(rxi, ryi);
            }
            xyq.compute_at(Ms, jt)
                .store_in(MemoryType::Tile)
                .bound_extent(j, tile)
                .bound_storage(j, tile)
                .bound_extent(i, tile)
                .bound_storage(i, tile)
                .tile(j, i, rxi, ryi, tile, tile)
                .tile_init(rxi, ryi);
            xyq.update()
                .tile(j, i, rxi, ryi, tile, tile)
                .split(rd_var(xyq), rro, rri, tile)
                .reorder(j, i, rro)
                .tile_matmul(rri, rxi, ryi);

            // (4) The trace pieces summed, one thread per chunk.
            cterm.compute_root().reorder(t, b).gpu_blocks(b).gpu_threads(t);
            cterm.update().reorder(RVar("rcs$x"), t, b).gpu_blocks(b).gpu_threads(t);
        }
        ga.compute_root().reorder(k, t, b).gpu_blocks(t, b).gpu_threads(k);
        {
            // The chunk's rows of stored values, staged once per block into
            // shared memory, where every thread's sum over them broadcasts.
            Func rows = XdXest.in({nx, pv});
            rows.compute_at(ga, t)
                .store_in(MemoryType::GPUShared)
                .reorder(rows.args()[1], rows.args()[0])
                .gpu_threads(rows.args()[1]);
        }
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
    void schedule_simple(Func cumdelta, Func qk, Func chunk_state,
                         Func H, Func Hop, Func dG, Func dHnr, Func dHopr,
                         Func dxi, Func dxe, Func dYH,
                         Func dCa, Func dCb,
                         Func XdH, Func dBa, Func dBb,
                         Var d, Var p, Var k, Var i, Var j, Var t, Var b,
                         Var g, Var tt, Var kk) {
        const int L = chunk;
        const int nt = (int)seq / L;

        Var x0("x0"), x1("x1"), y0("y0"), y1("y1");

        // The scans: one thread per independent walk.
        cumdelta.compute_root().reorder(k, t, b).gpu_blocks(b).gpu_threads(t);
        if (!inductive()) {
            cumdelta.update()
                .reorder(RVar("rmc$x"), t, b)
                .gpu_blocks(b)
                .gpu_threads(t);
        }
        // The within-chunk sums: a thread's own serial loop each.
        nx.compute_at(ga, k);
        pv.compute_at(ga, k);

        // The forward and backward state walks: serial over chunks, threads
        // over the state.
        for (Func f : {H, dHnr}) {
            if (inductive()) {
                f.compute_root()
                    .split(d, x0, x1, 8)
                    .split(p, y0, y1, 8)
                    .reorder(x1, y1, f.args()[2], x0, y0, b)
                    .gpu_blocks(x0, y0, b)
                    .gpu_threads(x1, y1);
            } else {
                // The base case, then the walk over the undefined state.
                f.compute_root();
                f.update(0)
                    .split(d, x0, x1, 8)
                    .split(p, y0, y1, 8)
                    .reorder(x1, y1, x0, y0, b)
                    .gpu_blocks(x0, y0, b)
                    .gpu_threads(x1, y1);
                RVar rw(f.update(1).get_schedule().rvars()[0].var);
                f.update(1)
                    .split(d, x0, x1, 8)
                    .split(p, y0, y1, 8)
                    .reorder(x1, y1, rw, x0, y0, b)
                    .gpu_blocks(x0, y0, b)
                    .gpu_threads(x1, y1);
            }
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

        // The increment gradients: plain reductions, a thread per element.
        Sst.bound(u, 0, tile).bound(jji, 0, tile).bound(jjt, 0, L / tile).bound(t, 0, nt).bound(b, 0, heads);
        XdXest.bound(u, 0, tile).bound(j, 0, L).bound(t, 0, nt).bound(b, 0, heads);
        for (Func f : {Uh, Ih}) {
            f.compute_root().gpu_blocks(jj).gpu_threads(j);
        }
        flat(xyq, j, i);
        ebd.compute_root().reorder(k, t, b).gpu_blocks(t, b).gpu_threads(k);
        for (Func f : {Q, Qd}) {
            f.compute_root().gpu_blocks(t, b).gpu_threads(ii, jji);
            for (int k2 = 0; k2 < f.num_update_definitions(); k2++) {
                f.update(k2).gpu_blocks(t, b).gpu_threads(ii, jji);
            }
        }
        flat(yprev, d, i);
        SX.compute_root().gpu_blocks(t, b).gpu_threads(jji, jjt);
        SX.update().gpu_blocks(t, b).gpu_threads(jji, jjt);
        XdXe.compute_root().reorder(j, t, b).gpu_blocks(t, b).gpu_threads(j);
        XdXe.update().reorder(rd_var(XdXe), j, t, b).gpu_blocks(t, b).gpu_threads(j);
        CTst.compute_root().reorder(j, t, b).gpu_blocks(t, b).gpu_threads(j);
        CTst.update().reorder(RVar("rcp$x"), j, t, b).gpu_blocks(t, b).gpu_threads(j);
        cterm.compute_root().reorder(t, b).gpu_blocks(b).gpu_threads(t);
        cterm.update().reorder(RVar("rcs$x"), t, b).gpu_blocks(b).gpu_threads(t);
        ga.compute_root().reorder(k, t, b).gpu_blocks(t, b).gpu_threads(k);
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
