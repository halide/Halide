// From-scratch Halide reimplementation of GGML's "repack" quantize_mat
// kernels (see src/ggml-cpu/repack.cpp: ggml_quantize_mat_q8_0_4x4_generic /
// ggml_quantize_mat_q8_0_4x8_generic / ggml_quantize_mat_q8_K_4x4_generic /
// ggml_quantize_mat_q8_K_4x8_generic upstream, as of GGML v0.15.3). These
// take 4 contiguous rows of `k` floats (row r at x[r*k .. r*k+k)) and
// interleave them into ONE activation-format block per `k`-sized chunk,
// where 4 per-row values are laid out consecutively (in groups of
// `blck_size_interleave`) instead of one row's worth at a time -- this is
// the packed activation format the corresponding repack_gemv/repack_gemm
// kernels consume. There are only 4 distinct interleavings (2 activation
// formats x 2 interleave widths), reused across every repack weight type
// that shares that (activation, interleave) pair -- see k_repack_entries in
// providers/ggml_provider.cpp and this file's registration in
// halide_provider.cpp.
//
// block_q8_0x4 layout (136 bytes, one per 32-element chunk x 4 rows):
//   byte 0-7:    4 fp16 deltas, one per row, in row order
//   byte 8-135:  128 signed int8 quants, interleaved in groups of
//                `blck_size_interleave` (4 or 8) per row
//
// block_q8_Kx4 layout (1168 bytes, one per 256-element chunk x 4 rows):
//   byte 0-15:    4 float32 deltas, one per row, in row order
//   byte 16-1039: 1024 signed int8 quants, interleaved the same way
//   byte 1040-1167: 64 signed int16 "bsums" (sum of quants in groups of 16,
//                  scattered across rows/groups by the same index mapping
//                  GGML uses -- see index_q8_k below)
//
// Q8_0's per-row scale is the same amax/127 symmetric scale as plain Q8_0
// (see q8_0_generators.cpp), using round() (roundf, not round-to-even).
// Q8_K's per-row scale is the same -127/max signed scale as plain Q8_K (see
// q8_k_generators.cpp), using the same round-to-nearest-even magic-number
// trick -- but unlike plain Q8_K's quantize_row, this repack version has no
// final MIN(127, ...) clamp (safe here since the scale is derived from this
// exact block's own amax, so values can never exceed +-127 already).
//
// This is intentionally unscheduled beyond the minimum Halide requires for
// legality (an update-defined Func can't stay inline) -- scheduling for
// performance is a later step.

#include "Halide.h"

using namespace Halide;

namespace {

constexpr int kQK8_0 = 32;
constexpr int kBlockBytesQ8_0x4 = 4 * 2 + kQK8_0 * 4;  // 136

constexpr int kQK_K = 256;
constexpr int kNumGroups = kQK_K / 16;                                     // 16
constexpr int kBlockBytesQ8_Kx4 = 4 * 4 + kQK_K * 4 + kNumGroups * 4 * 2;  // 1168

// Same magic-number round-to-nearest-even trick as q8_k_generators.cpp's
// nearest_int() (mirrors GGML's static inline nearest_int() in
// src/ggml-quants.c).
Expr nearest_int(Expr fval) {
    Expr val = fval + 12582912.0f;  // 1.5 * 2^23
    Expr bits = reinterpret<int32_t>(val);
    return (bits & 0x007fffff) - 0x00400000;
}

class Q8_0_4x4QuantizeMatGenerator : public Generator<Q8_0_4x4QuantizeMatGenerator> {
public:
    // dim 0: column-within-row (extent k, a multiple of kQK8_0), dim 1: row (extent 4).
    Input<Buffer<float, 2>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytesQ8_0x4), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        constexpr int kBlk = 4;
        Var row("row"), i("i"), j("j"), byte("byte");
        RDom r(0, kQK8_0, "r");

        Func amax_f("amax_f");
        amax_f(row, i) = 0.0f;
        amax_f(row, i) = max(amax_f(row, i), abs(x_(i * kQK8_0 + r, row)));
        amax_f.compute_root();

        Func scale("scale");
        Expr d = amax_f(row, i) / 127.0f;
        Expr id = select(d != 0.0f, 1.0f / d, 0.0f);
        scale(row, i) = Tuple(d, id);
        scale.compute_root();

        Func qval("qval");  // 128 payload bytes per block, j in [0, 128)
        Expr src_id = (j % (4 * kBlk)) / kBlk;
        Expr src_offset = (j / (4 * kBlk)) * kBlk + (j % kBlk);
        Expr id_ = scale(src_id, i)[1];
        qval(j, i) = cast<int8_t>(round(x_(i * kQK8_0 + src_offset, src_id) * id_));
        qval.compute_root();

        Expr d_row = clamp(byte / 2, 0, 3);
        Expr d_bits = reinterpret<uint16_t>(cast<float16_t>(scale(d_row, i)[0]));
        Expr d_byte = select((byte % 2) == 0, cast<uint8_t>(d_bits & 0xff), cast<uint8_t>((d_bits >> 8) & 0xff));

