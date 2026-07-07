// From-scratch Halide reimplementation of GGML's "repack" gemm kernels (see
// src/ggml-cpu/repack.cpp: ggml_gemm_q4_0_4x4_q8_0_generic /
// ggml_gemm_q4_0_4x8_q8_0_generic / ggml_gemm_q4_0_8x8_q8_0_generic /
// ggml_gemm_q8_0_4x4_q8_0_generic / ggml_gemm_q8_0_4x8_q8_0_generic /
// ggml_gemm_iq4_nl_4x4_q8_0_generic / ggml_gemm_iq4_nl_8x8_q8_0_generic /
// ggml_gemm_mxfp4_4x4_q8_0_generic / ggml_gemm_mxfp4_8x8_q8_0_generic
// upstream, as of GGML v0.15.3). Unlike gemv (see repack_gemv_generators.cpp),
// gemm processes `nr` activation rows at a time in groups of 4, packed by a
// repack_quantize_mat kernel into the *same* interleaved block_q8_0x4 format
// the weight side uses -- see repack_quantize_mat_generators.cpp, which this
// file's activation indexing exactly mirrors (both must agree on which
// `blocklen` groups a column's nibbles/bytes together, since gemm reads
// whatever repack_quantize_mat variant matches the weight's own blocklen).
//
// Weight block layout is identical to repack_gemv_generators.cpp's -- see
// that file's header comment. Activation blocks are a 3-D
// (byte, k-block, row-group) array, dim 2 selecting which group of 4 packed
// activation rows (`a_ptr = vy + row_group * nb + k_block`, contiguous in
// k_block for a fixed row_group), each block always block_q8_0x4-shaped
// (8-byte header: 4 fp16 deltas, one per row in the group) regardless of the
// weight family, since GGML only ever groups activations 4 rows at a time.
//
// Like the vec_dot and gemv generators, this is a tolerance-checked
// benchmark: interleaved weight/activation nibbles are dequantized with the
// plain "nibble - 8" formula rather than GGML's fused shift-and-rescale
// integer trick (mathematically equivalent, just re-grouped).
//
// This is intentionally unscheduled beyond the minimum Halide requires for
// legality -- scheduling for performance is a later step.

#include "Halide.h"

using namespace Halide;

namespace {

constexpr int kQK = 32;                                          // QK4_0 == QK8_0, both 32.
constexpr int kActNRows = 4;                                     // GGML always packs activations 4 rows at a time.
constexpr int kActBlockBytes = 2 * kActNRows + kQK * kActNRows;  // block_q8_0x4, 136 bytes.

// See repack_gemv_generators.cpp's comment on the same function: the repack
// step XORs every nibble with 0x8, turning the excess-8 encoding into a
// native two's-complement 4-bit signed value, so the value is recovered
// directly (not via "nibble - 8").
Expr q4_0_family_weight_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 3>> &blocks, int n_cols,
                              int blocklen, Expr j, Expr x, Expr r) {
    Expr l = r / kQK;
    Expr e = r % kQK;
    Expr half = e / (kQK / 2);
    Expr e_local = e % (kQK / 2);
    Expr k = e_local / blocklen;
    Expr i = e_local % blocklen;
    Expr qs_idx = k * n_cols * blocklen + j * blocklen + i;
    Expr byte = blocks(2 * n_cols + qs_idx, l, x);
    Expr nibble = cast<int32_t>(select(half == 0, byte & 0x0f, (byte >> 4) & 0x0f));
    Expr q = select(nibble < 8, nibble, nibble - 16);
    Expr d_lo = cast<uint16_t>(blocks(2 * j, l, x));
    Expr d_hi = cast<uint16_t>(blocks(2 * j + 1, l, x));
    Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
    return cast<float>(q) * d;
}

Expr q8_0_family_weight_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 3>> &blocks, int n_cols,
                              int blocklen, Expr j, Expr x, Expr r) {
    Expr l = r / kQK;
    Expr e = r % kQK;
    Expr k = e / blocklen;
    Expr i = e % blocklen;
    Expr qs_idx = k * n_cols * blocklen + j * blocklen + i;
    Expr byte = blocks(2 * n_cols + qs_idx, l, x);
    Expr q = reinterpret<int8_t>(byte);
    Expr d_lo = cast<uint16_t>(blocks(2 * j, l, x));
    Expr d_hi = cast<uint16_t>(blocks(2 * j + 1, l, x));
    Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
    return cast<float>(q) * d;
}

