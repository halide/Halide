#include "Halide.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <vector>

using namespace Halide;

// Exercises Approximation composition on a small dot product.
//
// Wt is encoded as row-wise symmetric int8 values plus one scale per row.
// The encoded values are then packed into (kk, i, blk) blocks. Vec is encoded
// as symmetric int8 values with one scale for the whole vector.
//
// The test checks the raw encoded weight representation, then splices the
// Wt and Vec quantize/dequantize round trips into Acc. After inlining the
// replacement Funcs, HoistInvariantFactor should produce an Int(32)
// intermediate accumulation.

namespace {

// Symmetric int8 quantization for a 2-D Func. encode() returns q(k, i) and
// row_scale(i); decode() dequantizes q(k, i) * row_scale(i).
class SymmetricRowQuantize : public Approximation {
public:
    explicit SymmetricRowQuantize(int k) : k_(k) {
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

// Symmetric int8 quantization for a 1-D Func. encode() returns q(k) and
// vec_scale(); decode() dequantizes q(k) * vec_scale().
class SymmetricVectorQuantize : public Approximation {
public:
    explicit SymmetricVectorQuantize(int k) : k_(k) {
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
// k == blk * reduce + kk.
class BlockPack : public Approximation {
public:
    explicit BlockPack(int reduce) : reduce_(reduce) {
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

// Plain C++ reference for the symmetric int8 quantizer's encode() math.
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

// Plain C++ reference for quantizing and then dequantizing one sequence.
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

// Check the composed weight encoder's packed int8 values and per-row scales.
int check_packed_weights(const Buffer<int8_t> &packed_out, const Buffer<float> &scale_out,
                         int m, int k, int k_reduce) {
    for (int row = 0; row < m; row++) {
        std::vector<int8_t> q;
        float scale;
        reference_symmetric_quantize(k, [row](int kk) { return sinf(kk * 0.1f + row * 0.37f) * (10.0f + row); }, q, scale);

        if (std::fabs(scale_out(row) - scale) > 1e-5f * std::fabs(scale)) {
            printf("WtScale(%d) = %f, expected %f\n", row, scale_out(row), scale);
            return 1;
        }
        for (int blk = 0; blk < k / k_reduce; blk++) {
            for (int kk = 0; kk < k_reduce; kk++) {
                int kfull = blk * k_reduce + kk;
                if (packed_out(kk, row, blk) != q[kfull]) {
                    printf("WtPacked(%d,%d,%d) = %d, expected %d\n", kk, row, blk,
                           packed_out(kk, row, blk), q[kfull]);
                    return 1;
                }
            }
        }
    }
    return 0;
}

// Check Acc against the reference dot product of dequantized Wt and Vec.
int check_accumulated_result(const Buffer<float> &acc_out, int m, int k) {
    std::vector<float> vec_dequant = reference_symmetric_round_trip(k, [](int kk) {
        return cosf(kk * 0.05f) * 3.0f;
    });

    for (int row = 0; row < m; row++) {
        std::vector<float> wt_row_dequant = reference_symmetric_round_trip(k, [row](int kk) {
            return sinf(kk * 0.1f + row * 0.37f) * (10.0f + row);
        });

        float expected = 0.0f;
        for (int kk = 0; kk < k; kk++) {
            expected += wt_row_dequant[kk] * vec_dequant[kk];
        }

        if (std::fabs(acc_out(row) - expected) > 1e-3f * std::fabs(expected)) {
            printf("Acc(%d) = %f, expected %f\n", row, acc_out(row), expected);
            return 1;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    const int K = 64;       // reduction extent
    const int M = 8;        // rows
    const int kReduce = 4;  // packing block width

    Var k("k"), i("i");

    Func Wt("Wt");
    Wt(k, i) = sin(cast<float>(k) * 0.1f + cast<float>(i) * 0.37f) * (10.0f + i);

    Func Vec("Vec");
    Vec(k) = cos(cast<float>(k) * 0.05f) * 3.0f;

    // Row-quantize Wt, then pack only the quantized-value Func. The scale
    // Func passes through the composed approximation unchanged. Compose
    // owns both stages, so there's no need to keep separately-named locals
    // alive alongside it.
    Compose weight_scheme{
        Apply{/*idx=*/0, /*encode_arity=*/1, /*decode_arity=*/1, BlockPack{kReduce}},
        SymmetricRowQuantize{K},
    };

    SymmetricVectorQuantize vec_quantize(K);

    // Verify the raw encoded weight Funcs before using them through
    // approximate_by().
    EncodeResult wt_enc = weight_scheme.encode({Wt});
    if (wt_enc.encoded.size() != 2) {
        printf("Expected weight_scheme.encode() to return 2 Funcs (packed, scale), got %d\n",
               (int)wt_enc.encoded.size());
        return 1;
    }
    Func WtPacked = wt_enc.encoded[0], WtScale = wt_enc.encoded[1];
    for (Func h : wt_enc.handles) {
        h.compute_root();
    }
    Buffer<int8_t> packed_out = WtPacked.realize({kReduce, M, K / kReduce});
    Buffer<float> scale_out = WtScale.realize({M});

    if (check_packed_weights(packed_out, scale_out, M, K, kReduce)) {
        return 1;
    }

    // Build a plain fp32 reduction, then replace Wt and Vec inside Acc with
    // their quantize/dequantize round trips.
    Func Acc("Acc");
    RDom r(0, K, "r");
    Acc(i) = 0.0f;
    Acc(i) += Wt(r, i) * Vec(r);

    ApproximationResult wt_result = Wt.approximate_by(weight_scheme, {Acc});
    ApproximationResult vec_result = Vec.approximate_by(vec_quantize, {Acc});

    for (Func h : wt_result.handles) {
        h.compute_root();
    }
    for (Func h : vec_result.handles) {
        h.compute_root();
    }

    // inline_calls() exposes the decoded multiplies in Acc's update
    // expression. HoistInvariantFactor can then factor row_scale(i) and
    // vec_scale() out of the reduction; the remaining int8-by-int8 product
    // should be accumulated in an Int(32) intermediate.
    Func Acc_intm = Acc.inline_calls({wt_result.replacement, vec_result.replacement})
                        .update()
                        .rfactor({}, RFactorOptions::HoistInvariantFactor);
    if (Acc_intm.types()[0] != Int(32)) {
        std::ostringstream oss;
        oss << Acc_intm.types()[0];
        printf("Expected approximate_by()+inline_calls()+rfactor() alone to reach a genuine "
               "Int(32) accumulation, got %s\n",
               oss.str().c_str());
        return 1;
    }
    Acc_intm.compute_root();

    Buffer<float> acc_out = Acc.realize({M});

    if (check_accumulated_result(acc_out, M, K)) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
