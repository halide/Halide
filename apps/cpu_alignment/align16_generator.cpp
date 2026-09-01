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
    GeneratorParam<int> sa{"sa", 2};
    GeneratorParam<int> sb{"sb", 4};
    GeneratorParam<int> gapo{"gapo", 4};
    GeneratorParam<int> gape{"gape", 2};

    Input<Buffer<uint8_t, 2>> qseq{"qseq"};
    Input<Buffer<uint8_t, 2>> tseq{"tseq"};
    Output<Buffer<uint8_t, 3>> dir{"dir"};

    void generate() {
        Var b("b"), j("j"), i("i");

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

        Func dp(std::vector<Type>(3, Int(16)), "dp");

        if (scan != ScanForm::RDom) {
            // E(j,i) = max(E(j,i-1) - gape, H(j,i-1) - gapoe): a gap in
            // the query, advancing the target. F advances the query.
            Expr E = max(dp(b, j, i - 1)[1] - (int)gape, dp(b, j, i - 1)[0] - gapoe);
            Expr F = max(dp(b, j - 1, i)[2] - (int)gape, dp(b, j - 1, i)[0] - gapoe);
            Expr Hd = dp(b, j - 1, i - 1)[0] + s;
            Expr H = max(max(Hd, E), F);
            Expr border[3] = {Hb, NEG, NEG};
            Expr step[3] = {cast<int16_t>(H), cast<int16_t>(E), cast<int16_t>(F)};
            std::vector<Expr> defs;
            for (int c = 0; c < 3; c++) {
                defs.push_back(select(i < 0 || j < 0, border[c], likely(step[c])));
            }
            dp(b, j, i) = Tuple(defs);
        } else {
            // The same recurrence as update definitions. Indices shift by
            // one so the boundary lives at storage index zero.
            Expr Hb1 = cast<int16_t>(select(i == 0 && j == 0, 0,
                                            select(i == 0, -(gapoe + (int)gape * (j - 1)),
                                                   -(gapoe + (int)gape * (i - 1)))));
            dp(b, j, i) = Tuple(Hb1, NEG, NEG);
            RDom r(1, qseq.dim(1).extent(), 1, tseq.dim(1).extent(), "r");
            rj = r.x, ri = r.y;
            Expr s1 = cast<int16_t>(select(qseq(b, rj - 1) == tseq(b, ri - 1), (int)sa, -(int)sb));
            Expr E = max(dp(b, rj, ri - 1)[1] - (int)gape, dp(b, rj, ri - 1)[0] - gapoe);
            Expr F = max(dp(b, rj - 1, ri)[2] - (int)gape, dp(b, rj - 1, ri)[0] - gapoe);
            Expr Hd = dp(b, rj - 1, ri - 1)[0] + s1;
            Expr H = max(max(Hd, E), F);
            dp(b, rj, ri) = Tuple(cast<int16_t>(H), cast<int16_t>(E), cast<int16_t>(F));
        }

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
            dir(b, j, i) = dir_byte(dp(b, j, i)[0], dp(b, j, i)[1], dp(b, j, i)[2],
                                    dp(b, j - 1, i - 1)[0] + s);
        } else {
            Expr s0 = cast<int16_t>(select(qseq(b, j) == tseq(b, i), (int)sa, -(int)sb));
            dir(b, j, i) = dir_byte(dp(b, j + 1, i + 1)[0], dp(b, j + 1, i + 1)[1],
                                    dp(b, j + 1, i + 1)[2], dp(b, j, i)[0] + s0);
        }

        // ---------------- Schedule ----------------

        const int VEC = 64;
        Var bo("bo"), bi("bi");
        // A parallel block owns 64 pairs - a full cache line of the
        // direction plane - so no two tasks write the same line. The
        // wide vector also keeps several stripes of pairs in flight,
        // filling the serial per-cell chain's latency.
        dir.split(b, bo, bi, VEC).reorder(bi, j, i, bo).vectorize(bi);
        if (par) {
            // The direction plane is written once and read only by the
            // O(N) traceback: streaming stores skip read-for-ownership.
            dir.parallel(bo).stream_stores();
        }

        if (scan != ScanForm::RDom) {
            // One block of pairs walks the table together; the score rows
            // fold to the two the recurrence can reach.
            dp.compute_at(dir, i).store_at(dir, bo).vectorize(b, VEC);
            if (scan == ScanForm::Inductive) {
                dp.fold_storage(i, 2);
            }
            if (!par) {
                dp.hoist_storage_root();
            }
        } else {
            dp.compute_at(dir, bo).vectorize(b, VEC);
            dp.update().reorder(b, rj, ri).vectorize(b, VEC);
        }
    }

private:
    RVar rj, ri;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Align16, align16)