// Activation side for the Q4_0 weight family: same nibble-halves split as
// the weight side, but grouped by row (`m`, one of the 4 packed rows) rather
// than by interleaved column, and always with blocklen*4 elements between
// halves (the packed activation block always covers exactly 4 rows).
Expr q4_0_family_act_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 3>> &blocks, int blocklen, Expr m,
                           Expr y, Expr r) {
    Expr l = r / kQK;
    Expr e = r % kQK;
    Expr half = e / (kQK / 2);
    Expr e_local = e % (kQK / 2);
    Expr k = e_local / blocklen;
    Expr i = e_local % blocklen;
    Expr qs_idx = k * kActNRows * blocklen + m * blocklen + i + half * (kQK / 2 * kActNRows);
    Expr byte = blocks(2 * kActNRows + qs_idx, l, y);
    Expr q = reinterpret<int8_t>(byte);
    Expr d_lo = cast<uint16_t>(blocks(2 * m, l, y));
    Expr d_hi = cast<uint16_t>(blocks(2 * m + 1, l, y));
    Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
    return cast<float>(q) * d;
}

// Activation side for the Q8_0 weight family: no nibble halves, matching
// that family's own weight-side layout.
Expr q8_0_family_act_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 3>> &blocks, int blocklen, Expr m,
                           Expr y, Expr r) {
    Expr l = r / kQK;
    Expr e = r % kQK;
    Expr k = e / blocklen;
    Expr i = e % blocklen;
    Expr qs_idx = k * kActNRows * blocklen + m * blocklen + i;
    Expr byte = blocks(2 * kActNRows + qs_idx, l, y);
    Expr q = reinterpret<int8_t>(byte);
    Expr d_lo = cast<uint16_t>(blocks(2 * m, l, y));
    Expr d_hi = cast<uint16_t>(blocks(2 * m + 1, l, y));
    Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
    return cast<float>(q) * d;
}

// kvalues_iq4nl/kvalues_mxfp4: see repack_gemv_generators.cpp's copies of
// the same tables (mirrors iq4_nl_generators.cpp/mxfp4_generators.cpp) --
// embedded as compile-time constant Buffers baked into the pipeline, not a
// deep select() chain.
Expr lookup_iq4nl(Expr idx) {
    static const int8_t kValues[16] = {-127, -104, -83, -65, -49, -35, -22, -10,
                                       1, 13, 25, 38, 53, 69, 89, 113};
    static const Buffer<int8_t> lut(const_cast<int8_t *>(kValues), 16, "kvalues_iq4nl");
    return cast<int32_t>(lut(idx));
}

Expr lookup_mxfp4(Expr idx) {
    static const int8_t kValues[16] = {0, 1, 2, 3, 4, 6, 8, 12, 0, -1, -2, -3, -4, -6, -8, -12};
    static const Buffer<int8_t> lut(const_cast<int8_t *>(kValues), 16, "kvalues_mxfp4");
    return cast<int32_t>(lut(idx));
}

// IQ4_NL weight family: same nibble/halves layout as Q4_0, but a plain
// codebook lookup (no XOR trick -- see repack_gemv_generators.cpp's header
// comment). Activation side reuses q4_0_family_act_value: it depends only
// on the shared nibble/halves indexing scheme, not on Q4_0's own weight
// decode, so it applies to any nibble-packed weight family paired with Q8_0
// activations (IQ4_NL and MXFP4 included).
Expr iq4_nl_family_weight_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 3>> &blocks, int n_cols,
                                int blocklen, Expr j, Expr x, Expr r) {
    Expr l = r / kQK;
    Expr e = r % kQK;
    Expr half = e / (kQK / 2);
    Expr e_local = e % (kQK / 2);
    Expr k = e_local / blocklen;
    Expr i = e_local % blocklen;
    Expr qs_idx = k * n_cols * blocklen + j * blocklen + i;
    Expr byte = blocks(2 * n_cols + qs_idx, l, x);
    Expr nibble = cast<int32_t>(select(half == 0, byte & 0x0f, byte >> 4));
    Expr val = lookup_iq4nl(nibble);
    Expr d_lo = cast<uint16_t>(blocks(2 * j, l, x));
    Expr d_hi = cast<uint16_t>(blocks(2 * j + 1, l, x));
    Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));
    return cast<float>(val) * d;
}

