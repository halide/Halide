#pragma once

// GGML's grid formats (iq2_xxs, iq2_xs, iq2_s, iq3_xxs, iq3_s, iq1_s, iq1_m):
// super-blocks of QK_K values whose codes, in groups of 8 (iq3: 4), are the
// points of a lattice grid (Lattice: GGML's kgrid lists, their digits mapped
// to values by a codebook), times signs (the iq2 and iq3 formats: 7 bits per
// 8 values of even parity, EvenSigns, or 8 raw bits) or plus a +-1/8 delta
// per run (iq1: RunShift), times an odd sub-block scale times the fp16
// super-block scale. Decode (dequantize_row_iq*): a ScaledCodes stage, then
// lossless units down to GGML's block structs.
//
// The encode is GGML's quantizer itself, called as an extern stage
// (ExternQuantize: ggml_trusted_quantize, in the harness), paired with the
// decode by a TrustedInverse: GGML's quantizers search the grid's neighbours
// of each group (tables built at init by iq2xs_init_impl, iq3xs_init_impl)
// per trial scale, weighted by an importance matrix, which no plain
// composition of Halide units reproduces (see the README).
#include "common.h"
#include "grid_tables.h"
#include "kquants.h"

namespace ggml {

// values = codes * (scale * scales(j / n)): int8 codes, an int8 scale per
// sub-block of n, an f32 scale per block (Q6KQuant's decode). Decode only:
// pair it with an encoder (TrustedInverse).
struct ScaledCodes {
    int n, lo, hi, scales_lo, scales_hi;

    std::vector<Func> encode(const std::vector<Func> &) const {
        throw std::logic_error("ScaledCodes: decode only (an encoder pairs with it: TrustedInverse)");
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func v("values");
        Var j, b;
        v(j, b, _) = cast<float>(e[0](j, b, _)) * (e[2](b, _) * cast<float>(e[1](j / n, b, _)));
        return {v};
    }
    ApproximationSignature signature() const {
        return {{{"blocks", Float(32)}}, {codes_port(lo, hi), param_port("scales", Int(8), tag::scale, ApproximationRange(scales_lo, scales_hi)), param_port("scale", Float(32), tag::scale)}};
    }
};

// GGML's quantizer of `type` (ggml_quantize_chunk: with the harness's
// importance matrix where GGML requires one), as an extern stage, Halide's
// define_extern: values -> blocks of `block` values, GGML's `record`s.
// `indexed`: values (index in block, block, ...), else flat. Encode only.
struct ExternQuantize {
    std::string type;
    Type record;
    bool indexed;
    int block = QK_K;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x("trusted_values"), q("trusted_blocks");
        Var k;
        if (indexed) {
            x(k, _) = in[0](k % block, k / block, _);
        } else {
            x(_) = in[0](_);
        }
        x.compute_root();  // an extern stage reads its inputs from buffers
        q.define_extern("ggml_trusted_quantize", {Expr(type), x}, record, x.dimensions());
        return {q};
    }
    std::vector<Func> decode(const std::vector<Func> &) const {
        throw std::logic_error("ExternQuantize: encode only (a decoder pairs with it: TrustedInverse)");
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        std::optional<int> dims = in.empty() || !in[0].dimensions ? std::nullopt : std::optional<int>(*in[0].dimensions - indexed);
        return {{{"values", Float(32), in.empty() ? std::nullopt : in[0].dimensions}}, {{"blocks", record, dims}}};
    }
};

// A grid: groups of `per` codes, each a point of `points` (digits of `bits`
// bits) whose digits map to `levels`, <-> its index; decoded through a table
// of the points' codes (a row of `per` per point).
inline Approximation grid(const std::string &name, const std::vector<uint16_t> &points, int bits, int per, const std::vector<int> &levels) {
    Lattice l{name, points, bits, per};
    return Tabulate{Compose{codebook(name + "_levels", levels), l}, per, (int)points.size(), l.index()};
}

inline Approximation iq2_grid(const std::string &name, const std::vector<uint16_t> &points) {
    return grid(name, points, 2, 8, {8, 25, 43});
}

// Signs of each 8 codes, the 7 bits of even parity GGML keeps (ksigns_iq2xs),
// decoded through a table (Tabulate; as GGML's NEON reads keven_signs).
inline Approximation even_signs() {
    return Tabulate{Compose{sign_bits(), EvenSigns{}}, 8, 128};
}

// Raw sign bits, 8 per byte (iq2_s, iq3_s).
inline Approximation raw_signs() {
    return Compose{sign_bits(), BitPack{1}};
}

// The odd sub-block scales 2s + 1 <-> s.
inline Approximation odd_scales(int bits) {
    return multiples(1, 1, 1 << bits);
}

