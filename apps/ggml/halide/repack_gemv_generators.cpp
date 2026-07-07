// From-scratch Halide reimplementation of GGML's "repack" gemv kernels (see
// src/ggml-cpu/repack.cpp: ggml_gemv_q4_0_4x4_q8_0_generic /
// ggml_gemv_q4_0_4x8_q8_0_generic / ggml_gemv_q4_0_8x8_q8_0_generic /
// ggml_gemv_q8_0_4x4_q8_0_generic / ggml_gemv_q8_0_4x8_q8_0_generic /
// ggml_gemv_iq4_nl_4x4_q8_0_generic / ggml_gemv_iq4_nl_8x8_q8_0_generic /
// ggml_gemv_mxfp4_4x4_q8_0_generic / ggml_gemv_mxfp4_8x8_q8_0_generic
// upstream, as of GGML v0.15.3). gemv computes one row of activations
// (nr == 1, always a plain, non-interleaved Q8_0 block per
// src/ggml-cpu/repack.cpp's own assert) against every column of a
// repack-interleaved weight matrix (see repack_quantize_mat_generators.cpp
// for how that weight matrix's packed layout comes to exist -- gemv only
// ever reads it).
//
// Like the vec_dot generators (see q4_0_generators.cpp/q8_0_generators.cpp/
// iq4_nl_generators.cpp/mxfp4_generators.cpp), this is a tolerance-checked
// benchmark, not bit-exact, so the interleaved weight nibbles are
// dequantized with the same formulas those plain (non-interleaved) kernels
// use instead of replicating GGML's fused shift-and-rescale integer trick
// (Q4_0) -- the two are mathematically equivalent, just re-grouped, and
// re-deriving the trick by hand would be error-prone for no behavioral
// difference. IQ4_NL/MXFP4's interleave step is already a plain nibble
// copy with no such trick (see repack_gemm_generators.cpp's comment on
// make_block_iq4_nlx4/mxfp4x4 upstream), so their codebook lookups need no
// reinterpretation at all.
//
// Weight block layout: a packed weight buffer is a 3-D
// (byte, k-block, col-group) array -- dim 2 selects which group of
// `ncols_interleaved` output columns, dim 1 selects which of the `n / 32`
// blocks along the reduction axis, matching vx's actual memory order
// (`b_ptr = vx + col_group * nb + k_block`, contiguous in k_block for a
// fixed col_group). Each block is:
//   Q4_0/IQ4_NL weight families (block<4, N>, N = ncols_interleaved):
//     byte 0 .. 2*N-1:  N fp16 deltas, one per interleaved column
//     byte 2*N ..:      16*N bytes of packed 4-bit values (2 per byte),
//                       interleaved in groups of `blocklen` per column --
//                       see weight_value() below for the exact index map.
//   Q8_0 weight family (block<8, N>, N = ncols_interleaved, always 4 here):
//     byte 0 .. 2*N-1:  N fp16 deltas
//     byte 2*N ..:      32*N bytes of signed int8 values, interleaved the
//                       same way (no nibble split).
//   MXFP4 weight family: like Q4_0/IQ4_NL, but a 1-byte E8M0 exponent per
//     column (byte 0 .. N-1) instead of an N-fp16-delta header, so its
//     packed nibbles start at byte N, not byte 2*N.
//
// This is intentionally unscheduled beyond the minimum Halide requires for
// legality -- scheduling for performance is a later step.

#include "Halide.h"

#include "activation_dequant.h"

using namespace Halide;