// MXFP4 weight family: same nibble/halves layout, but a 1-byte-per-column
// E8M0 exponent header (byte `j`) instead of a 2-byte-per-column fp16 delta.
Expr mxfp4_family_weight_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 3>> &blocks, int n_cols,
                               int blocklen, Expr j, Expr x, Expr r) {
    Expr l = r / kQK;
    Expr e_idx = r % kQK;
    Expr half = e_idx / (kQK / 2);
    Expr e_local = e_idx % (kQK / 2);
    Expr k = e_local / blocklen;
    Expr i = e_local % blocklen;
    Expr qs_idx = k * n_cols * blocklen + j * blocklen + i;
    Expr byte = blocks(n_cols + qs_idx, l, x);
    Expr nibble = cast<int32_t>(select(half == 0, byte & 0x0f, byte >> 4));
    Expr val = lookup_mxfp4(nibble);
    Expr e = blocks(j, l, x);
    Expr bits = select(cast<uint32_t>(e) < 2, cast<uint32_t>(0x00200000) << cast<uint32_t>(e),
                       (cast<uint32_t>(e) - 1) << 23);
    Expr d = reinterpret<float>(bits);
    return cast<float>(val) * d;
}

class Q4_0_4x4GemmGenerator : public Generator<Q4_0_4x4GemmGenerator> {
public:
    // dim 0: byte-within-block, dim 1: k-block, dim 2: column group.
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    // dim 0: byte-within-block, dim 1: k-block, dim 2: row group (4 rows/group).
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    // dim 0: column-within-group, dim 1: column group, dim 2: row-within-group, dim 3: row group.
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 4, kBlockLen = 4;
        constexpr int kWeightBlockBytes = 2 * kNCols + (kQK * kNCols * 4) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x, m, y) = sum(q4_0_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                             q4_0_family_act_value(act_blocks_, kBlockLen, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRows);
        s_.dim(3).set_min(0);
    }
};

class Q4_0_4x8GemmGenerator : public Generator<Q4_0_4x8GemmGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 4, kBlockLen = 8;
        constexpr int kWeightBlockBytes = 2 * kNCols + (kQK * kNCols * 4) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x, m, y) = sum(q4_0_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                             q4_0_family_act_value(act_blocks_, kBlockLen, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRows);
        s_.dim(3).set_min(0);
    }
};

class Q4_0_8x8GemmGenerator : public Generator<Q4_0_8x8GemmGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 8;
        constexpr int kWeightBlockBytes = 2 * kNCols + (kQK * kNCols * 4) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x, m, y) = sum(q4_0_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                             q4_0_family_act_value(act_blocks_, kBlockLen, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRows);
        s_.dim(3).set_min(0);
    }
};

class Q8_0_4x4GemmGenerator : public Generator<Q8_0_4x4GemmGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 4, kBlockLen = 4;
        constexpr int kWeightBlockBytes = 2 * kNCols + (kQK * kNCols * 8) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x, m, y) = sum(q8_0_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                             q8_0_family_act_value(act_blocks_, kBlockLen, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRows);
        s_.dim(3).set_min(0);
    }
};

class Q8_0_4x8GemmGenerator : public Generator<Q8_0_4x8GemmGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 4, kBlockLen = 8;
        constexpr int kWeightBlockBytes = 2 * kNCols + (kQK * kNCols * 8) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x, m, y) = sum(q8_0_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                             q8_0_family_act_value(act_blocks_, kBlockLen, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRows);
        s_.dim(3).set_min(0);
    }
};

class IQ4_NL_4x4GemmGenerator : public Generator<IQ4_NL_4x4GemmGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 4, kBlockLen = 4;
        constexpr int kWeightBlockBytes = 2 * kNCols + (kQK * kNCols * 4) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x, m, y) = sum(iq4_nl_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                             q4_0_family_act_value(act_blocks_, kBlockLen, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRows);
        s_.dim(3).set_min(0);
    }
};

class IQ4_NL_8x8GemmGenerator : public Generator<IQ4_NL_8x8GemmGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 8;
        constexpr int kWeightBlockBytes = 2 * kNCols + (kQK * kNCols * 4) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x, m, y) = sum(iq4_nl_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                             q4_0_family_act_value(act_blocks_, kBlockLen, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRows);
        s_.dim(3).set_min(0);
    }
};

class MXFP4_4x4GemmGenerator : public Generator<MXFP4_4x4GemmGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 4, kBlockLen = 4;
        constexpr int kWeightBlockBytes = kNCols + (kQK * kNCols * 4) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x, m, y) = sum(mxfp4_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                             q4_0_family_act_value(act_blocks_, kBlockLen, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRows);
        s_.dim(3).set_min(0);
    }
};

class MXFP4_8x8GemmGenerator : public Generator<MXFP4_8x8GemmGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 8;
        constexpr int kWeightBlockBytes = kNCols + (kQK * kNCols * 4) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x, m, y) = sum(mxfp4_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                             q4_0_family_act_value(act_blocks_, kBlockLen, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRows);
        s_.dim(3).set_min(0);
    }
};