// GGML's struct `fields`, stored by the trusted encoder and read by the
// decoder (`logical`: in its port order); AoS only.
inline Approximation trusted(const std::string &type, const std::string &lay, bool indexed, const std::vector<StructField> &fields,
                             const std::vector<std::string> &logical, std::vector<Approximation> decoder) {
    if (lay != "aos") throw std::invalid_argument(type + ": the aos layout only");
    Type record = Type::Struct(fields);
    decoder.insert(decoder.begin(), BlockReshape{QK_K, indexed});
    decoder.push_back(StructLayout{record, logical});
    return TrustedInverse{ExternQuantize{type, record, indexed}, Compose{decoder}};
}

// block_iq2_xxs {f16 d; u16 qs[32]}: per 32 values, 8 bytes (a u64): the 4
// grid indices (kgrid_2bit_256) in bytes 0-3, the 4 sign indices at bits 32 +
// 7l, the scale s (2s + 1) at bit 60; d * 8 in d.
inline Approximation iq2_xxs(const std::string &lay, bool indexed = false) {
    return trusted("iq2_xxs", lay, indexed, {{"d", Float(16)}, {"qs", UInt(64), QK_K / 32}}, {"qs", "d"},
                   {ScaledCodes{32, -43, 43, 1, 31},
                    Parallel{{"codes", Compose{SignSplit{}, Parallel{iq2_grid("kgrid_2bit_256", kgrid_2bit_256()), even_signs()}}},
                             {"scales", odd_scales(4)},
                             {"scale", Compose{scale_by(8), fp16()}}},
                    Parallel{BitFields{UInt(64), {{"grid", 8, 4, 0, 8}, {"signs", 7, 4, 32, 7}, {"scales", 4, 1, 60}}}, Identity{}}});
}

// block_iq2_xs {f16 d; u16 qs[32]; u8 scales[8]}: per 8 values a u16, the
// grid index (kgrid_2bit_512) in bits 0-8, the sign index in 9-15; scales
// per 16 values, nibbles (low first); d * 8 in d.
inline Approximation iq2_xs(const std::string &lay, bool indexed = false) {
    return trusted("iq2_xs", lay, indexed, {{"d", Float(16)}, {"qs", UInt(16), QK_K / 8}, {"scales", UInt(8), QK_K / 32}}, {"qs", "scales", "d"},
                   {ScaledCodes{16, -43, 43, 1, 31},
                    Parallel{{"codes", Compose{SignSplit{}, Parallel{iq2_grid("kgrid_2bit_512", kgrid_2bit_512()), even_signs()}}},
                             {"scales", Compose{odd_scales(4), BitPack{4}}},
                             {"scale", Compose{scale_by(8), fp16()}}},
                    Parallel{BitFields{UInt(16), {{"grid", 9, 1, 0, 0, UInt(16)}, {"signs", 7, 1, 9}}}, Identity{}, Identity{}}});
}

// block_iq2_s {f16 d; u8 qs[64]; u8 qh[8]; u8 scales[8]}: per 8 values the
// grid index's (kgrid_2bit_1024) low byte in qs[0:32], its top 2 bits in qh
// (4 per byte), the 8 sign bits in qs[32:64]; scales and d as iq2_xs'.
inline Approximation iq2_s(const std::string &lay, bool indexed = false) {
    return trusted("iq2_s", lay, indexed, {{"d", Float(16)}, {"qs", UInt(8), QK_K / 8}, {"signs", UInt(8), QK_K / 8}, {"qh", UInt(8), QK_K / 32}, {"scales", UInt(8), QK_K / 32}},
                   {"qs", "qh", "signs", "scales", "d"},
                   {ScaledCodes{16, -43, 43, 1, 31},
                    Parallel{{"codes", Compose{SignSplit{}, Parallel{Compose{iq2_grid("kgrid_2bit_1024", kgrid_2bit_1024()), BitSplit{8}, Parallel{Identity{}, BitPack{2}}}, raw_signs()}}},
                             {"scales", Compose{odd_scales(4), BitPack{4}}},
                             {"scale", Compose{scale_by(8), fp16()}}}});
}

// block_iq3_xxs {f16 d; u8 qs[96]}: per 4 values a grid index (kgrid_256,
// digits 4 + 8i, the top 62) in qs[0:64] (on CPUs, the schedule reads them a
// word at a time: staged_indices()); per 32 values a u32 in qs[64:96], the 4
// sign indices (per 8) at bits 7l, the scale s (2s + 1) at bit 28; d * 4 in d.
inline Approximation iq3_xxs(const std::string &lay, bool indexed = false) {
    return trusted("iq3_xxs", lay, indexed, {{"d", Float(16)}, {"qs", UInt(8), QK_K / 4}, {"scales_and_signs", UInt(32), QK_K / 32}},
                   {"qs", "scales_and_signs", "d"},
                   {ScaledCodes{32, -62, 62, 1, 31},
                    Parallel{{"codes", Compose{SignSplit{}, Parallel{grid("kgrid_256", kgrid_256(), 3, 4, {4, 12, 20, 28, 36, 44, 52, 62}), even_signs()}}},
                             {"scales", odd_scales(4)},
                             {"scale", Compose{scale_by(4), fp16()}}},
                    Parallel{Identity{}, BitFields{UInt(32), {{"signs", 7, 4, 0, 7}, {"scales", 4, 1, 28}}}, Identity{}}});
}

