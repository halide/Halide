// Batched global alignment of sequence pairs with affine gap costs
// (Needleman-Wunsch / Gotoh), producing the per-cell traceback
// directions from which the runner reconstructs CIGARs. The recurrence,
// boundary values, tie-breaking, and direction-byte encoding mirror
// ksw2's ksw_gg exactly, so the output is byte-identical to the
// production kernel family used by minimap2.
//
// The DP state is a Tuple (H, E, F) inductive in BOTH the query
// position j and the target position i - the same-row reference
// H(j-1, i) makes j inductive, and every reference is non-increasing
// in each dim. The consumer wants only the direction byte per cell
// (5 bits), not the three 16-bit score planes. Written three ways:
//
// - scan=inductive: the scores live in a two-row folded window that
//   stays in cache; only the direction bytes reach memory. This is the
//   strategy alignment libraries hand-implement.
// - scan=unfolded: the same fused walk with folding disabled. Isolates
//   folding from fusion.
// - scan=rdom: the walk as an update definition, which owns its axes,
//   so all three score planes materialize at full extent before the
//   directions can be derived from them.

#include "Halide.h"

using namespace Halide;

namespace {

class Align : public Halide::Generator<Align> {
public:
    enum class ScanForm { Inductive,
                          Unfolded,
                          RDom,
                          Diff8 };
    GeneratorParam<ScanForm> scan{"scan",
                                  ScanForm::Inductive,
                                  {{"inductive", ScanForm::Inductive},
                                   {"unfolded", ScanForm::Unfolded},
                                   {"rdom", ScanForm::RDom},
                                   {"diff8", ScanForm::Diff8}}};
    GeneratorParam<bool> par{"par", false};
    // Uniform match bonus and mismatch/gap penalties (positive numbers),
    // matching a ksw2 score matrix with sa on the diagonal and -sb off it.
    GeneratorParam<int> sa{"sa", 2};
    GeneratorParam<int> sb{"sb", 4};
    GeneratorParam<int> gapo{"gapo", 4};
    GeneratorParam<int> gape{"gape", 2};

    // Sequences as 0..3 codes, batch-major so a block of pairs
    // reads its cell's characters as one dense vector: qseq(b, j).
    Input<Buffer<uint8_t, 2>> qseq{"qseq"};
    Input<Buffer<uint8_t, 2>> tseq{"tseq"};
    // ksw2's z byte per cell: bits 0-2 select the state that maximized
    // H, bits 3-4 mark E/F gap extensions.
    Output<Buffer<uint8_t, 3>> dir{"dir"};

