#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;

// Quantized (int8 x int8 -> int32) mat-vec with a per-row weight scale and a
// per-vector scale, targeting ARM's SDOT instruction, scheduled via rfactor's
// distributive-law hoisting mode.
//
// WtPacked(k, i, blk) = Wt(blk*reduce+k, i): the weight matrix, repacked so
// the dimension being vectorized across (i) sits next to the reduction chunk
// (k), not behind the block index. That makes each sdot operand load a
// single contiguous 16-byte read instead of a gather across K-byte strides.
//
// Two variants are compared: Hoisted factors WtScale(i) * VecScale (the
// invariant scale product) out of the reduction before splitting it by
// block, so the per-block partial sums are a pure Int(32) dot product
// eligible for SDOT. PlainRfactor uses the same split/atomic/vectorize
// schedule but without hoisting, so the scale multiply stays inline in every
// term of the per-block reduction: the partial-sum intermediate is
// Float(32)-typed, and CodeGen_ARM's i32(widening_mul(i8, i8)) pattern can't
// match it, so no sdot is generated even though the reduction is still
// turned into a horizontal vector reduce.

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::ARMDotProd)) {
        printf("[SKIP] This test requires ARM's SDOT instruction.\n");
        return 0;
    }

    const int M = 1024;    // output rows
    const int K = 1024;    // reduction extent
    const int reduce = 4;  // SDOT contracts 4 int8 lanes per output lane

    enum { Hoisted,
           PlainRfactor,
           NumVariants };
    double times[NumVariants];
    Buffer<float> outs[NumVariants];

    for (int variant = 0; variant < NumVariants; variant++) {
        Var i{"i"}, u{"u"};
        RDom r(0, K, "r");

        ImageParam WtPacked(Int(8), 3, "WtPacked");   // WtPacked(k, i, blk) = Wt(blk*reduce+k, i)
        ImageParam WtScale(Float(32), 1, "WtScale");  // WtScale(i): per-row weight scale
        ImageParam Vec(Int(8), 1, "Vec");             // Vec(k): quantized vector
        Param<float> VecScale("VecScale");            // single scale for the vector

        // Without this, WtPacked's row stride is an opaque runtime value (an
        // ImageParam doesn't otherwise promise anything about it), so the
        // simplifier can't prove that 4 consecutive rows' data is *also*
        // contiguous with the reduction dimension -- it's forced to build each
        // sdot operand via a scalar gather-and-insert instead of a single dense
        // vld1q_s8. Declaring the true packed strides lets it prove the combined
        // (k, i) index is one flat dense ramp.
        WtPacked.dim(0).set_stride(1);
        WtPacked.dim(1).set_stride(reduce);

        Func Acc("Acc");
        Acc(i) = 0.0f;
        Acc(i) += WtScale(i) * VecScale * cast<int32_t>(widening_mul(WtPacked(r % reduce, i, r / reduce), Vec(r)));

        // ro: which block of `reduce` elements a term belongs to; ri: its
        // position within that block.
        RVar ro{"ro"}, ri{"ri"};
        Acc.update().split(r, ro, ri, reduce);

        const int panel = target.natural_vector_size<int8_t>() * 4;

        Func Result("Result");
        Result(i) = Acc(i);

        Var io, ii;
        Result.bound(i, 0, M);
        Result.split(i, io, ii, panel).vectorize(ii, panel);

        if (variant == Hoisted) {
            // Factor the reduction in two steps. First, preserving nothing
            // hoists WtScale(i) * VecScale -- invariant across all of r --
            // out of the reduction entirely: Acc_wb becomes a pure Int(32)
            // dot product over all of K, eligible for SDOT, and Acc's own
            // update collapses to a single multiply per row:
            // Acc(i) += WtScale(i) * VecScale * Acc_wb(i).
            Func Acc_wb = Acc.update().rfactor({}, RFactorOptions::HoistInvariantFactor);

            // Second, factor Acc_wb's own (now scale-free) reduction by
            // block: no hoisting needed here, just combining per-block
            // partial sums. Preserving ro turns it into a new dimension u of
            // Acc_dot, so Acc_dot(u, i) holds one block's partial dot
            // product (reduced over ri only), while Acc_wb sums
            // Acc_dot(ro, i) across blocks. ro keeps its identity across
            // both rfactor calls, so it's still the same RVar driving
            // Acc_wb's reduction loop -- usable directly below as the
            // compute_at level for Acc_dot.
            Func Acc_dot = Acc_wb.update().rfactor(ro, u);

            // Compute a whole panel of rows through every stage (Acc,
            // Acc_wb, Acc_dot) before moving to the next panel, rather than
            // fully materializing each stage before the next reads it: Acc
            // and Acc_wb are computed_at the panel loop, and Acc_dot is
            // fused one level deeper still, into Acc_wb's own per-block
            // reduction loop, so it only ever needs panel-many elements --
            // small enough to live on the stack/in registers instead of
            // being heap-allocated.
            Acc_wb.compute_at(Result, io)
                .vectorize(i, panel)
                .update()
                .vectorize(i, panel);

            // Acc_dot's row dimension (i) is deliberately left unvectorized:
            // fused this deep inside Acc_wb's own per-block reduction loop,
            // vectorizing it makes the vectorizer conflate the two loops and
            // build a wildly oversized (and out-of-bounds) vector
            // expression. The sdot reduction itself is still
            // atomic-vectorized over ri, so this costs nothing.
            Acc_dot.compute_at(Acc_wb, ro)
                .update()
                .reorder(ri, i, u)
                .atomic()
                .vectorize(ri, reduce)
                .unroll(ri);
        } else {
            // No invariant factor to hoist out this time, so a single
            // rfactor preserving ro is enough: Acc_dot(u, i) holds one
            // block's partial sum of WtScale(i) * VecScale * term(ri, i)
            // (Float(32), since the scale multiply is still inline), and
            // Acc sums Acc_dot(ro, i) across blocks directly -- there's no
            // separate write-back stage, since Acc's own combine was never
            // factored out.
            Func Acc_dot = Acc.update().rfactor(ro, u, RFactorOptions::None);

            Acc_dot.compute_at(Acc, ro)
                .update()
                .reorder(ri, i, u)
                .atomic()
                .vectorize(ri, reduce)
                .unroll(ri);
        }

        Acc.compute_at(Result, io)
            .vectorize(i, panel)
            .update()
            .vectorize(i, panel);

        Buffer<int8_t> wt_packed(reduce, M, K / reduce);
        for (int blk = 0; blk < K / reduce; blk++) {
            for (int y = 0; y < M; y++) {
                for (int k = 0; k < reduce; k++) {
                    wt_packed(k, y, blk) = (int8_t)((((reduce * blk + k) + y) % 15) - 7);
                }
            }
        }

        Buffer<float> wt_scale_buf(M);
        for (int y = 0; y < M; y++) {
            wt_scale_buf(y) = 0.01f * ((y % 7) + 1);
        }

        Buffer<int8_t> vec_buf(K);
        for (int x = 0; x < K; x++) {
            vec_buf(x) = (int8_t)((x % 13) - 6);
        }

        WtPacked.set(wt_packed);
        WtScale.set(wt_scale_buf);
        Vec.set(vec_buf);
        VecScale.set(0.5f);

        Buffer<float> out(M);
        Result.realize(out, target);
        outs[variant] = out;

        times[variant] = Tools::benchmark([&] {
            Result.realize(out, target);
        });
    }

    for (int y = 0; y < M; y++) {
        float ref = outs[PlainRfactor](y);
        if (std::abs(ref - outs[Hoisted](y)) > 1e-3f * std::abs(ref)) {
            printf("Quantized mat-vec mismatch at %d: %f (plain) vs %f (hoisted)\n", y, ref, outs[Hoisted](y));
            return 1;
        }
    }

    printf("Quantized mat-vec (int8 x int8 -> f32, SDOT)\n"
           "Time with non-hoisting rfactor:  %0.4f ms\n"
           "Time with hoisted rfactor:       %0.4f ms (%0.2fx vs non-hoisting)\n",
           times[PlainRfactor] * 1000,
           times[Hoisted] * 1000,
           times[PlainRfactor] / times[Hoisted]);

    printf("Success!\n");
    return 0;
}
