// From-scratch Halide reimplementation of GGML's Q8_K quantize kernel (see
// src/ggml-quants.c: quantize_row_q8_K_ref upstream, as of GGML v0.15.3). No
// GGML headers are used here -- this file encodes its own understanding of
// the 292-byte block_q8_K layout (a 256-element superblock):
//
//   byte 0-3:     float32 delta 'd' (NOT fp16, unlike every other type here)
//   byte 4-259:   256 signed int8 values, one byte per value
//   byte 260-291: 16 signed int16 "bsums" (little-endian), the sum of qs
//                 within each contiguous group of 16 values
//
// Q8_K is an activation-only format: GGML has no public to_float for it (see
// include/ggml.h's type_traits table), so there's nothing to dequantize
// against and no dequantize generator here -- same situation as Q8_1.
//
// GGML's rounding here is nearest_int(), a magic-number float trick that
// relies on the default IEEE-754 round-to-nearest-even addition to produce a
// round-to-nearest-even integer -- NOT the same as roundf() (round half away
// from zero, used by Q8_0/Q8_1) or the truncating "+0.5f then cast" trick
// (used by the nibble-packed legacy types). Halide has no round-to-even
// primitive exposed, so the exact same bit trick is reproduced here to match
// bit-for-bit.
//
// This is intentionally unscheduled beyond the minimum Halide requires for
// legality (an update-defined Func can't stay inline) -- scheduling for
// performance is a later step.

#include "Halide.h"

using namespace Halide;

namespace {

constexpr int kQK_K = 256;
constexpr int kGroupSize = 16;
constexpr int kNumGroups = kQK_K / kGroupSize;           // 16
constexpr int kBlockBytes = 4 + kQK_K + kNumGroups * 2;  // 292

// Same magic-number trick as GGML's static inline nearest_int() in
// src/ggml-quants.c: adding 1.5*2^23 forces the CPU's default round-to-
// nearest-even addition to round fval's fractional part, then the rounded
// integer is recovered from the float's mantissa bits.
Expr nearest_int(Expr fval) {
    Expr val = fval + 12582912.0f;  // 1.5 * 2^23
    Expr bits = reinterpret<int32_t>(val);
    return (bits & 0x007fffff) - 0x00400000;
}

class Q8_KQuantizeGenerator : public Generator<Q8_KQuantizeGenerator> {
public:
    Input<Buffer<float, 1>> x_{"x"};
    // dim 0: byte-within-block (extent kBlockBytes), dim 1: block index.
    Output<Buffer<uint8_t, 2>> blocks_{"blocks"};

    void generate() {
        Var i("i"), j("j"), byte("byte");
        RDom r(0, kQK_K, "r");

        // Per-block reduction: track the signed value with the largest
        // magnitude seen so far (same "argmax"-style idiom as Q4_0).
        Func stat("stat");
        stat(i) = Tuple(0.0f, 0.0f);  // {amax, max}
        Expr v = x_(i * kQK_K + r);
        Expr take = abs(v) > stat(i)[0];
        stat(i) = Tuple(select(take, abs(v), stat(i)[0]),
                        select(take, v, stat(i)[1]));
        stat.compute_root();

        // amax == 0 implies every element in the block is exactly 0 (GGML's
        // special-cased "all zero" block: d = 0, qs all zero, skipping the
        // iscale computation that would otherwise divide 0/0).
        Expr is_zero = stat(i)[0] == 0.0f;
        Expr max_val = stat(i)[1];
        // GGML computes d as 1/iscale -- a second, separate division -- not
        // the algebraically-equivalent max/-127 in one step. Floating point
        // isn't associative, so these round differently in the last bit;
        // reproduce the exact two-step computation to stay bit-exact.
        Expr iscale = select(is_zero, 0.0f, -127.0f / max_val);
        Expr d = select(is_zero, 0.0f, 1.0f / iscale);

        Func qval("qval");  // 256 payload bytes per block, j in [0, 256)
        Expr q_raw = nearest_int(iscale * x_(i * kQK_K + j));
        Expr q_clamped = min(127, q_raw);
        qval(j, i) = select(is_zero, 0, q_clamped);
        qval.compute_root();  // referenced again below by the bsums reduction

        Func bsums("bsums");  // 16 groups of 16, g in [0, 16)
        Var gv("gv");
        RDom rg(0, kGroupSize, "rg");
        bsums(gv, i) = 0;
        bsums(gv, i) += qval(gv * kGroupSize + rg, i);
        bsums.compute_root();

        Expr d_bits = reinterpret<uint32_t>(d);
        Expr qs_byte = reinterpret<uint8_t>(cast<int8_t>(qval(clamp(byte - 4, 0, kQK_K - 1), i)));

        Expr bsums_rel = clamp(byte - (4 + kQK_K), 0, kNumGroups * 2 - 1);
        Expr bsums_group = bsums_rel / 2;
        Expr bsums_is_lo = (bsums_rel % 2) == 0;
        Expr bsums_bits = reinterpret<uint16_t>(cast<int16_t>(bsums(bsums_group, i)));
        Expr bsums_byte = cast<uint8_t>(select(bsums_is_lo, bsums_bits & 0xff, (bsums_bits >> 8) & 0xff));

        blocks_(byte, i) = select(
            byte < 4, cast<uint8_t>((d_bits >> (cast<uint32_t>(clamp(byte, 0, 3)) * 8)) & 0xff),
            byte < 4 + kQK_K, qs_byte,
            bsums_byte);

        blocks_.dim(0).set_bounds(0, kBlockBytes);
        blocks_.dim(1).set_min(0);
        x_.dim(0).set_min(0);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(Q8_KQuantizeGenerator, q8_k_quantize)