    void generate() {
        Var b("b"), j("j"), i("i");

        const int gapoe = (int)gapo + (int)gape;
        const Expr NEG = cast<int16_t>(-(1 << 14));

        // Substitution score for cell (j, i) of pair b.
        // The max() keeps bounds inference from reaching the sequences at
        // -1 through the base-case region, where the score is unused.
        Expr s = cast<int16_t>(select(qseq(b, max(j, 0)) == tseq(b, max(i, 0)), (int)sa, -(int)sb));

        // Boundary values, exactly ksw_gg's first-row/column init:
        // H(-1,-1)=0, H(-1,j)=-(gapoe+gape*j), H(i,-1)=-(gapoe+gape*i).
        // E and F start at -inf so that E(0,j)=H(-1,j)-gapoe and
        // F(i,0)=H(i,-1)-gapoe, matching -(2*gapoe+gape*{j,i}).
        Expr Hb = cast<int16_t>(select(i < 0 && j < 0, 0,
                                       select(i < 0, -(gapoe + (int)gape * j),
                                              -(gapoe + (int)gape * i))));

        Func dp(std::vector<Type>(3, Int(16)), "dp");
        Func dp_r(std::vector<Type>(3, Int(16)), "dp_r");

        if (scan != ScanForm::RDom && scan != ScanForm::Diff8) {
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
            // Same recurrence as update definitions. Indices shift by one
            // so the boundary lives at storage index zero.
            Expr Hb1 = cast<int16_t>(select(i == 0 && j == 0, 0,
                                            select(i == 0, -(gapoe + (int)gape * (j - 1)),
                                                   -(gapoe + (int)gape * (i - 1)))));
            dp_r(b, j, i) = Tuple(Hb1, NEG, NEG);
            RDom r(1, qseq.dim(1).extent(), 1, tseq.dim(1).extent(), "r");
            rj = r.x, ri = r.y;
            Expr s1 = cast<int16_t>(select(qseq(b, rj - 1) == tseq(b, ri - 1), (int)sa, -(int)sb));
            Expr E = max(dp_r(b, rj, ri - 1)[1] - (int)gape, dp_r(b, rj, ri - 1)[0] - gapoe);
            Expr F = max(dp_r(b, rj - 1, ri)[2] - (int)gape, dp_r(b, rj - 1, ri)[0] - gapoe);
            Expr Hd = dp_r(b, rj - 1, ri - 1)[0] + s1;
            Expr H = max(max(Hd, E), F);
            dp_r(b, rj, ri) = Tuple(cast<int16_t>(H),
                                    cast<int16_t>(E),
                                    cast<int16_t>(F));
        }

        // The direction byte re-derives ksw_gg's comparisons from the
        // state: d = 0 if the diagonal ties-or-beats E, else 1; then 2 if
        // F strictly beats that. The extension bits use strict >.
        auto dir_byte = [&](Expr H, Expr E, Expr F, Expr Hd) {
            Expr d = select(Hd >= E, cast<uint8_t>(0), cast<uint8_t>(1));
            Expr m1 = max(Hd, E);
            d = select(m1 >= F, d, cast<uint8_t>(2));
            d = d | select(E - (int)gape > H - gapoe, cast<uint8_t>(0x08), cast<uint8_t>(0));
            d = d | select(F - (int)gape > H - gapoe, cast<uint8_t>(0x10), cast<uint8_t>(0));
            return d;
        };

        if (scan == ScanForm::Diff8) {
            // defined below, from the difference state
        } else if (scan != ScanForm::RDom) {
            Expr s0 = s;
            dir(b, j, i) = dir_byte(dp(b, j, i)[0], dp(b, j, i)[1], dp(b, j, i)[2],
                                    dp(b, j - 1, i - 1)[0] + s0);
        } else {
            Expr s0 = cast<int16_t>(select(qseq(b, j) == tseq(b, i), (int)sa, -(int)sb));
            dir(b, j, i) = dir_byte(dp_r(b, j + 1, i + 1)[0], dp_r(b, j + 1, i + 1)[1],
                                    dp_r(b, j + 1, i + 1)[2],
                                    dp_r(b, j, i)[0] + s0);
        }

        // scan=diff8: the Suzuki-Kasahara difference formulation, as in
        // ksw2_gg2: state (U, V, X, Y) where U = H(i,j)-H(i-1,j)+q+e and
        // V = H(i,j)-H(i,j-1)+q+e are bounded by the scoring parameters,
        // so the whole state is int8 - twice the lanes per vector. The
        // references are only up and left (the diagonal rides in the
        // differences), still inductive in both dims. Mirrors ksw_gg2's
        // arithmetic and direction bytes exactly (which produce the same
        // CIGARs as ksw_gg).
        Func dp8(std::vector<Type>(4, Int(8)), "dp8");
        if (scan == ScanForm::Diff8) {
            // int8 arithmetic throughout: Halide does no C-style type
            // promotion, so these stay eight bits and wrap, matching the
            // hand kernel's vectors.
            const int qe2 = 2 * gapoe;
            Expr s8 = cast<int8_t>(select(qseq(b, max(j, 0)) == tseq(b, max(i, 0)), (int)sa, -(int)sb));
            Expr Vp = dp8(b, j, i - 1)[1], Xp = dp8(b, j, i - 1)[2];
            Expr Up = dp8(b, j - 1, i)[0], Yp = dp8(b, j - 1, i)[3];
            Expr av = Xp + Vp;
            Expr bv = Yp + Up;
            Expr z0 = s8 + qe2;
            Expr z = max(max(z0, av), bv);
            Expr U = z - Vp;
            Expr V = z - Up;
            Expr zq = z - (int)gapo;
            Expr X = max(av - zq, 0);
            Expr Y = max(bv - zq, 0);
            // Boundaries from the H-difference definitions: U(i,-1) = q
            // for i > 0 else 0, V(-1,j) = q for j > 0 else 0, X = Y = 0.
            Expr Ub = cast<int8_t>(select(j < 0 && 0 < i, (int)gapo, 0));
            Expr Vb = cast<int8_t>(select(i < 0 && 0 < j, (int)gapo, 0));
            Tuple border = {Ub, Vb, cast<int8_t>(0), cast<int8_t>(0)};
            Tuple step = {likely(U), V, X, Y};
            dp8(b, j, i) = select(i < 0 || j < 0, border, step);

            // ksw_gg2's direction byte: 1 if the up-path strictly beats
            // the diagonal, then 2 if the left-path strictly beats that;
            // extension bits are the pre-clamp X/Y positivity.
            Expr aa = dp8(b, j, i - 1)[2] + dp8(b, j, i - 1)[1];
            Expr bb = dp8(b, j - 1, i)[3] + dp8(b, j - 1, i)[0];
            Expr zz0 = s8 + qe2;
            Expr zz1 = max(zz0, aa);
            Expr zz = max(zz1, bb);
            Expr zzq = zz - (int)gapo;
            Expr d = select(aa > zz0, cast<uint8_t>(1), cast<uint8_t>(0));
            d = select(bb > zz1, cast<uint8_t>(2), d);
            d = d | select(aa - zzq > 0, cast<uint8_t>(0x08), cast<uint8_t>(0));
            d = d | select(bb - zzq > 0, cast<uint8_t>(0x10), cast<uint8_t>(0));
            dir(b, j, i) = d;
        }

        // ---------------- Schedule ----------------

        const int VEC = natural_vector_size<int16_t>();
        // A parallel block owns 64 pairs - a full cache line of the
        // direction plane - so no two tasks ever write the same line.
        const int BLK = 2 * VEC;
        Var bo("bo"), bi("bi");
        dir.split(b, bo, bi, BLK).reorder(bi, j, i, bo).vectorize(bi, VEC);
        if (par) {
            // The direction plane is written once and read only by the
            // O(N) traceback: streaming stores skip the read-for-ownership
            // traffic, which is half the plane's DRAM demand.
            dir.parallel(bo).stream_stores();
        }

        if (scan == ScanForm::Diff8) {
            // The int8 state doubles the lanes: one block is one vector.
            dp8.compute_at(dir, i)
                .store_at(dir, bo)
                .fold_storage(i, 2)
                .vectorize(b, 2 * VEC);
        } else if (scan != ScanForm::RDom) {
            // One block of pairs walks the table together; the score rows
            // fold to the two the recurrence can reach.
            dp.compute_at(dir, i)
                .store_at(dir, bo)
                .vectorize(b, VEC);
            if (scan == ScanForm::Inductive) {
                dp.fold_storage(i, 2);
            }
        } else {
            dp_r.compute_at(dir, bo)
                .vectorize(b, VEC);
            dp_r.update()
                .reorder(b, rj, ri)
                .vectorize(b, VEC);
        }
    }

private:
    RVar rj, ri;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Align, align)
