// Generic, family-driven repack gemv/gemm, the matmul counterpart of the
// repack quantize_mat codecs. Like the vec_dot generators, the weight and
// activation operands are decoded through the Approximation framework
// (approximate_by + compute_offline), and the interleaved weight layout is a
// lossless relayout (UnInterleaveWeight) composed in front of the same lossy
// quant -- the col dims ride the dimension-general LinearDequant/Codebook via
// Halide::_. One generator backs every (family, n_cols, blocklen) gemv library
// (32 hand-rolled kernels -> 2 generic generators). This file covers the four
// "simple" weight families (Q4_0/Q8_0/IQ4_NL/MXFP4); the interleaved K-quant
// weights layer on top later.

#include "Halide.h"

#include "quant_components.h"

using namespace Halide;
using namespace ggml_halide;

namespace {

enum class WFamily { Q4_0,
                     Q8_0,
                     IQ4_NL,
                     MXFP4,
                     Q4_K,
                     Q5_K,
                     Q6_K,
                     Q2_K };

inline bool is_kquant(WFamily f) {
    return f == WFamily::Q4_K || f == WFamily::Q5_K || f == WFamily::Q6_K || f == WFamily::Q2_K;
}

struct WeightSpec {
    std::unique_ptr<Halide::Approximation> scheme;
    int block_bytes;
};

// Weight decode scheme + on-disk block byte width for a simple family. Byte
// widths: fp16-delta families = 2*n_cols header + payload; mxfp4 = n_cols E8M0
// header. Nibble payload = 16*n_cols, byte payload = 32*n_cols.
WeightSpec weight_spec(WFamily fam, int n_cols, int blocklen) {
    static const int8_t kIq4nl[16] = {-127, -104, -83, -65, -49, -35, -22, -10,
                                      1, 13, 25, 38, 53, 69, 89, 113};
    static const Buffer<int8_t> iq4nl_lut(const_cast<int8_t *>(kIq4nl), 16, "kvalues_iq4nl_gemv");
    static const int8_t kMxfp4[16] = {0, 1, 2, 3, 4, 6, 8, 12, 0, -1, -2, -3, -4, -6, -8, -12};
    static const Buffer<int8_t> mxfp4_lut(const_cast<int8_t *>(kMxfp4), 16, "kvalues_mxfp4_gemv");

    switch (fam) {
    case WFamily::Q4_0:
        return {make_repack_weight_scheme(n_cols, blocklen, 18 * n_cols,
                                          RepackWeightCode::SignedNibble, ScaleFormat::Fp16),
                18 * n_cols};
    case WFamily::Q8_0:
        return {make_repack_weight_scheme(n_cols, blocklen, 34 * n_cols,
                                          RepackWeightCode::SignedByte, ScaleFormat::Fp16),
                34 * n_cols};
    case WFamily::IQ4_NL:
        return {make_repack_weight_scheme(n_cols, blocklen, 18 * n_cols,
                                          RepackWeightCode::RawNibble, ScaleFormat::Fp16, iq4nl_lut),
                18 * n_cols};
    case WFamily::MXFP4:
        return {make_repack_weight_scheme(n_cols, blocklen, 17 * n_cols,
                                          RepackWeightCode::RawNibble, ScaleFormat::E8M0, mxfp4_lut),
                17 * n_cols};
    // K-quant weights (n_cols=8 always): a bespoke interleaved decode leaf.
    case WFamily::Q4_K:
        return {make_kquant_repack_weight_scheme(KQuantWeightFamily::Q4_K, blocklen, 1152), 1152};
    case WFamily::Q5_K:
        return {make_kquant_repack_weight_scheme(KQuantWeightFamily::Q5_K, blocklen, 1408), 1408};
    case WFamily::Q6_K:
        return {make_kquant_repack_weight_scheme(KQuantWeightFamily::Q6_K, blocklen, 1680), 1680};
    case WFamily::Q2_K:
        return {make_kquant_repack_weight_scheme(KQuantWeightFamily::Q2_K, blocklen, 672), 672};
    }
    _halide_internal_error << "RepackGemvGenerator: bad family\n";
    return {};
}

// gemv: one plain Q8_0 activation row x every column of a repack-interleaved
// weight matrix -> s(col-in-group, col-group).
class RepackGemvGenerator : public Generator<RepackGemvGenerator> {
public:
    GeneratorParam<WFamily> family{
        "family",
        WFamily::Q4_0,
        {{"q4_0", WFamily::Q4_0},
         {"q8_0", WFamily::Q8_0},
         {"iq4_nl", WFamily::IQ4_NL},
         {"mxfp4", WFamily::MXFP4},
         {"q4_k", WFamily::Q4_K},
         {"q5_k", WFamily::Q5_K},
         {"q6_k", WFamily::Q6_K},
         {"q2_k", WFamily::Q2_K}}};
    GeneratorParam<int> n_cols{"n_cols", 4};
    GeneratorParam<int> blocklen{"blocklen", 4};