namespace {

constexpr int kQK = 32;                  // QK4_0 == QK8_0, both 32.
constexpr int kActBlockBytes = 2 + kQK;  // plain (non-interleaved) Q8_0 block.

// Q4_0 weight family: element `e` of interleaved column `j` (col-group `x`,
// k-block `l`) lives in the low nibble of a byte if e is in the block's
// first half, or the high nibble if in its second half (mirroring plain
// Q4_0's own two-halves-per-block layout) -- `blocklen` groups that many
// same-column nibbles together before the byte stream switches to the next
// interleaved column.
//
// Unlike plain Q4_0 (nibble - 8), the repack step that builds this
// interleaved format XORs every nibble with 0x8 (see make_block_q4_0x4/x8 in
// src/ggml-cpu/repack.cpp), which turns the excess-8 encoding into a native
// two's-complement 4-bit signed value -- that's what lets GGML's own gemv
// dequantize with a raw sign-extending shift instead of a subtract. The
// value is recovered here the same way, just spelled out instead of via the
// shift trick: nibbles < 8 are already their own value, nibbles >= 8 are
// negative (subtract 16).
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

// Q8_0 weight family: same interleave scheme, but a whole signed byte per
// element (no nibble split, no halves).
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

// kvalues_iq4nl/kvalues_mxfp4: small fixed codebooks, embedded as compile-
// time constant data (not an Input<Buffer<>>/ImageParam -- these Buffers are
// baked into the compiled pipeline as read-only resources) rather than a
// deep select() chain. Mirrors iq4_nl_generators.cpp/mxfp4_generators.cpp's
// own tables -- duplicated here rather than shared since each generator file
// is self-contained.
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

// IQ4_NL weight family: same nibble/halves layout as Q4_0, but the raw
// nibble is a codebook index (no XOR trick -- the repack step is a plain
// nibble copy, see make_block_iq4_nlx4/x8 upstream) into kvalues_iq4nl.
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

// MXFP4 weight family: same nibble/halves layout, codebook lookup into
// kvalues_mxfp4, but a 1-byte-per-column E8M0 exponent header (byte `j`,
// not a 2-byte-per-column fp16 delta) -- see ggml_e8m0_to_fp32_half
// upstream (mirrors mxfp4_generators.cpp's own reproduction of it).
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

class Q4_0_4x4GemvGenerator : public Generator<Q4_0_4x4GemvGenerator> {
public:
    // dim 0: byte-within-block, dim 1: k-block, dim 2: column group.
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    // Plain Q8_0 activation row: dim 0: byte-within-block, dim 1: k-block.
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    // dim 0: column-within-group, dim 1: column group.
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 4, kBlockLen = 4;
        constexpr int kBlockBytes = 2 * kNCols + (kQK * kNCols * 4) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x) = sum(q4_0_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                       ggml_halide::q8_0_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

class Q4_0_4x8GemvGenerator : public Generator<Q4_0_4x8GemvGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 4, kBlockLen = 8;
        constexpr int kBlockBytes = 2 * kNCols + (kQK * kNCols * 4) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x) = sum(q4_0_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                       ggml_halide::q8_0_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

class Q4_0_8x8GemvGenerator : public Generator<Q4_0_8x8GemvGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 8;
        constexpr int kBlockBytes = 2 * kNCols + (kQK * kNCols * 4) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x) = sum(q4_0_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                       ggml_halide::q8_0_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

class Q8_0_4x4GemvGenerator : public Generator<Q8_0_4x4GemvGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 4, kBlockLen = 4;
        constexpr int kBlockBytes = 2 * kNCols + (kQK * kNCols * 8) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x) = sum(q8_0_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                       ggml_halide::q8_0_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

class Q8_0_4x8GemvGenerator : public Generator<Q8_0_4x8GemvGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 4, kBlockLen = 8;
        constexpr int kBlockBytes = 2 * kNCols + (kQK * kNCols * 8) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x) = sum(q8_0_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                       ggml_halide::q8_0_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

class IQ4_NL_4x4GemvGenerator : public Generator<IQ4_NL_4x4GemvGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 4, kBlockLen = 4;
        constexpr int kBlockBytes = 2 * kNCols + (kQK * kNCols * 4) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x) = sum(iq4_nl_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                       ggml_halide::q8_0_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

class IQ4_NL_8x8GemvGenerator : public Generator<IQ4_NL_8x8GemvGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 8;
        constexpr int kBlockBytes = 2 * kNCols + (kQK * kNCols * 4) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x) = sum(iq4_nl_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                       ggml_halide::q8_0_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

class MXFP4_4x4GemvGenerator : public Generator<MXFP4_4x4GemvGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 4, kBlockLen = 4;
        constexpr int kBlockBytes = kNCols + (kQK * kNCols * 4) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x) = sum(mxfp4_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                       ggml_halide::q8_0_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

class MXFP4_8x8GemvGenerator : public Generator<MXFP4_8x8GemvGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 8;
        constexpr int kBlockBytes = kNCols + (kQK * kNCols * 4) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK, "r");

