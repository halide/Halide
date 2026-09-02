// Batched global alignment (Needleman-Wunsch / Gotoh) with affine gaps,
// producing the per-cell ksw2 traceback direction byte. This is the
// int16 ABSOLUTE-SCORE table: state (H, E, F) carried as signed 16-bit
// scores, the textbook recurrence. The state is inductive in BOTH table
// dimensions - the same-row reference H(j-1, i) forces j inductive, and
// every reference is non-increasing in each dim. The consumer wants only
// the direction byte (5 bits), not the three score planes.
//
// Recurrence, boundary values, tie-breaking, and direction bytes mirror
// ksw2's ksw_gg exactly, so the output is byte-identical. Three forms:
//
// - scan=inductive: scores live in a two-row folded window in cache;
//   only direction bytes reach memory.
// - scan=unfolded: the same fused walk, folding disabled. Isolates
//   folding from fusion.
// - scan=rdom: the walk as update definitions, which own their axes, so
//   all three planes materialize at full extent before the directions
//   can be read from them.
//
// The int8 difference formulation (twice the lanes) lives in the sibling
// align8 generator.

#include "Halide.h"
#include "align_traceback.h"

using namespace Halide;

namespace {

class Align16 : public Halide::Generator<Align16> {
public:
    enum class ScanForm { Inductive,
                          Unfolded,
                          RDom };
    GeneratorParam<ScanForm> scan{"scan",
                                  ScanForm::Inductive,
                                  {{"inductive", ScanForm::Inductive},
                                   {"unfolded", ScanForm::Unfolded},
                                   {"rdom", ScanForm::RDom}}};
    GeneratorParam<bool> par{"par", false};
    // Streaming stores for the direction plane under par (off only to
    // test cache-resident planes).
    GeneratorParam<bool> stream{"stream", true};
    GeneratorParam<int> sa{"sa", 2};
    GeneratorParam<int> sb{"sb", 4};
    GeneratorParam<int> gapo{"gapo", 4};
    GeneratorParam<int> gape{"gape", 2};

    Input<Buffer<uint8_t, 2>> qseq{"qseq"};
    Input<Buffer<uint8_t, 2>> tseq{"tseq"};
    // The traceback op stream: path(b, s) for query+target length steps,
    // backward order, 3 once a pair is done. The direction plane is an
    // intermediate realized one block of pairs at a time.
    Output<Buffer<uint8_t, 2>> path{"path"};