// ---- K-quants (Q4_K/Q5_K/Q6_K/Q2_K): see repack_gemv_generators.cpp's
// header comment on this section -- same 256-element superblocks, always 8
// interleaved columns, same "fully dequantize both sides and multiply"
// simplification (bsums untouched). gemm's activation is packed 4 rows at a
// time into block_q8_Kx4 by the matching repack_quantize_mat_q8_k_4x4/4x8
// kernel (see repack_quantize_mat_generators.cpp), same as every other
// family's gemm in this file.

constexpr int kQK_K = 256;
constexpr int kActNRowsK = 4;
constexpr int kActBlockBytesK = 4 * kActNRowsK + kQK_K * kActNRowsK;  // block_q8_Kx4, 1040 of 1168 bytes used (bsums skipped).

// Shared by Q4_K and Q5_K: both process a superblock as 4 iterations of 64
// elements (iter = gi/64), each iteration split into a low/high-nibble half
// (half64 = (gi%64)/32) -- this activation indexing depends only on that
// shared structure (and `blocklen`, matching whichever repack_quantize_mat
// variant packed it), not on either family's own weight-side decode.
Expr q8_k_iter_half_act_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 3>> &blocks, int blocklen, Expr m,
                              Expr y, Expr r) {
    Expr l = r / kQK_K;
    Expr gi = r % kQK_K;
    Expr iter = gi / 64;
    Expr local64 = gi % 64;
    Expr half64 = local64 / 32;
    Expr lpos = local64 % 32;
    Expr k_inner = lpos / blocklen;
    Expr ii = lpos % blocklen;

    Expr qs_idx = iter * 256 + half64 * 128 + k_inner * (4 * blocklen) + m * blocklen + ii;
    Expr byte = blocks(4 * kActNRowsK + qs_idx, l, y);
    Expr q = reinterpret<int8_t>(byte);

    Expr d_b0 = cast<uint32_t>(blocks(4 * m + 0, l, y));
    Expr d_b1 = cast<uint32_t>(blocks(4 * m + 1, l, y));
    Expr d_b2 = cast<uint32_t>(blocks(4 * m + 2, l, y));
    Expr d_b3 = cast<uint32_t>(blocks(4 * m + 3, l, y));
    Expr d = reinterpret<float>(d_b0 | (d_b1 << 8) | (d_b2 << 16) | (d_b3 << 24));

    return cast<float>(q) * d;
}

// See repack_gemv_generators.cpp's q4_k_family_weight_value for the byte-
// layout reasoning (identical here -- weight decode doesn't depend on
// gemv vs gemm).
Expr q4_k_family_weight_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 3>> &blocks, int blocklen, Expr j,
                              Expr x, Expr r) {
    constexpr int kNCols = 8;
    constexpr int kDOffset = 0, kDminOffset = 16, kScalesOffset = 32, kQsOffset = 128;

    Expr l = r / kQK_K;
    Expr gi = r % kQK_K;

    Expr iter = gi / 64;
    Expr local64 = gi % 64;
    Expr half64 = local64 / 32;
    Expr lpos = local64 % 32;
    Expr orig_sb = iter * 2 + half64;

    Expr k_inner = lpos / blocklen;
    Expr ii = lpos % blocklen;
    Expr k = iter * (32 / blocklen) + k_inner;

    Expr qs_idx = k * kNCols * blocklen + j * blocklen + ii;
    Expr qs_byte = blocks(kQsOffset + qs_idx, l, x);
    Expr nibble = cast<int32_t>(select(half64 == 0, qs_byte & 0x0f, qs_byte >> 4));

    Expr window = (orig_sb / 4) * 48 + (orig_sb % 4) * 12;
    Expr jj = clamp(j - 4, 0, 3);
    Expr sc = select(j < 4, blocks(kScalesOffset + window + j, l, x) & 0x3f,
                     cast<uint8_t>((blocks(kScalesOffset + window + 8 + jj, l, x) & 0x0f) |
                                   ((blocks(kScalesOffset + window + jj, l, x) >> 6) << 4)));
    Expr m = select(j < 4, blocks(kScalesOffset + window + j + 4, l, x) & 0x3f,
                    cast<uint8_t>((blocks(kScalesOffset + window + 8 + jj, l, x) >> 4) |
                                  ((blocks(kScalesOffset + window + 4 + jj, l, x) >> 6) << 4)));

    Expr d_lo = cast<uint16_t>(blocks(kDOffset + 2 * j, l, x));
    Expr d_hi = cast<uint16_t>(blocks(kDOffset + 2 * j + 1, l, x));
    Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

    Expr dmin_lo = cast<uint16_t>(blocks(kDminOffset + 2 * j, l, x));
    Expr dmin_hi = cast<uint16_t>(blocks(kDminOffset + 2 * j + 1, l, x));
    Expr dmin = cast<float>(reinterpret<float16_t>(dmin_lo | (dmin_hi << 8)));

    Expr d1 = d * cast<float>(sc);
    Expr m1 = dmin * cast<float>(m);
    return d1 * cast<float>(nibble) - m1;
}