        s_(j, x) = sum(mxfp4_family_weight_value(weight_blocks_, kNCols, kBlockLen, j, x, r) *
                       ggml_halide::q8_0_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytes);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

// ---- K-quants (Q4_K/Q5_K/Q6_K/Q2_K): 256-element superblocks, always 8
// interleaved columns (n_cols=8 in every registered RepackKey), paired with
// plain (non-interleaved) Q8_K activation blocks for gemv -- reuse
// ggml_halide::q8_k_value directly, exactly like the plain vec_dot
// generators (q4_k_generators.cpp etc.) do, since gemv's activation side is
// bit-for-bit the same "one whole row, no repacking" block regardless of
// weight family.
//
// Each weight decode below is a direct, mechanical translation of the
// corresponding upstream ggml_gemv_*_generic (or its NxM_generic_impl
// template) in src/ggml-cpu/repack.cpp, working out which byte of the
// interleaved qs/scales/qh arrays a given (column j, element e) reads --
// see each function's own comment for the byte-layout reasoning. As with
// every other family in this file, this is a tolerance-checked benchmark,
// so weight and activation are fully dequantized and multiplied elementwise
// rather than replicating GGML's block-sum ("bsums") shortcut for the
// min-subtraction term.

constexpr int kQK_K = 256;
constexpr int kActBlockBytesK = 4 + kQK_K + 2 * (kQK_K / 16);  // plain block_q8_K, 292 bytes.

// Q4_K weight family (block_q4_Kx8, n_cols=8 always): header is d[8] (16
// bytes) + dmin[8] (16 bytes) + scales[96] (12 bytes/superblock-eighth,
// 8 columns interleaved per get_scale_min_k4 window -- see below), then
// qs[1024] (4-bit nibbles, same 4-iterations-of-64-elements structure as
// plain Q4_K, interleaved across columns exactly like Q4_0's qs).
//
// The scales array is NOT a plain byte permutation of the original 8
// columns' own scales[12]: make_block_q4_Kx8 (repack.cpp) re-packs each
// original sub-block's decoded (scale, min) pair for all 8 columns through
// the *same* get_scale_min_k4 bit scheme Q4_K itself uses, just with
// "column index" (0..7) standing in for what get_scale_min_k4 normally
// treats as "sub-block index" (0..7) -- i.e. get_scale_min_k4(j, window)
// recovers column j's own (scale, min) for whichever original sub-block
// `window` (one of 8 fixed 12-byte slices) corresponds to. This was
// verified by hand-expanding both the packer's bit formulas and
// get_scale_min_k4's decode formula and confirming they're inverses; the
// window for original sub-block `sb` is byte offset (sb/4)*48 + (sb%4)*12.
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

// Q5_K weight family (block_q5_Kx8): same column-interleaved
// get_scale_min_k4 scales scheme as Q4_K (see q4_k_family_weight_value's
// comment), plus a 5th (high) bit per value read from a qh[256] array --
// qh's own interleave uses the sub-block-relative position `lpos` (0..31,
// the same value plain Q5_K's own qh indexing uses, see q5_k_generators.cpp)
// as the thing being interleaved across columns, exactly like qs.
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

class Q5_K_8x4GemvGenerator : public Generator<Q5_K_8x4GemvGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 4;
        constexpr int kBlockBytes = 16 + 16 + 96 + (kQK_K * kNCols) / 8 + (kQK_K * kNCols * 4) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK_K, "r");

        s_(j, x) = sum(q5_k_family_weight_value(weight_blocks_, kBlockLen, j, x, r) *
                       ggml_halide::q8_k_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytesK);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

