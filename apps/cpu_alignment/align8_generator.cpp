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
#include "align_traceback.h"

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

        // The four differences and the direction byte, computed from the
        // same intermediates in one pass.
        Func dp8({Int(8), Int(8), Int(8), Int(8), UInt(8)}, "dp8");

        if (scan != ScanForm::RDom) {
            // Boundaries from the H-difference definitions: U(-1,i) = q
            // for i > 0 else 0, V(j,-1) = q for j > 0 else 0, X = Y = 0.
            Expr Ub = cast<int8_t>(select(j < 0 && 0 < i, (int)gapo, 0));
            Expr Vb = cast<int8_t>(select(i < 0 && 0 < j, (int)gapo, 0));
            Tuple border = {Ub, Vb, cast<int8_t>(0), cast<int8_t>(0), cast<uint8_t>(0)};

            Expr Up = dp8(b, j - 1, i)[0], Vp = dp8(b, j, i - 1)[1];
            Expr Xp = dp8(b, j, i - 1)[2], Yp = dp8(b, j - 1, i)[3];
            auto n = step(Up, Vp, Xp, Yp, score(j, i));
            // likely on every element, so the border select partitions
            // the loops rather than being evaluated per cell.
            Tuple stepT = {likely(n[0]), likely(n[1]), likely(n[2]), likely(n[3]),
                           likely(dir_byte(Up, Vp, Xp, Yp, score(j, i)))};
            dp8(b, j, i) = select(i < 0 || j < 0, border, stepT);

            // Two cells per byte of the direction plane: half the bytes the
            // fill streams out, which is what bounds it across cores.
            Expr J = qseq.dim(1).extent();
            dir(b, j, i) = align_tb::pack_dir(dp8(b, 2 * j, i)[4]) |
                           (align_tb::pack_dir(dp8(b, min(2 * j + 1, J - 1), i)[4]) << 4);
        } else {
            // Update-definition form. Storage index shifts by one so the
            // boundary lives at index zero; storage (j, i) holds original
            // cell (j-1, i-1). The nonzero boundary differences sit on the
            // two storage edges.
            Expr Ub = cast<int8_t>(select(j == 0 && i > 1, (int)gapo, 0));
            Expr Vb = cast<int8_t>(select(i == 0 && j > 1, (int)gapo, 0));
            dp8(b, j, i) = Tuple(Ub, Vb, cast<int8_t>(0), cast<int8_t>(0), cast<uint8_t>(0));

            RDom r(1, qseq.dim(1).extent(), 1, tseq.dim(1).extent(), "r");
            rj = r.x, ri = r.y;
            // Up neighbour (j-1, i) is storage (rj-1, ri); left neighbour
            // (j, i-1) is storage (rj, ri-1).
            Expr Up = dp8(b, rj - 1, ri)[0], Vp = dp8(b, rj, ri - 1)[1];
            Expr Xp = dp8(b, rj, ri - 1)[2], Yp = dp8(b, rj - 1, ri)[3];
            auto n = step(Up, Vp, Xp, Yp, score(rj - 1, ri - 1));
            dp8(b, rj, ri) = Tuple(n[0], n[1], n[2], n[3],
                                   dir_byte(Up, Vp, Xp, Yp, score(rj - 1, ri - 1)));

            // The walk gathers straight from the direction plane.
            dir(b, j, i) = dp8(b, j + 1, i + 1)[4];
        }

        // The walk: inductive for the inductive forms, an update
        // definition for the fully-RDom ablation.
        Expr J = qseq.dim(1).extent(), I = tseq.dim(1).extent();
        Func tb;
        RVar rs;
        if (scan != ScanForm::RDom) {
            tb = align_traceback(dir, true, J, I, b, ps);
            path(b, ps) = tb(b, ps)[3];
        } else {
            TracebackRDom t = align_traceback_rdom(dir, false, J, I, J + I, b, ps);
            tb = t.tb;
            rs = t.rs;
            path(b, ps) = tb(b, ps + 1)[3];
        }

        // ---------------- Schedule ----------------

        const int VEC = 64;
        Var bo("bo"), bi("bi");
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
            dp8.compute_at(dir, i).store_at(path, bo).vectorize(b, VEC);
            if (scan == ScanForm::Inductive) {
                dp8.fold_storage(i, 2);
            } else {
                // Genuinely unfolded: an explicit fold factor of the whole
                // table gives every row its own slot (the modulo never
                // wraps) and overrides the automatic folding pass, which
                // would otherwise fold this form anyway and leave the
                // ablation measuring nothing. bound_storage alone only
                // inflates the allocation; the indexing still folds.
                dp8.fold_storage(i, tseq.dim(1).extent() + 1);
            }
            if (!par) {
                dp8.hoist_storage_root();
            }
        } else {
            dp8.compute_at(path, bo).vectorize(b, VEC);
            dp8.update().reorder(b, rj, ri).vectorize(b, VEC);
        }
    }

private:
    RVar rj, ri;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Align8, align8)