// See repack_gemv_generators.cpp's q5_k_family_weight_value for the byte-
// layout reasoning.
Expr q5_k_family_weight_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 3>> &blocks, int blocklen, Expr j,
                              Expr x, Expr r) {
    constexpr int kNCols = 8;
    constexpr int kDOffset = 0, kDminOffset = 16, kScalesOffset = 32, kQhOffset = 128, kQsOffset = 384;

    Expr l = r / kQK_K;
    Expr gi = r % kQK_K;

    Expr iter = gi / 64;
    Expr local64 = gi % 64;
    Expr half64 = local64 / 32;
    Expr lpos = local64 % 32;
    Expr orig_sb = iter * 2 + half64;

    Expr k_inner = lpos / blocklen;
    Expr ii = lpos % blocklen;
    Expr k = iter * (32 / blocklen) + k_inner;

    Expr qs_idx = k * kNCols * blocklen + j * blocklen + ii;
    Expr qs_byte = blocks(kQsOffset + qs_idx, l, x);
    Expr nibble = cast<int32_t>(select(half64 == 0, qs_byte & 0x0f, qs_byte >> 4));

    Expr qh_offset = k_inner * (blocklen * kNCols) + j * blocklen + ii;
    Expr qh_byte = blocks(kQhOffset + qh_offset, l, x);
    Expr qh_shift = iter * 2 + half64;
    Expr h_bit = cast<int32_t>((cast<uint32_t>(qh_byte) >> qh_shift) & 1);
    Expr value5 = nibble | (h_bit << 4);

    Expr window = (orig_sb / 4) * 48 + (orig_sb % 4) * 12;
    Expr jj = clamp(j - 4, 0, 3);
    Expr sc = select(j < 4, blocks(kScalesOffset + window + j, l, x) & 0x3f,
                     cast<uint8_t>((blocks(kScalesOffset + window + 8 + jj, l, x) & 0x0f) |
                                   ((blocks(kScalesOffset + window + jj, l, x) >> 6) << 4)));
    Expr m = select(j < 4, blocks(kScalesOffset + window + j + 4, l, x) & 0x3f,
                    cast<uint8_t>((blocks(kScalesOffset + window + 8 + jj, l, x) >> 4) |
                                  ((blocks(kScalesOffset + window + 4 + jj, l, x) >> 6) << 4)));

    Expr d_lo = cast<uint16_t>(blocks(kDOffset + 2 * j, l, x));
    Expr d_hi = cast<uint16_t>(blocks(kDOffset + 2 * j + 1, l, x));
    Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

    Expr dmin_lo = cast<uint16_t>(blocks(kDminOffset + 2 * j, l, x));
    Expr dmin_hi = cast<uint16_t>(blocks(kDminOffset + 2 * j + 1, l, x));
    Expr dmin = cast<float>(reinterpret<float16_t>(dmin_lo | (dmin_hi << 8)));

    Expr d1 = d * cast<float>(sc);
    Expr m1 = dmin * cast<float>(m);
    return d1 * cast<float>(value5) - m1;
}

class Q5_K_8x4GemmGenerator : public Generator<Q5_K_8x4GemmGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 4;
        constexpr int kWeightBlockBytes = 16 + 16 + 96 + (kQK_K * kNCols) / 8 + (kQK_K * kNCols * 4) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK_K, "r");

        s_(j, x, m, y) = sum(q5_k_family_weight_value(weight_blocks_, kBlockLen, j, x, r) *
                             q8_k_iter_half_act_value(act_blocks_, kBlockLen, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytesK);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRowsK);
        s_.dim(3).set_min(0);
    }
};

class Q5_K_8x8GemmGenerator : public Generator<Q5_K_8x8GemmGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 8;
        constexpr int kWeightBlockBytes = 16 + 16 + 96 + (kQK_K * kNCols) / 8 + (kQK_K * kNCols * 4) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK_K, "r");

        s_(j, x, m, y) = sum(q5_k_family_weight_value(weight_blocks_, kBlockLen, j, x, r) *
                             q8_k_iter_half_act_value(act_blocks_, kBlockLen, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytesK);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRowsK);
        s_.dim(3).set_min(0);
    }
};