class Q5_K_8x8GemvGenerator : public Generator<Q5_K_8x8GemvGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 8;
        constexpr int kBlockBytes = 16 + 16 + 96 + (kQK_K * kNCols) / 8 + (kQK_K * kNCols * 4) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK_K, "r");

        s_(j, x) = sum(q5_k_family_weight_value(weight_blocks_, kBlockLen, j, x, r) *
                       ggml_halide::q8_k_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytesK);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

class Q4_K_8x4GemvGenerator : public Generator<Q4_K_8x4GemvGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 4;
        constexpr int kBlockBytes = 16 + 16 + 96 + (kQK_K * kNCols * 4) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK_K, "r");

        s_(j, x) = sum(q4_k_family_weight_value(weight_blocks_, kBlockLen, j, x, r) *
                       ggml_halide::q8_k_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytesK);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

class Q4_K_8x8GemvGenerator : public Generator<Q4_K_8x8GemvGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 8;
        constexpr int kBlockBytes = 16 + 16 + 96 + (kQK_K * kNCols * 4) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK_K, "r");

        s_(j, x) = sum(q4_k_family_weight_value(weight_blocks_, kBlockLen, j, x, r) *
                       ggml_halide::q8_k_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytesK);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

// Q6_K weight family (block_q6_Kx8, n_cols=8 always): a plain SIGNED int8
// per-sub-group scale (no compact bit-packing scheme, unlike Q4_K/Q5_K), so
// its interleaved scales[128] array is a direct byte permutation:
// scales[sub_group*8+j] holds column j's own scale for that sub-group --
// verified against ggml_gemv_q6_K_NxM_q8_K_generic_impl upstream, which
// reads `b_ptr[l].scales[scale_idx*ncols_interleaved+j]` directly, no
// unpacking. ql/qh interleave the same "k*n_cols*blocklen+j*blocklen+i" way
// as every other family; this function is a direct, mechanical translation
// of that same upstream function's base_l/base_h/qh_* index arithmetic
// (computed for both roles unconditionally, then select()ed on `is_high`)
// rather than an algebraically-simplified rederivation, to minimize the
// chance of a transcription error in this family's more involved indexing.
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

    // ql_pos/qh_offset_*/scale_idx_* below are arguments to select() and so
    // are evaluated unconditionally for every element (Halide's select()
    // isn't short-circuiting); each is clamped to its array's valid range so
    // bounds inference never requires an out-of-range read from the
    // not-taken branch. Same idiom as q4_0_generators.cpp's packed(...).
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

class Q6_K_8x4GemvGenerator : public Generator<Q6_K_8x4GemvGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 4;
        constexpr int kBlockBytes = 16 + 128 + (kQK_K * kNCols * 4) / 8 + (kQK_K * kNCols * 2) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK_K, "r");

        s_(j, x) = sum(q6_k_family_weight_value(weight_blocks_, kBlockLen, j, x, r) *
                       ggml_halide::q8_k_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytesK);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

class Q6_K_8x8GemvGenerator : public Generator<Q6_K_8x8GemvGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8, kBlockLen = 8;
        constexpr int kBlockBytes = 16 + 128 + (kQK_K * kNCols * 4) / 8 + (kQK_K * kNCols * 2) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK_K, "r");

        s_(j, x) = sum(q6_k_family_weight_value(weight_blocks_, kBlockLen, j, x, r) *
                       ggml_halide::q8_k_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytesK);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