// block_iq3_s {f16 d; u8 qs[64]; u8 qh[8]; u8 signs[32]; u8 scales[4]}: per
// 4 values the grid index's (kgrid_512, odd digits 1-15) low byte in qs, its
// top bit in qh (8 per byte); the sign bits; scales per 32 values, nibbles;
// d itself.
inline Approximation iq3_s(const std::string &lay, bool indexed = false) {
    return trusted("iq3_s", lay, indexed, {{"d", Float(16)}, {"qs", UInt(8), QK_K / 4}, {"qh", UInt(8), QK_K / 32}, {"signs", UInt(8), QK_K / 8}, {"scales", UInt(8), QK_K / 64}},
                   {"qs", "qh", "signs", "scales", "d"},
                   {ScaledCodes{32, -15, 15, 1, 31},
                    Parallel{{"codes", Compose{SignSplit{}, Parallel{Compose{grid("kgrid_512", kgrid_512(), 3, 4, {1, 3, 5, 7, 9, 11, 13, 15}), BitSplit{8}, Parallel{Identity{}, BitPack{1}}}, raw_signs()}}},
                             {"scales", Compose{odd_scales(4), BitPack{4}}},
                             {"scale", fp16()}}});
}

// iq1's codes: grid values -1, 0, 1 (kgrid_1bit_2048) at 8 times their
// value, shifted by +-1 (GGML's delta, +-1/8) per run of n: the grid index's
// low byte, its top 3 bits, the shift's sign bit per run.
inline Approximation iq1_codes(int n) {
    return Compose{RunShift{n, 8}, Parallel{Compose{grid("kgrid_1bit_2048", kgrid_1bit_2048(), 2, 8, {-1, 0, 1}), BitSplit{8}}, sign_bits()}};
}

// block_iq1_s {f16 d; u8 qs[32]; u16 qh[8]}: per 8 values the grid index's
// low byte in qs; per 32 a u16, the 4 indices' top 3 bits at 3l, the scale s
// (2s + 1) at bit 12, the delta's sign at 15; d * 8 in d.
inline Approximation iq1_s(const std::string &lay, bool indexed = false) {
    return trusted("iq1_s", lay, indexed, {{"d", Float(16)}, {"qs", UInt(8), QK_K / 8}, {"qh", UInt(16), QK_K / 32}}, {"qs", "qh", "d"},
                   {ScaledCodes{32, -9, 9, 1, 15},
                    Parallel{{"codes", iq1_codes(32)}, {"scales", odd_scales(3)}, {"scale", Compose{scale_by(8), fp16()}}},
                    Parallel{Identity{}, BitFields{UInt(16), {{"high", 3, 4, 0, 3}, {"shifts", 1, 1, 15}, {"scales", 3, 1, 12}}}, Identity{}}});
}

// block_iq1_m {u8 qs[32]; u8 qh[16]; u8 scales[8]}: qs as iq1_s'; per 16
// values a byte of qh, the 2 indices' top 3 bits and their delta signs (per
// 8 values) at bits 0-3 and 4-7; scales per 16 values, 4 per u16 at bits 3i,
// and the fp16 bits of d * 8, a nibble at the top of each u16.
inline Approximation iq1_m(const std::string &lay, bool indexed = false) {
    return trusted("iq1_m", lay, indexed, {{"qs", UInt(8), QK_K / 8}, {"qh", UInt(8), QK_K / 16}, {"scales", UInt(16), QK_K / 64}}, {"qs", "qh", "scales"},
                   {ScaledCodes{16, -9, 9, 1, 15},
                    Parallel{{"codes", iq1_codes(8)}, {"scales", odd_scales(3)}, {"scale", Compose{scale_by(8), fp16(), fp16_bits(), BlockReshape{1}, Inverse{BitFields{UInt(16), {{"d", 4, 4, 0, 4}}}}}}},
                    Parallel{Identity{}, BitFields{UInt(8), {{"high", 3, 2, 0, 4}, {"shifts", 1, 2, 3, 4}}}, BitFields{UInt(16), {{"scales", 3, 4, 0, 3}, {"d", 4, 1, 12}}}}});
}

}  // namespace ggml