class Q4_K_8x4GemmGenerator : public Generator<Q4_K_8x4GemmGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 4;
        constexpr int kWeightBlockBytes = 16 + 16 + 96 + (kQK_K * kNCols * 4) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK_K, "r");

        s_(j, x, m, y) = sum(q4_k_family_weight_value(weight_blocks_, kBlockLen, j, x, r) *
                             q8_k_iter_half_act_value(act_blocks_, kBlockLen, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytesK);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRowsK);
        s_.dim(3).set_min(0);
    }
};

class Q4_K_8x8GemmGenerator : public Generator<Q4_K_8x8GemmGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 8;
        constexpr int kWeightBlockBytes = 16 + 16 + 96 + (kQK_K * kNCols * 4) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK_K, "r");

        s_(j, x, m, y) = sum(q4_k_family_weight_value(weight_blocks_, kBlockLen, j, x, r) *
                             q8_k_iter_half_act_value(act_blocks_, kBlockLen, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytesK);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRowsK);
        s_.dim(3).set_min(0);
    }
};

// See repack_gemv_generators.cpp's q6_k_family_weight_value for the byte-
// layout reasoning (identical here).
Expr q6_k_family_weight_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 3>> &blocks, int blocklen, Expr j,
                              Expr x, Expr r) {
    constexpr int kNCols = 8;
    constexpr int kDOffset = 0, kScalesOffset = 16, kQlOffset = 144, kQhOffset = 1168;
    const int blocks_per_half = 64 / blocklen;

    Expr l = r / kQK_K;
    Expr gi = r % kQK_K;

    Expr local128 = gi % 128;
    Expr is_high = local128 >= 64;
    Expr pos64 = local128 % 64;
    Expr i = pos64 % blocklen;
    Expr base_l = gi - i - select(is_high, 64, 0);
    Expr base_h = base_l + 64;

    // See repack_gemv_generators.cpp's identical comment: these are select()
    // arguments, evaluated unconditionally, so each is clamped to its
    // array's valid range.
    constexpr int kQlSize = (kQK_K * kNCols) / 2;
    constexpr int kQhSize = (kQK_K * kNCols) / 4;
    constexpr int kNumGroups = kQK_K / 16;

    Expr k = (base_l / 128) * blocks_per_half + (base_l % 128) / blocklen;
    Expr ql_pos = clamp(k * kNCols * blocklen + j * blocklen + i, 0, kQlSize - 1);
    Expr ql_byte = blocks(kQlOffset + ql_pos, l, x);
    Expr nibble = cast<int32_t>(select(is_high, ql_byte >> 4, ql_byte & 0x0f));

    Expr scale_idx_l = clamp(base_l / 16, 0, kNumGroups - 1);
    Expr scale_idx_h = clamp(base_h / 16, 0, kNumGroups - 1);
    Expr scale_idx = select(is_high, scale_idx_h, scale_idx_l);

    Expr qh_shift_l = ((base_l % 128) / 32) * 2;
    Expr qh_shift_h = ((base_h % 128) / 32) * 2;
    Expr qh_shift = select(is_high, qh_shift_h, qh_shift_l);

    Expr qh_half_l = (base_l / 128) * 32;
    Expr qh_half_h = (base_h / 128) * 32;
    Expr qh_idx_l = qh_half_l + ((base_l + i) % 32);
    Expr qh_idx_h = qh_half_h + ((base_h + i) % 32);
    Expr qh_offset_l = clamp((qh_idx_l / blocklen) * (blocklen * kNCols) + j * blocklen + (qh_idx_l % blocklen), 0,
                             kQhSize - 1);
    Expr qh_offset_h = clamp((qh_idx_h / blocklen) * (blocklen * kNCols) + j * blocklen + (qh_idx_h % blocklen), 0,
                             kQhSize - 1);
    Expr qh_offset = select(is_high, qh_offset_h, qh_offset_l);

    Expr qh_byte = blocks(kQhOffset + qh_offset, l, x);
    Expr hi2 = cast<int32_t>((qh_byte >> qh_shift) & 3);
    Expr q_signed = (nibble | (hi2 << 4)) - 32;

    Expr scale = reinterpret<int8_t>(blocks(kScalesOffset + scale_idx * kNCols + j, l, x));

    Expr d_lo = cast<uint16_t>(blocks(kDOffset + 2 * j, l, x));
    Expr d_hi = cast<uint16_t>(blocks(kDOffset + 2 * j + 1, l, x));
    Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

    return d * cast<float>(scale) * cast<float>(q_signed);
}