        Expr qs_byte = reinterpret<uint8_t>(qval(clamp(byte - 8, 0, kQK8_0 * 4 - 1), i));

        blocks_(byte, i) = select(byte < 8, d_byte, qs_byte);

        x_.dim(0).set_min(0);
        x_.dim(1).set_bounds(0, 4);
        blocks_.dim(0).set_bounds(0, kBlockBytesQ8_0x4);
        blocks_.dim(1).set_min(0);
    }
};

class Q8_0_4x8QuantizeMatGenerator : public Generator<Q8_0_4x8QuantizeMatGenerator> {
public:
    Input<Buffer<float, 2>> x_{"x"};
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        constexpr int kBlk = 8;
        Var row("row"), i("i"), j("j"), byte("byte");
        RDom r(0, kQK8_0, "r");

        Func amax_f("amax_f");
        amax_f(row, i) = 0.0f;
        amax_f(row, i) = max(amax_f(row, i), abs(x_(i * kQK8_0 + r, row)));
        amax_f.compute_root();

        Func scale("scale");
        Expr d = amax_f(row, i) / 127.0f;
        Expr id = select(d != 0.0f, 1.0f / d, 0.0f);
        scale(row, i) = Tuple(d, id);
        scale.compute_root();

        Func qval("qval");
        Expr src_id = (j % (4 * kBlk)) / kBlk;
        Expr src_offset = (j / (4 * kBlk)) * kBlk + (j % kBlk);
        Expr id_ = scale(src_id, i)[1];
        qval(j, i) = cast<int8_t>(round(x_(i * kQK8_0 + src_offset, src_id) * id_));
        qval.compute_root();

        Expr d_row = clamp(byte / 2, 0, 3);
        Expr d_bits = reinterpret<uint16_t>(cast<float16_t>(scale(d_row, i)[0]));
        Expr d_byte = select((byte % 2) == 0, cast<uint8_t>(d_bits & 0xff), cast<uint8_t>((d_bits >> 8) & 0xff));

        Expr qs_byte = reinterpret<uint8_t>(qval(clamp(byte - 8, 0, kQK8_0 * 4 - 1), i));

        blocks_(byte, i) = select(byte < 8, d_byte, qs_byte);

        x_.dim(0).set_min(0);
        x_.dim(1).set_bounds(0, 4);
        blocks_.dim(0).set_bounds(0, kBlockBytesQ8_0x4);
        blocks_.dim(1).set_min(0);
    }
};

class Q8_K_4x4QuantizeMatGenerator : public Generator<Q8_K_4x4QuantizeMatGenerator> {
public:
    Input<Buffer<float, 2>> x_{"x"};
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        constexpr int kBlk = 4;
        Var row("row"), i("i"), j("j"), byte("byte");
        RDom r(0, kQK_K, "r");

        // Per-(row,block) signed-max reduction (same "argmax"-style idiom as
        // q8_k_generators.cpp's stat Func).
        Func stat("stat");
        stat(row, i) = Tuple(0.0f, 0.0f);  // {amax, max}
        Expr v = x_(i * kQK_K + r, row);
        Expr take = abs(v) > stat(row, i)[0];
        stat(row, i) = Tuple(select(take, abs(v), stat(row, i)[0]), select(take, v, stat(row, i)[1]));
        stat.compute_root();

        Expr is_zero = stat(row, i)[0] == 0.0f;
        Expr max_val = stat(row, i)[1];
        Expr iscale_e = select(is_zero, 0.0f, -127.0f / max_val);
        Expr d_e = select(is_zero, 0.0f, 1.0f / iscale_e);

        Func scale("scale");
        scale(row, i) = Tuple(d_e, iscale_e);
        scale.compute_root();

        Func qval("qval");  // 1024 payload values per block, j in [0, 1024)
        Expr src_id = (j % (4 * kBlk)) / kBlk;
        Expr src_offset = (j / (4 * kBlk)) * kBlk + (j % kBlk);
        Expr iscale_ = scale(src_id, i)[1];
        qval(j, i) = nearest_int(x_(i * kQK_K + src_offset, src_id) * iscale_);
        qval.compute_root();

        // GGML's index mapping from interleaved payload index j to the
        // corresponding bsums slot (see ggml_quantize_mat_q8_K_4x4_generic:
        // "Bsums values are interleaved in sequence of four bsums from each
        // super block taken for interleaving"). Expressed in terms of the
        // RDom below (rj), the variable this scatter update actually
        // iterates over -- not the qval Func's own arg Var (j).
        Func bsums("bsums");
        Var idx("idx");
        RDom rj(0, kQK_K * 4, "rj");
        Expr index_q8_k = (((rj & (4 * kBlk - 1)) >> 2) << 2) + ((rj >> 8) << 4) + ((rj >> 6) & 3);
        bsums(idx, i) = 0;
        bsums(index_q8_k, i) += qval(rj, i);
        bsums.compute_root();