    void generate() {
        Var b("b"), j("j"), i("i"), ps("ps");
        Func dir("dir");

        const int gapoe = (int)gapo + (int)gape;
        const Expr NEG = cast<int16_t>(-(1 << 14));

        // The max() keeps bounds inference from reaching the sequences at
        // -1 through the base-case region, where the score is unused.
        Expr s = cast<int16_t>(select(qseq(b, max(j, 0)) == tseq(b, max(i, 0)), (int)sa, -(int)sb));

        // ksw_gg's first-row/column init: H(-1,-1)=0,
        // H(-1,j)=-(gapoe+gape*j), H(i,-1)=-(gapoe+gape*i). E, F start at
        // -inf so E(0,j)=H(-1,j)-gapoe and F(i,0)=H(i,-1)-gapoe.
        Expr Hb = cast<int16_t>(select(i < 0 && j < 0, 0,
                                       select(i < 0, -(gapoe + (int)gape * j),
                                              -(gapoe + (int)gape * i))));

        // H, E, F and the direction byte, computed from the same
        // intermediates in one pass.
        Func dp({Int(16), Int(16), Int(16), UInt(8)}, "dp");

        // ksw_gg's direction byte: d = 0 if the diagonal ties-or-beats E,
        // else 1; then 2 if F strictly beats that; extension bits use
        // strict >.
        auto dir_byte = [&](Expr H, Expr E, Expr F, Expr Hd) {
            Expr d = select(Hd >= E, cast<uint8_t>(0), cast<uint8_t>(1));
            Expr m1 = max(Hd, E);
            d = select(m1 >= F, d, cast<uint8_t>(2));
            d = d | select(E - (int)gape > H - gapoe, cast<uint8_t>(0x08), cast<uint8_t>(0));
            d = d | select(F - (int)gape > H - gapoe, cast<uint8_t>(0x10), cast<uint8_t>(0));
            return d;
        };

        if (scan != ScanForm::RDom) {
            // E(j,i) = max(E(j,i-1) - gape, H(j,i-1) - gapoe): a gap in
            // the query, advancing the target. F advances the query.
            Expr E = max(dp(b, j, i - 1)[1] - (int)gape, dp(b, j, i - 1)[0] - gapoe);
            Expr F = max(dp(b, j - 1, i)[2] - (int)gape, dp(b, j - 1, i)[0] - gapoe);
            Expr Hd = dp(b, j - 1, i - 1)[0] + s;
            Expr H = max(max(Hd, E), F);
            Expr border[4] = {Hb, NEG, NEG, cast<uint8_t>(0)};
            Expr step[4] = {cast<int16_t>(H), cast<int16_t>(E), cast<int16_t>(F),
                            dir_byte(cast<int16_t>(H), cast<int16_t>(E), cast<int16_t>(F), Hd)};
            std::vector<Expr> defs;
            for (int c = 0; c < 4; c++) {
                defs.push_back(select(i < 0 || j < 0, border[c], likely(step[c])));
            }
            dp(b, j, i) = Tuple(defs);
            dir(b, j, i) = dp(b, j, i)[3];
        } else {
            // The same recurrence as update definitions. Indices shift by
            // one so the boundary lives at storage index zero.
            Expr Hb1 = cast<int16_t>(select(i == 0 && j == 0, 0,
                                            select(i == 0, -(gapoe + (int)gape * (j - 1)),
                                                   -(gapoe + (int)gape * (i - 1)))));
            dp(b, j, i) = Tuple(Hb1, NEG, NEG, cast<uint8_t>(0));
            RDom r(1, qseq.dim(1).extent(), 1, tseq.dim(1).extent(), "r");
            rj = r.x, ri = r.y;
            Expr s1 = cast<int16_t>(select(qseq(b, rj - 1) == tseq(b, ri - 1), (int)sa, -(int)sb));
            Expr E = max(dp(b, rj, ri - 1)[1] - (int)gape, dp(b, rj, ri - 1)[0] - gapoe);
            Expr F = max(dp(b, rj - 1, ri)[2] - (int)gape, dp(b, rj - 1, ri)[0] - gapoe);
            Expr Hd = dp(b, rj - 1, ri - 1)[0] + s1;
            Expr H = max(max(Hd, E), F);
            dp(b, rj, ri) = Tuple(cast<int16_t>(H), cast<int16_t>(E), cast<int16_t>(F),
                                  dir_byte(cast<int16_t>(H), cast<int16_t>(E), cast<int16_t>(F), Hd));
            // The walk gathers straight from the direction plane.
            dir(b, j, i) = dp(b, j + 1, i + 1)[3];
        }


        // The walk: inductive for the inductive forms, an update
        // definition for the fully-RDom ablation.
        Expr J = qseq.dim(1).extent(), I = tseq.dim(1).extent();
        Func tb;
        RVar rs;
        if (scan != ScanForm::RDom) {
            tb = align_traceback(dir, J, I, b, ps);
            path(b, ps) = tb(b, ps)[3];
        } else {
            TracebackRDom t = align_traceback_rdom(dir, J, I, J + I, b, ps);
            tb = t.tb;
            rs = t.rs;
            path(b, ps) = tb(b, ps + 1)[3];
        }

        // ---------------- Schedule ----------------

        const int VEC = 64;
        Var bo("bo"), bi("bi");
        // A parallel block owns 64 pairs - a full cache line of the
        // direction plane - so no two tasks write the same line. The
        // wide vector also keeps several stripes of pairs in flight,
        // filling the serial per-cell chain's latency.
        // One block of 64 pairs is one task: realize its direction plane,
        // then walk it while it is the freshest thing in cache.
        path.split(b, bo, bi, VEC).reorder(bi, ps, bo).vectorize(bi);
        if (par) {
            path.parallel(bo);
        }
        if (scan != ScanForm::RDom) {
            tb.compute_at(path, ps).store_at(path, bo).fold_storage(ps, 2).vectorize(b, VEC);
        } else {
            tb.compute_at(path, bo).vectorize(b, VEC);
            tb.update().reorder(b, rs).vectorize(b, VEC);
        }
        if (scan != ScanForm::RDom) {
            // The direction plane is the state's last element, copied out
            // row by row as the window slides.
            dir.compute_at(path, bo).store_at(path, bo).reorder(b, j, i).vectorize(b, VEC);
            if (par && stream) {
                // Written once per block and read back at 2N of its N^2
                // cells: streaming stores skip read-for-ownership.
                dir.stream_stores();
            }
        }

        if (scan != ScanForm::RDom) {
            // One block of pairs walks the table together; the score rows
            // fold to the two the recurrence can reach.
            dp.compute_at(dir, i).store_at(path, bo).vectorize(b, VEC);
            if (scan == ScanForm::Inductive) {
                dp.fold_storage(i, 2);
            } else {
                // Genuinely unfolded: an explicit fold factor of the whole
                // table gives every row its own slot (the modulo never
                // wraps) and overrides the automatic folding pass, which
                // would otherwise fold this form anyway and leave the
                // ablation measuring nothing. bound_storage alone only
                // inflates the allocation; the indexing still folds.
                dp.fold_storage(i, tseq.dim(1).extent() + 1);
            }
            if (!par) {
                dp.hoist_storage_root();
            }
        } else {
            dp.compute_at(path, bo).vectorize(b, VEC);
            dp.update().reorder(b, rj, ri).vectorize(b, VEC);
        }
    }

private:
    RVar rj, ri;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Align16, align16)