    void configure() {
        bool kq = is_kquant(family);
        int block_size = kq ? 256 : 32;
        WeightSpec w = weight_spec(family, n_cols, blocklen);
        // Activation: plain Q8_K for K-quant weights, plain Q8_0 otherwise.
        auto act = kq ? make_q8_k_scheme(256, 127, Layout::BlockIndexed).scheme
                      : make_symmetric_block_scheme(32, 127, RoundingMode::Nearest, ScaleAnchor::AbsMax, 8, Layout::BlockIndexed).scheme;
        int act_bytes = kq ? (4 + 256 + 2 * (256 / 16)) : (2 + 32);

        ImageParam weight_blocks(UInt(8), 3, "weight_blocks");  // (byte, k-block, col-group)
        ImageParam act_blocks(UInt(8), 2, "act_blocks");        // plain Q8_0/Q8_K (byte, k-block)

        Var kk("kk"), blk("blk"), j("j"), x("x");
        Func Wt("wt_naive"), Vec("act_naive");
        Wt(kk, blk, j, x) = 0.0f;
        Vec(kk, blk) = 0.0f;

        RDom r(0, block_size, 0, weight_blocks.dim(1).extent(), "r");
        Func s("s");
        s(j, x) = 0.0f;
        s(j, x) += Wt(r.x, r.y, j, x) * Vec(r.x, r.y);

        ApproximationResult wr = Wt.approximate_by(*w.scheme, {s});
        ApproximationResult ar = Vec.approximate_by(*act, {s});
        s.inline_calls({wr.replacement, ar.replacement});

        std::vector<Func> sever = wr.encoded;
        sever.insert(sever.end(), ar.encoded.begin(), ar.encoded.end());
        std::vector<ImageParam> bind = {weight_blocks, act_blocks};
        Pipeline({s}).compute_offline(sever, bind);

        for (Func h : wr.handles) {
            if (h.has_update_definition()) {
                h.compute_root();
            }
        }
        for (Func h : ar.handles) {
            if (h.has_update_definition()) {
                h.compute_root();
            }
        }

        weight_blocks.dim(0).set_bounds(0, w.block_bytes);
        weight_blocks.dim(1).set_min(0);
        weight_blocks.dim(2).set_min(0);
        act_blocks.dim(0).set_bounds(0, act_bytes);
        act_blocks.dim(1).set_min(0);
        s.output_buffer().dim(0).set_bounds(0, n_cols);
        s.output_buffer().dim(1).set_min(0);

        add_input(weight_blocks);
        add_input(act_blocks);
        add_output(s);
    }