        Expr d_row = clamp(byte / 4, 0, 3);
        Expr d_byte_in_row = clamp(byte % 4, 0, 3);
        Expr d_bits = reinterpret<uint32_t>(scale(d_row, i)[0]);
        Expr d_byte = cast<uint8_t>((d_bits >> (cast<uint32_t>(d_byte_in_row) * 8)) & 0xff);

        Expr qs_byte = reinterpret<uint8_t>(cast<int8_t>(qval(clamp(byte - 16, 0, kQK_K * 4 - 1), i)));

        Expr bsums_rel = clamp(byte - (16 + kQK_K * 4), 0, kNumGroups * 4 * 2 - 1);
        Expr bsums_group = bsums_rel / 2;
        Expr bsums_is_lo = (bsums_rel % 2) == 0;
        Expr bsums_bits = reinterpret<uint16_t>(cast<int16_t>(bsums(bsums_group, i)));
        Expr bsums_byte = cast<uint8_t>(select(bsums_is_lo, bsums_bits & 0xff, (bsums_bits >> 8) & 0xff));

        blocks_(byte, i) =
            select(byte < 16, d_byte, byte < 16 + kQK_K * 4, qs_byte, bsums_byte);

        x_.dim(0).set_min(0);
        x_.dim(1).set_bounds(0, 4);
        blocks_.dim(0).set_bounds(0, kBlockBytesQ8_Kx4);
        blocks_.dim(1).set_min(0);
    }
};

class Q8_K_4x8QuantizeMatGenerator : public Generator<Q8_K_4x8QuantizeMatGenerator> {
public:
    Input<Buffer<float, 2>> x_{"x"};
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        constexpr int kBlk = 8;
        Var row("row"), i("i"), j("j"), byte("byte");
        RDom r(0, kQK_K, "r");

        Func stat("stat");
        stat(row, i) = Tuple(0.0f, 0.0f);
        Expr v = x_(i * kQK_K + r, row);
        Expr take = abs(v) > stat(row, i)[0];
        stat(row, i) = Tuple(select(take, abs(v), stat(row, i)[0]), select(take, v, stat(row, i)[1]));
        stat.compute_root();

        Expr is_zero = stat(row, i)[0] == 0.0f;
        Expr max_val = stat(row, i)[1];
        Expr iscale_e = select(is_zero, 0.0f, -127.0f / max_val);
        Expr d_e = select(is_zero, 0.0f, 1.0f / iscale_e);

        Func scale("scale");
        scale(row, i) = Tuple(d_e, iscale_e);
        scale.compute_root();

        Func qval("qval");
        Expr src_id = (j % (4 * kBlk)) / kBlk;
        Expr src_offset = (j / (4 * kBlk)) * kBlk + (j % kBlk);
        Expr iscale_ = scale(src_id, i)[1];
        qval(j, i) = nearest_int(x_(i * kQK_K + src_offset, src_id) * iscale_);
        qval.compute_root();

        Func bsums("bsums");
        Var idx("idx");
        RDom rj(0, kQK_K * 4, "rj");
        Expr index_q8_k = (((rj & (4 * kBlk - 1)) >> 3) << 2) + ((rj >> 8) << 4) + ((rj >> 6) & 3);
        bsums(idx, i) = 0;
        bsums(index_q8_k, i) += qval(rj, i);
        bsums.compute_root();

        Expr d_row = clamp(byte / 4, 0, 3);
        Expr d_byte_in_row = clamp(byte % 4, 0, 3);
        Expr d_bits = reinterpret<uint32_t>(scale(d_row, i)[0]);
        Expr d_byte = cast<uint8_t>((d_bits >> (cast<uint32_t>(d_byte_in_row) * 8)) & 0xff);

        Expr qs_byte = reinterpret<uint8_t>(cast<int8_t>(qval(clamp(byte - 16, 0, kQK_K * 4 - 1), i)));

        Expr bsums_rel = clamp(byte - (16 + kQK_K * 4), 0, kNumGroups * 4 * 2 - 1);
        Expr bsums_group = bsums_rel / 2;
        Expr bsums_is_lo = (bsums_rel % 2) == 0;
        Expr bsums_bits = reinterpret<uint16_t>(cast<int16_t>(bsums(bsums_group, i)));
        Expr bsums_byte = cast<uint8_t>(select(bsums_is_lo, bsums_bits & 0xff, (bsums_bits >> 8) & 0xff));

        blocks_(byte, i) =
            select(byte < 16, d_byte, byte < 16 + kQK_K * 4, qs_byte, bsums_byte);

        x_.dim(0).set_min(0);
        x_.dim(1).set_bounds(0, 4);
        blocks_.dim(0).set_bounds(0, kBlockBytesQ8_Kx4);
        blocks_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q8_0_4x4QuantizeMatGenerator, q8_0_4x4_quantize_mat)
HALIDE_REGISTER_GENERATOR(Q8_0_4x8QuantizeMatGenerator, q8_0_4x8_quantize_mat)
HALIDE_REGISTER_GENERATOR(Q8_K_4x4QuantizeMatGenerator, q8_k_4x4_quantize_mat)
HALIDE_REGISTER_GENERATOR(Q8_K_4x8QuantizeMatGenerator, q8_k_4x8_quantize_mat)