// Q6_K's own activation indexing (distinct from Q4_K/Q5_K's iter/half64
// structure): matches ggml_gemm_q6_K_NxM_q8_K_generic_impl's `q8_base`
// computation, keyed off the same base_l/is_high split the weight side
// uses.
Expr q6_k_act_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 3>> &blocks, int blocklen, Expr m, Expr y,
                    Expr r) {
    const int blocks_per_half = 64 / blocklen;
    constexpr int kQsSize = kQK_K * kActNRowsK;

    Expr l = r / kQK_K;
    Expr gi = r % kQK_K;

    Expr local128 = gi % 128;
    Expr is_high = local128 >= 64;
    Expr pos64 = local128 % 64;
    Expr i = pos64 % blocklen;
    Expr base_l = gi - i - select(is_high, 64, 0);

    Expr q8_base = (base_l / 128) * 512 + ((base_l % 128) / blocklen) * (blocklen * 4);
    Expr qs_idx = clamp(q8_base + m * blocklen + i + select(is_high, 256, 0), 0, kQsSize - 1);

    Expr byte = blocks(4 * kActNRowsK + qs_idx, l, y);
    Expr q = reinterpret<int8_t>(byte);

    Expr d_b0 = cast<uint32_t>(blocks(4 * m + 0, l, y));
    Expr d_b1 = cast<uint32_t>(blocks(4 * m + 1, l, y));
    Expr d_b2 = cast<uint32_t>(blocks(4 * m + 2, l, y));
    Expr d_b3 = cast<uint32_t>(blocks(4 * m + 3, l, y));
    Expr d = reinterpret<float>(d_b0 | (d_b1 << 8) | (d_b2 << 16) | (d_b3 << 24));

    return cast<float>(q) * d;
}

class Q6_K_8x4GemmGenerator : public Generator<Q6_K_8x4GemmGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 4;
        constexpr int kWeightBlockBytes = 16 + 128 + (kQK_K * kNCols * 4) / 8 + (kQK_K * kNCols * 2) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK_K, "r");

        s_(j, x, m, y) = sum(q6_k_family_weight_value(weight_blocks_, kBlockLen, j, x, r) *
                             q6_k_act_value(act_blocks_, kBlockLen, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytesK);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRowsK);
        s_.dim(3).set_min(0);
    }
};

class Q6_K_8x8GemmGenerator : public Generator<Q6_K_8x8GemmGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 8;
        constexpr int kWeightBlockBytes = 16 + 128 + (kQK_K * kNCols * 4) / 8 + (kQK_K * kNCols * 2) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK_K, "r");

        s_(j, x, m, y) = sum(q6_k_family_weight_value(weight_blocks_, kBlockLen, j, x, r) *
                             q6_k_act_value(act_blocks_, kBlockLen, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytesK);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRowsK);
        s_.dim(3).set_min(0);
    }
};

// See repack_gemv_generators.cpp's q2_k_family_weight_value for the byte-
// layout reasoning.
Expr q2_k_family_weight_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 3>> &blocks, Expr j, Expr x,
                              Expr r) {
    constexpr int kNCols = 8, kBlockLen = 8;
    constexpr int kDOffset = 0, kDminOffset = 16, kScalesOffset = 32, kQsOffset = 160;

    Expr l = r / kQK_K;
    Expr gi = r % kQK_K;

    Expr half = gi / 128;
    Expr local = gi % 128;
    Expr sub = local / 32;
    Expr rem32 = local % 32;

    Expr k = half * 4 + rem32 / kBlockLen;
    Expr i = rem32 % kBlockLen;
    Expr qs_pos = k * kNCols * kBlockLen + j * kBlockLen + i;
    Expr qs_byte = blocks(kQsOffset + qs_pos, l, x);
    Expr twobit = cast<int32_t>((qs_byte >> (sub * 2)) & 3);

    Expr sm_idx = half * 64 + sub * 16 + j * 2 + select(rem32 >= 16, 1, 0);
    Expr sm_byte = blocks(kScalesOffset + sm_idx, l, x);
    Expr scale_nibble = cast<int32_t>(sm_byte & 0x0f);
    Expr min_nibble = cast<int32_t>(sm_byte >> 4);

    Expr d_lo = cast<uint16_t>(blocks(kDOffset + 2 * j, l, x));
    Expr d_hi = cast<uint16_t>(blocks(kDOffset + 2 * j + 1, l, x));
    Expr d = cast<float>(reinterpret<float16_t>(d_lo | (d_hi << 8)));

    Expr dmin_lo = cast<uint16_t>(blocks(kDminOffset + 2 * j, l, x));
    Expr dmin_hi = cast<uint16_t>(blocks(kDminOffset + 2 * j + 1, l, x));
    Expr dmin = cast<float>(reinterpret<float16_t>(dmin_lo | (dmin_hi << 8)));

    Expr dl = d * cast<float>(scale_nibble);
    Expr ml = dmin * cast<float>(min_nibble);
    return dl * cast<float>(twobit) - ml;
}

