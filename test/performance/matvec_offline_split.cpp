#include "Halide.h"
#include "halide_benchmark.h"
#include <cmath>
#include <functional>
#include <vector>

using namespace Halide;

// Starts from a naive fp32 matvec, applies Approximation-driven weight/vector
// quantization via approximate_by()/inline_calls()/rfactor's
// HoistInvariantFactor mode, then severs the quantize step out with
// Pipeline::compute_offline() before scheduling and benchmarking the rest.
// The resulting schedule and codegen should match
// test/performance/tiled_matmul_arm_neon.cpp's "Hoisted" variant, which
// starts from already-quantized ImageParams instead of deriving them from an
// Approximation -- this test exists to check that the two reach comparable
// performance.

namespace {

// Same shape as approximation_composition.cpp's SymmetricRowQuantize: one
// scale per row of a 2-D Func.
class SymmetricRowQuantize : public Approximation {
public:
    explicit SymmetricRowQuantize(int k)
        : k_(k) {
    }

    EncodeResult encode(std::vector<Func> inputs) override {
        Func w = inputs[0];
        Var k("k"), i("i");
        RDom r(0, k_, "r");

        Func amax("row_amax");
        amax(i) = 0.0f;
        amax(i) = max(amax(i), abs(w(r, i)));

        Func d("row_scale");
        d(i) = amax(i) / 127.0f;

        Func q("row_q");
        Expr id = select(d(i) != 0.0f, 1.0f / d(i), 0.0f);
        q(k, i) = cast<int8_t>(clamp(round(w(k, i) * id), -127, 127));

        return {{q, d}, {amax}};
    }

    DecodeResult decode(std::vector<Func> encoded) override {
        Func q = encoded[0], d = encoded[1];
        Var k("k"), i("i");
        Func dequantized("row_dequantized");
        dequantized(k, i) = cast<float>(q(k, i)) * d(i);
        return {{dequantized}, {}};
    }

private:
    int k_;
};

// Same shape as approximation_composition.cpp's SymmetricVectorQuantize: one
// scale for the whole 1-D Func.
class SymmetricVectorQuantize : public Approximation {
public:
    explicit SymmetricVectorQuantize(int k)
        : k_(k) {
    }

    EncodeResult encode(std::vector<Func> inputs) override {
        Func v = inputs[0];
        Var k("k");
        RDom r(0, k_, "r");

        Func amax("vec_amax");
        amax() = 0.0f;
        amax() = max(amax(), abs(v(r)));

        Func d("vec_scale");
        d() = amax() / 127.0f;

        Func q("vec_q");
        Expr id = select(d() != 0.0f, 1.0f / d(), 0.0f);
        q(k) = cast<int8_t>(clamp(round(v(k) * id), -127, 127));

        return {{q, d}, {amax}};
    }

    DecodeResult decode(std::vector<Func> encoded) override {
        Func q = encoded[0], d = encoded[1];
        Var k("k");
        Func dequantized("vec_dequantized");
        dequantized(k) = cast<float>(q(k)) * d();
        return {{dequantized}, {}};
    }

private:
    int k_;
};

// Lossless relayout from q(k, i) to packed(kk, i, blk), where
// k == blk * reduce + kk -- matches WtPacked's layout in
// tiled_matmul_arm_neon.cpp exactly.
class BlockPack : public Approximation {
public:
    explicit BlockPack(int reduce)
        : reduce_(reduce) {
    }

    EncodeResult encode(std::vector<Func> inputs) override {
        Func q = inputs[0];
        Var kk("kk"), i("i"), blk("blk");
        Func packed("packed");
        packed(kk, i, blk) = q(blk * reduce_ + kk, i);
        return {{packed}, {}};
    }