// Q2_K weight family (block_q2_Kx8, n_cols=8, blocklen=8 -- the only
// registered variant, GGML has no ARM 8x4 path for this type). 2 bits per
// value, 4 values packed per byte (`sub` below, 0..3, selects which). Unlike
// Q6_K's scales, Q2_K's scale/min byte is a direct permutation too, but at
// the SAME interleaved byte position for both nibbles (scale = low nibble,
// min = high nibble of one shared byte) -- verified by independently
// working out ggml_gemv_q2_K_8x8_q8_K_generic's `scales_N[offset]` (scale)
// and its separate `mins[j*2(+1)]` (min) index arithmetic upstream and
// finding they resolve to the identical byte offset
// `half*64 + sub*16 + j*2 + (rem32>=16)`, matching plain Q2_K's own
// low-nibble-scale/high-nibble-min encoding (see q2_k_generators.cpp) --
// this repack step is a pure byte permutation, no bit re-packing.
Expr q2_k_family_weight_value(const Halide::GeneratorInput<Halide::Buffer<uint8_t, 3>> &blocks, Expr j, Expr x,
                              Expr r) {
    constexpr int kNCols = 8, kBlockLen = 8;
    constexpr int kDOffset = 0, kDminOffset = 16, kScalesOffset = 32, kQsOffset = 160;

    Expr l = r / kQK_K;
    Expr gi = r % kQK_K;

    Expr half = gi / 128;
    Expr local = gi % 128;
    Expr sub = local / 32;  // which of the 4 packed 2-bit values, 0..3
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

class Q2_K_8x8GemvGenerator : public Generator<Q2_K_8x8GemvGenerator> {
public:
    Input<Buffer<uint8_t, 3>> weight_blocks_{"weight_blocks"};
    Input<Buffer<uint8_t, 2>> act_blocks_{"act_blocks"};
    Output<Buffer<float, 2>> s_{"s"};

    void generate() {
        constexpr int kNCols = 8;
        constexpr int kBlockBytes = 16 + 16 + 128 + (kQK_K * kNCols * 2) / 8;

        Var j("j"), x("x");
        RDom r(0, weight_blocks_.dim(1).extent() * kQK_K, "r");

        s_(j, x) = sum(q2_k_family_weight_value(weight_blocks_, j, x, r) * ggml_halide::q8_k_value(act_blocks_, r));

        weight_blocks_.dim(0).set_bounds(0, kBlockBytes);
        weight_blocks_.dim(1).set_min(0);
        weight_blocks_.dim(2).set_min(0);
        act_blocks_.dim(0).set_bounds(0, kActBlockBytesK);
        act_blocks_.dim(1).set_min(0);
        s_.dim(0).set_bounds(0, kNCols);
        s_.dim(1).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q4_0_4x4GemvGenerator, q4_0_4x4_gemv)
HALIDE_REGISTER_GENERATOR(Q4_0_4x8GemvGenerator, q4_0_4x8_gemv)
HALIDE_REGISTER_GENERATOR(Q4_0_8x8GemvGenerator, q4_0_8x8_gemv)
HALIDE_REGISTER_GENERATOR(Q8_0_4x4GemvGenerator, q8_0_4x4_gemv)
HALIDE_REGISTER_GENERATOR(Q8_0_4x8GemvGenerator, q8_0_4x8_gemv)
HALIDE_REGISTER_GENERATOR(IQ4_NL_4x4GemvGenerator, iq4_nl_4x4_gemv)
HALIDE_REGISTER_GENERATOR(IQ4_NL_8x8GemvGenerator, iq4_nl_8x8_gemv)
HALIDE_REGISTER_GENERATOR(MXFP4_4x4GemvGenerator, mxfp4_4x4_gemv)
HALIDE_REGISTER_GENERATOR(MXFP4_8x8GemvGenerator, mxfp4_8x8_gemv)
HALIDE_REGISTER_GENERATOR(Q4_K_8x4GemvGenerator, q4_k_8x4_gemv)
HALIDE_REGISTER_GENERATOR(Q4_K_8x8GemvGenerator, q4_k_8x8_gemv)
HALIDE_REGISTER_GENERATOR(Q5_K_8x4GemvGenerator, q5_k_8x4_gemv)
HALIDE_REGISTER_GENERATOR(Q5_K_8x8GemvGenerator, q5_k_8x8_gemv)
HALIDE_REGISTER_GENERATOR(Q6_K_8x4GemvGenerator, q6_k_8x4_gemv)
HALIDE_REGISTER_GENERATOR(Q6_K_8x8GemvGenerator, q6_k_8x8_gemv)
HALIDE_REGISTER_GENERATOR(Q2_K_8x8GemvGenerator, q2_k_8x8_gemv)