    void generate() {
    }
};

// gemm activation: `nr` rows packed 4-at-a-time into the SAME interleaved
// block layout the weight uses -- so it decodes through make_repack_weight_scheme
// with n_cols=4 (row group of 4), the 4 "columns" being the 4 packed rows.
// Q8_0x4 (fp16 scale, 32-block, 136 B) for simple weights; Q8_Kx4 (f32 scale,
// 256-block, 1168 B incl. dropped bsums) for K-quant weights.
struct ActSpec {
    std::unique_ptr<Halide::Approximation> scheme;
    int block_bytes;
    int block_size;
};
ActSpec act_spec(bool kquant, int blocklen) {
    if (kquant) {
        // block_q8_Kx4 is 1168 B, but the gemm reads only the f32 d[4] header
        // (16 B) + interleaved qs (1024 B) = 1040 B; the 128 B of bsums are
        // never touched, so the input is bound to 1040, not the full 1168.
        return {make_repack_weight_scheme(4, blocklen, 1040, RepackWeightCode::SignedByte,
                                          ScaleFormat::F32, {}, 256),
                1040, 256};
    }
    return {make_repack_weight_scheme(4, blocklen, 136, RepackWeightCode::SignedByte,
                                      ScaleFormat::Fp16),
            136, 32};
}

// gemm: 4 packed activation rows x every column of a repack-interleaved weight
// matrix -> s(col-in-group j, col-group x, row-in-group m, row-group y). Both
// operands are interleaved codecs decoded through the framework; the only
// difference from gemv is that the activation is interleaved too (4 rows) and
// the output gains the two activation-lane dims.
class RepackGemmGenerator : public Generator<RepackGemmGenerator> {
public:
    GeneratorParam<WFamily> family{
        "family",
        WFamily::Q4_0,
        {{"q4_0", WFamily::Q4_0},
         {"q8_0", WFamily::Q8_0},
         {"iq4_nl", WFamily::IQ4_NL},
         {"mxfp4", WFamily::MXFP4},
         {"q4_k", WFamily::Q4_K},
         {"q5_k", WFamily::Q5_K},
         {"q6_k", WFamily::Q6_K},
         {"q2_k", WFamily::Q2_K}}};
    GeneratorParam<int> n_cols{"n_cols", 4};
    GeneratorParam<int> blocklen{"blocklen", 4};

    void configure() {
        bool kq = is_kquant(family);
        int block_size = kq ? 256 : 32;
        WeightSpec w = weight_spec(family, n_cols, blocklen);
        ActSpec a = act_spec(kq, blocklen);

        ImageParam weight_blocks(UInt(8), 3, "weight_blocks");  // (byte, k-block, col-group)
        ImageParam act_blocks(UInt(8), 3, "act_blocks");        // (byte, k-block, row-group)

        Var kk("kk"), blk("blk"), j("j"), x("x"), m("m"), y("y");
        Func Wt("wt_naive"), Act("act_naive");
        Wt(kk, blk, j, x) = 0.0f;
        Act(kk, blk, m, y) = 0.0f;

        RDom r(0, block_size, 0, weight_blocks.dim(1).extent(), "r");
        Func s("s");
        s(j, x, m, y) = 0.0f;
        s(j, x, m, y) += Wt(r.x, r.y, j, x) * Act(r.x, r.y, m, y);

        ApproximationResult wr = Wt.approximate_by(*w.scheme, {s});
        ApproximationResult ar = Act.approximate_by(*a.scheme, {s});
        s.inline_calls({wr.replacement, ar.replacement});

        std::vector<Func> sever = wr.encoded;
        sever.insert(sever.end(), ar.encoded.begin(), ar.encoded.end());
        std::vector<ImageParam> bind = {weight_blocks, act_blocks};
        Pipeline({s}).compute_offline(sever, bind);

        for (Func h : wr.handles) {
            if (h.has_update_definition()) {
                h.compute_root();
            }
        }
        for (Func h : ar.handles) {
            if (h.has_update_definition()) {
                h.compute_root();
            }
        }

        weight_blocks.dim(0).set_bounds(0, w.block_bytes);
        weight_blocks.dim(1).set_min(0);
        weight_blocks.dim(2).set_min(0);
        act_blocks.dim(0).set_bounds(0, a.block_bytes);
        act_blocks.dim(1).set_min(0);
        act_blocks.dim(2).set_min(0);
        s.output_buffer().dim(0).set_bounds(0, n_cols);
        s.output_buffer().dim(1).set_min(0);
        s.output_buffer().dim(2).set_bounds(0, 4);
        s.output_buffer().dim(3).set_min(0);

        add_input(weight_blocks);
        add_input(act_blocks);
        add_output(s);
    }

    void generate() {
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(RepackGemvGenerator, repack_gemv)
HALIDE_REGISTER_GENERATOR(RepackGemmGenerator, repack_gemm)
