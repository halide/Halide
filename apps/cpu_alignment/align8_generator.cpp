// Batched global alignment (Needleman-Wunsch / Gotoh) with affine gaps,
// producing the per-cell ksw2 traceback direction byte. This is the
// int8 DIFFERENCE formulation of Suzuki-Kasahara, the one ksw2's SIMD
// kernel (ksw_gg2) uses: state (U, V, X, Y) of adjacent-cell H
// differences, bounded by the scoring parameters regardless of sequence
// length, so the whole state is eight bits - twice the lanes and
// two-thirds the row bytes of the sibling align16 table. In (j, i)
// coordinates its references are only up and left (the diagonal rides in
// the differences), so it needs no anti-diagonal iteration; still
// inductive in both dims.
//
// Direction bytes mirror ksw_gg2's comparisons, which produce the same
// CIGARs as ksw_gg, so the output stays byte-exact. Three forms, matching
// align16: scan = inductive (folded two-row window) / unfolded (fused,
// no fold) / rdom (update definitions materializing the full state).

#include "Halide.h"

using namespace Halide;

namespace {

class Align8 : public Halide::Generator<Align8> {
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
        const int qe2 = 2 * gapoe;

        // int8 arithmetic throughout: Halide does no C-style promotion,
        // so these stay eight bits and wrap, matching the hand kernel's
        // vectors. The max() shields the -1 sequence access in the base
        // region, where the score is unused.
        auto score = [&](Expr jj, Expr ii) {
            return cast<int8_t>(select(qseq(b, max(jj, 0)) == tseq(b, max(ii, 0)), (int)sa, -(int)sb));
        };

        // One difference step from the four neighbouring differences.
        // z is the running max the cell's H attains; U, V are the new
        // differences to the up and left neighbours; X, Y the clamped
        // gap-open surpluses.
        auto step = [&](Expr Up, Expr Vp, Expr Xp, Expr Yp, Expr s8) {
            Expr av = Xp + Vp, bv = Yp + Up;
            Expr z = max(max(cast<int8_t>(s8 + qe2), av), bv);
            Expr zq = z - (int)gapo;
            return std::vector<Expr>{z - Vp, z - Up, max(av - zq, 0), max(bv - zq, 0)};
        };

        // ksw_gg2's direction byte, from the same intermediates: 1 if the
        // up-path strictly beats the diagonal, then 2 if the left-path
        // strictly beats that; extension bits are the pre-clamp X/Y sign.
        auto dir_byte = [&](Expr Up, Expr Vp, Expr Xp, Expr Yp, Expr s8) {
            Expr aa = Xp + Vp, bb = Yp + Up;
            Expr zz0 = cast<int8_t>(s8 + qe2);
            Expr zz1 = max(zz0, aa);
            Expr zzq = max(zz1, bb) - (int)gapo;
            Expr d = select(aa > zz0, cast<uint8_t>(1), cast<uint8_t>(0));
            d = select(bb > zz1, cast<uint8_t>(2), d);
            d = d | select(aa - zzq > 0, cast<uint8_t>(0x08), cast<uint8_t>(0));
            d = d | select(bb - zzq > 0, cast<uint8_t>(0x10), cast<uint8_t>(0));
            return d;
        };

        Func dp8(std::vector<Type>(4, Int(8)), "dp8");

        if (scan != ScanForm::RDom) {
            // Boundaries from the H-difference definitions: U(-1,i) = q
            // for i > 0 else 0, V(j,-1) = q for j > 0 else 0, X = Y = 0.
            Expr Ub = cast<int8_t>(select(j < 0 && 0 < i, (int)gapo, 0));
            Expr Vb = cast<int8_t>(select(i < 0 && 0 < j, (int)gapo, 0));
            Tuple border = {Ub, Vb, cast<int8_t>(0), cast<int8_t>(0)};

            auto n = step(dp8(b, j - 1, i)[0], dp8(b, j, i - 1)[1],
                          dp8(b, j, i - 1)[2], dp8(b, j - 1, i)[3], score(j, i));
            Tuple stepT = {likely(n[0]), n[1], n[2], n[3]};
            dp8(b, j, i) = select(i < 0 || j < 0, border, stepT);

            dir(b, j, i) = dir_byte(dp8(b, j - 1, i)[0], dp8(b, j, i - 1)[1],
                                    dp8(b, j, i - 1)[2], dp8(b, j - 1, i)[3], score(j, i));
        } else {
            // Update-definition form. Storage index shifts by one so the
            // boundary lives at index zero; storage (j, i) holds original
            // cell (j-1, i-1). The nonzero boundary differences sit on the
            // two storage edges.
            Expr Ub = cast<int8_t>(select(j == 0 && i > 1, (int)gapo, 0));
            Expr Vb = cast<int8_t>(select(i == 0 && j > 1, (int)gapo, 0));
            dp8(b, j, i) = Tuple(Ub, Vb, cast<int8_t>(0), cast<int8_t>(0));

            RDom r(1, qseq.dim(1).extent(), 1, tseq.dim(1).extent(), "r");
            rj = r.x, ri = r.y;
            // Up neighbour (j-1, i) is storage (rj-1, ri); left neighbour
            // (j, i-1) is storage (rj, ri-1).
            auto n = step(dp8(b, rj - 1, ri)[0], dp8(b, rj, ri - 1)[1],
                          dp8(b, rj, ri - 1)[2], dp8(b, rj - 1, ri)[3],
                          score(rj - 1, ri - 1));
            dp8(b, rj, ri) = Tuple(n[0], n[1], n[2], n[3]);

            dir(b, j, i) = dir_byte(dp8(b, j, i + 1)[0], dp8(b, j + 1, i)[1],
                                    dp8(b, j + 1, i)[2], dp8(b, j, i + 1)[3], score(j, i));
        }

        // ---------------- Schedule ----------------

        const int VEC = 64;
        Var bo("bo"), bi("bi");
        dir.split(b, bo, bi, VEC).reorder(bi, j, i, bo).vectorize(bi);
        if (par) {
            dir.parallel(bo).stream_stores();
        }

        if (scan != ScanForm::RDom) {
            dp8.compute_at(dir, i).store_at(dir, bo).vectorize(b, VEC);
            if (scan == ScanForm::Inductive) {
                dp8.fold_storage(i, 2);
            }
            if (!par) {
                dp8.hoist_storage_root();
            }
        } else {
            dp8.compute_at(dir, bo).vectorize(b, VEC);
            dp8.update().reorder(b, rj, ri).vectorize(b, VEC);
        }
    }

private:
    RVar rj, ri;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Align8, align8)