// Q2_K's own activation indexing: matches ggml_gemm_q2_K_8x8_q8_K_generic's
// `sumi1..4` offsets (+0/+128/+256/+384 selecting which of the 4 packed
// 2-bit values' matching activation elements to read).
Expr q2_k_act_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 3>> &blocks, Expr m, Expr y, Expr r) {
    constexpr int kBlockLen = 8;

    Expr l = r / kQK_K;
    Expr gi = r % kQK_K;

    Expr half = gi / 128;
    Expr local = gi % 128;
    Expr sub = local / 32;
    Expr rem32 = local % 32;
    Expr i = rem32 % kBlockLen;

    Expr qs_idx = half * 512 + (rem32 / kBlockLen) * (4 * kBlockLen) + m * kBlockLen + i + sub * 128;
    Expr byte = blocks(4 * kActNRowsK + qs_idx, l, y);
    Expr q = reinterpret<int8_t>(byte);

    Expr d_b0 = cast<uint32_t>(blocks(4 * m + 0, l, y));
    Expr d_b1 = cast<uint32_t>(blocks(4 * m + 1, l, y));
    Expr d_b2 = cast<uint32_t>(blocks(4 * m + 2, l, y));
    Expr d_b3 = cast<uint32_t>(blocks(4 * m + 3, l, y));
    Expr d = reinterpret<float>(d_b0 | (d_b1 << 8) | (d_b2 << 16) | (d_b3 << 24));

    return cast<float>(q) * d;
}

class Q2_K_8x8GemmGenerator : public Generator<Q2_K_8x8GemmGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 3>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 4>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8;
        constexpr int kWeightBlockBytes = 16 + 16 + 128 + (kQK_K * kNCols * 2) / 8;

        Var j("j"), x("x"), m("m"), y("y");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK_K, "r");

        s_(j, x, m, y) = sum(q2_k_family_weight_value(weight_blocks_, j, x, r) * q2_k_act_value(act_blocks_, m, y, r));

        weight_blocks_.dim(0).set_bounds(0, kWeightBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytesK);
        act_blocks_.dim(1).set_min(0);
        act_blocks_.dim(2).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
        s_.dim(2).set_bounds(0, kActNRowsK);
        s_.dim(3).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q4_0_4x4GemmGenerator, q4_0_4x4_gemm)
HALIDE_REGISTER_GENERATOR(Q4_0_4x8GemmGenerator, q4_0_4x8_gemm)
HALIDE_REGISTER_GENERATOR(Q4_0_8x8GemmGenerator, q4_0_8x8_gemm)
HALIDE_REGISTER_GENERATOR(Q8_0_4x4GemmGenerator, q8_0_4x4_gemm)
HALIDE_REGISTER_GENERATOR(Q8_0_4x8GemmGenerator, q8_0_4x8_gemm)
HALIDE_REGISTER_GENERATOR(IQ4_NL_4x4GemmGenerator, iq4_nl_4x4_gemm)
HALIDE_REGISTER_GENERATOR(IQ4_NL_8x8GemmGenerator, iq4_nl_8x8_gemm)
HALIDE_REGISTER_GENERATOR(MXFP4_4x4GemmGenerator, mxfp4_4x4_gemm)
HALIDE_REGISTER_GENERATOR(MXFP4_8x8GemmGenerator, mxfp4_8x8_gemm)
HALIDE_REGISTER_GENERATOR(Q4_K_8x4GemmGenerator, q4_k_8x4_gemm)
HALIDE_REGISTER_GENERATOR(Q4_K_8x8GemmGenerator, q4_k_8x8_gemm)
HALIDE_REGISTER_GENERATOR(Q5_K_8x4GemmGenerator, q5_k_8x4_gemm)
HALIDE_REGISTER_GENERATOR(Q5_K_8x8GemmGenerator, q5_k_8x8_gemm)
HALIDE_REGISTER_GENERATOR(Q6_K_8x4GemmGenerator, q6_k_8x4_gemm)
HALIDE_REGISTER_GENERATOR(Q6_K_8x8GemmGenerator, q6_k_8x8_gemm)
HALIDE_REGISTER_GENERATOR(Q2_K_8x8GemmGenerator, q2_k_8x8_gemm)