    DecodeResult decode(std::vector<Func> encoded) override {
        Func packed = encoded[0];
        Var k("k"), i("i");
        Func unpacked("unpacked");
        unpacked(k, i) = packed(k % reduce_, i, k / reduce_);
        return {{unpacked}, {}};
    }

private:
    int reduce_;
};

void reference_symmetric_quantize(int k, const std::function<float(int)> &values,
                                  std::vector<int8_t> &q, float &scale) {
    float amax = 0.0f;
    for (int kk = 0; kk < k; kk++) {
        amax = std::max(amax, std::fabs(values(kk)));
    }
    scale = amax / 127.0f;
    float id = scale != 0.0f ? 1.0f / scale : 0.0f;
    q.resize(k);
    for (int kk = 0; kk < k; kk++) {
        int v = (int)std::round(values(kk) * id);
        q[kk] = (int8_t)std::max(-127, std::min(127, v));
    }
}

std::vector<float> reference_symmetric_round_trip(int k, const std::function<float(int)> &values) {
    std::vector<int8_t> q;
    float scale;
    reference_symmetric_quantize(k, values, q, scale);
    std::vector<float> result(k);
    for (int kk = 0; kk < k; kk++) {
        result[kk] = q[kk] * scale;
    }
    return result;
}

int check_accumulated_result(const Buffer<float> &acc_out, int m, int k) {
    std::vector<float> vec_dequant = reference_symmetric_round_trip(k, [](int kk) {
        return cosf(kk * 0.05f) * 3.0f;
    });

    for (int row = 0; row < m; row++) {
        std::vector<float> wt_row_dequant = reference_symmetric_round_trip(k, [row](int kk) {
            return sinf(kk * 0.1f + row * 0.37f) * (10.0f + row);
        });

        // Tolerance is relative to the sum of the terms' magnitudes, not the
        // signed total: Wt and Vec oscillate in sign across k, so for some
        // rows the true dot product is a near-cancellation close to zero.
        // The SDOT-driven reduction accumulates those terms in a different
        // grouping than this sequential reference sum, so near a
        // cancellation the two can disagree by more than any tolerance
        // scaled by the (near-zero) signed total, even though both are
        // correct to within a couple of int8 rounding units.
        float expected = 0.0f, magnitude = 0.0f;
        for (int kk = 0; kk < k; kk++) {
            float term = wt_row_dequant[kk] * vec_dequant[kk];
            expected += term;
            magnitude += std::fabs(term);
        }

        if (std::fabs(acc_out(row) - expected) > 1e-2f * magnitude) {
            printf("Acc(%d) = %f, expected %f (magnitude %f)\n", row, acc_out(row), expected, magnitude);
            return 1;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::ARMDotProd)) {
        printf("[SKIP] This test requires ARM's SDOT instruction.\n");
        return 0;
    }

    const int M = 1024;    // output rows
    const int K = 1024;    // reduction extent
    const int reduce = 4;  // SDOT contracts 4 int8 lanes per output lane

    Var k("k"), i("i");

    Func Wt("Wt");
    Wt(k, i) = sin(cast<float>(k) * 0.1f + cast<float>(i) * 0.37f) * (10.0f + i);

    Func Vec("Vec");
    Vec(k) = cos(cast<float>(k) * 0.05f) * 3.0f;

    Compose weight_scheme{
        Apply{/*idx=*/0, /*encode_arity=*/1, /*decode_arity=*/1, BlockPack{reduce}},
        SymmetricRowQuantize{K},
    };
    SymmetricVectorQuantize vec_quantize(K);

    Var u{"u"};
    RDom r(0, K, "r");

    Func Acc("Acc");
    Acc(i) = 0.0f;
    Acc(i) += Wt(r, i) * Vec(r);

    ApproximationResult wt_result = Wt.approximate_by(weight_scheme, {Acc});
    ApproximationResult vec_result = Vec.approximate_by(vec_quantize, {Acc});
    Acc.inline_calls({wt_result.replacement, vec_result.replacement});

    ComputeOfflineResult split = Pipeline({Acc}).compute_offline({
        wt_result.handles[0],   // WtPacked
        wt_result.handles[1],   // WtScale
        vec_result.handles[0],  // VecQ
        vec_result.handles[1],  // VecScale
    });
    ImageParam &WtPacked = split.online_inputs[0];
    ImageParam &WtScale = split.online_inputs[1];
    ImageParam &VecQ = split.online_inputs[2];
    ImageParam &VecScale = split.online_inputs[3];

    // Same stride hint tiled_matmul_arm_neon.cpp needs for the same reason:
    // without it, the simplifier can't prove WtPacked's (k, i) index is a
    // single flat dense ramp, and falls back to scalar gather-and-insert
    // instead of a dense vld1q_s8 for each SDOT operand.
    WtPacked.dim(0).set_stride(1);
    WtPacked.dim(1).set_stride(reduce);

    // wt_result.handles mixes several kinds of Funcs beyond WtPacked/WtScale
    // themselves: reduction handles that must be scheduled somewhere
    // (row_amax/vec_amax, which have an update definition and can't be left
    // inline), and pure pass-through Funcs from the BlockPack decode/Compose
    // plumbing (row_q, a duplicated row_scale, "unpacked") that Halide
    // already inlines by default and should stay that way -- forcing one of
    // those to compute_root() would materialize a whole extra M*K-element
    // pass on every realize() call instead of letting it fold into the
    // vectorized reduction. Only Funcs with an update definition actually
    // need explicit scheduling here.
    for (Func &h : wt_result.handles) {
        if (h.has_update_definition()) {
            h.compute_root();
        }
    }
    for (Func &h : vec_result.handles) {
        if (h.has_update_definition()) {
            h.compute_root();
        }
    }

    // ro: which block of `reduce` elements a term belongs to; ri: its
    // position within that block -- same split tiled_matmul_arm_neon.cpp
    // performs before rfactor-ing by block.
    RVar ro{"ro"}, ri{"ri"};
    Acc.update().split(r, ro, ri, reduce);

    const int panel = target.natural_vector_size<int8_t>() * 4;

    Func Result("Result");
    Result(i) = Acc(i);

    Var io, ii;
    Result.bound(i, 0, M);
    Result.split(i, io, ii, panel).vectorize(ii, panel);

    // Identical two-step hoisting schedule to tiled_matmul_arm_neon.cpp's
    // "Hoisted" variant: first hoist WtScale(i) * VecScale (invariant across
    // the whole reduction) out entirely, leaving a pure Int(32) dot product;
    // then factor that dot product by block.
    Func Acc_wb = Acc.update().rfactor({}, RFactorOptions::HoistInvariantFactor);
    Func Acc_dot = Acc_wb.update().rfactor(ro, u);

    Acc_wb.compute_at(Result, io)
        .vectorize(i, panel)
        .update()
        .vectorize(i, panel);

    Acc_dot.compute_at(Acc_wb, ro)
        .update()
        .reorder(ri, i, u)
        .atomic()
        .vectorize(ri, reduce)
        .unroll(ri);

    Acc.compute_at(Result, io)
        .vectorize(i, panel)
        .update()
        .vectorize(i, panel);

    // Realize the offline half once, outside the timed loop, and bind its
    // output to the ImageParams the online pipeline now reads from -- from
    // here on, Result's own realize() never touches Wt/Vec's quantization
    // again.
    Buffer<int8_t> wt_packed_buf(reduce, M, K / reduce);
    Buffer<float> wt_scale_buf(M);
    Buffer<int8_t> vec_q_buf(K);
    Buffer<float> vec_scale_buf = Buffer<float>::make_scalar();
    split.offline.realize({wt_packed_buf, wt_scale_buf, vec_q_buf, vec_scale_buf});
    WtPacked.set(wt_packed_buf);
    WtScale.set(wt_scale_buf);
    VecQ.set(vec_q_buf);
    VecScale.set(vec_scale_buf);

    Buffer<float> out(M);
    Result.realize(out, target);

    if (check_accumulated_result(out, M, K)) {
        return 1;
    }

    double time = Tools::benchmark([&] {
        Result.realize(out, target);
    });

    printf("Quantized mat-vec via Approximation + compute_offline (int8 x int8 -> f32, SDOT)\n"
           "Time with hoisted rfactor: %0.4f ms\n",
           time * 1000);

    printf("Success!\n");
    return 0;
}
