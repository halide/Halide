#pragma once

// Units shared by GGML's block formats: blocks of QK values, an fp32 scale per
// block stored as fp16, codes stored as integers; the layout stage comes from
// layouts.h. Rank-polymorphic: extra dimensions (rows) pass through as
// implicit vars.
#include "layouts.h"

#include <cmath>
#include <map>
#include <stdexcept>

namespace ggml {

using namespace Halide;

constexpr int QK = 32;

inline Expr inverse(const Expr &d) {
    return select(d != 0.0f, 1.0f / d, 0.0f);
}

// values = codes * scale, for codes(j, b, ...) and scale(b, ...).
inline Func dequant(const std::vector<Func> &e) {
    Func v("values");
    Var j, b;
    v(j, b, _) = cast<float>(e[0](j, b, _)) * e[1](b, _);
    return v;
}

// Error of at most `steps` quantization steps (plus float slack).
inline Func step_bound(const std::vector<Func> &e, double steps) {
    Func f("bound");
    Var j, b;
    f(j, b, _) = abs(cast<double>(e[1](b, _))) * Expr(steps * 1.0001);
    return f;
}

inline ApproximationSignature codes_and_scale(int lo, int hi) {
    return {{{"blocks", Float(32)}}, {{"codes", Int(8), std::nullopt, ApproximationRange(lo, hi)}, {"scale", Float(32)}}};
}

// Codes in [0, 2^bits - 1] <-> the same unsigned fields (GGML's q4_1 nibbles).
inline Approximation unsigned_codes(int bits) {
    return Pointwise{"unsigned", [](const Expr &x) { return cast<uint8_t>(x); }, [](const Expr &x) { return cast<int8_t>(x); }}
        .with_types(Int(8), UInt(8))
        .with_ranges(ApproximationRange(0, (1 << bits) - 1), ApproximationRange(0, (1 << bits) - 1))
        .with_lossless();
}

// Signed codes in [-bias, bias - 1] <-> unsigned fields in [0, 2 * bias - 1].
// With `shift`, codes kept at 2^shift times their value (multiples of 2^shift).
inline Approximation offset(int bias, int shift = 0) {
    int k = 1 << shift;
    return Pointwise{"offset", [=](const Expr &x) { return cast<uint8_t>((shift ? x >> shift : x) + bias); },
                     [=](const Expr &x) { return shift ? cast<int8_t>((x << shift) - cast<uint8_t>(bias * k)) : cast<int8_t>(cast<int>(x) - bias); }}
        .with_types(Int(8), UInt(8))
        .with_ranges(ApproximationRange(-bias * k, (bias - 1) * k), ApproximationRange(0, 2 * bias - 1))
        .with_lossless();
}

// Signed codes in [-2^(bits-1), 2^(bits-1) - 1] <-> their two's-complement
// `bits`-bit fields (GGML's repacked q4_0 nibbles: offset(8) XOR 8). With
// `shift`, codes kept at 2^shift times their value (multiples of 2^shift): at
// shift 8 - bits, a field's decode is its shift to the top of an int8.
inline Approximation twos(int bits, int shift = 0) {
    int h = 1 << (bits - 1), s = 8 - bits, k = 1 << shift;
    return Pointwise{"twos", [=](const Expr &x) { return cast<uint8_t>(shift ? x >> shift : x) & cast<uint8_t>(2 * h - 1); },
                     [=](const Expr &x) { Expr t = cast<int8_t>(x << s); return s > shift ? t >> (s - shift) : t; }}
        .with_types(Int(8), UInt(8))
        .with_ranges(ApproximationRange(-h * k, (h - 1) * k), ApproximationRange(0, 2 * h - 1))
        .with_lossless();
}

// The `n` codes lo + i * 2^shift <-> i, unsigned.
inline Approximation multiples(int shift, int lo, int n) {
    return Pointwise{"multiples", [=](const Expr &x) { return cast<uint8_t>(x - cast<int8_t>(lo)) >> shift; },
                     [=](const Expr &x) { return cast<int8_t>(x << shift) + cast<int8_t>(lo); }}
        .with_types(Int(8), UInt(8))
        .with_ranges(ApproximationRange(lo, lo + (n - 1) * (1 << shift)), ApproximationRange(0, n - 1))
        .with_lossless();
}

// Codes x <-> their low `low` bits, unsigned ("low"), and the rest, a
// multiple of 2^low ("high"): x = (x & ~m) + (x & m), so the decode is a sum
// (GGML's q5 codes: a nibble and a fifth bit, stored apart; the sum lets a
// table of the high parts fold in q5_0's offset). Shape-preserving.
struct SumSplit {
    int low;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func l("low"), h("high");
        l(_) = cast<uint8_t>(in[0](_)) & cast<uint8_t>((1 << low) - 1);
        h(_) = in[0](_) & cast<int8_t>(-(1 << low));
        return {l, h};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func f("codes");
        f(_) = e[1](_) + cast<int8_t>(e[0](_));
        return {f};
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        std::optional<int> dims = in.empty() ? std::nullopt : in[0].dimensions;
        std::optional<ApproximationRange> r = in.empty() ? std::nullopt : in[0].range, hr;
        if (r) hr = ApproximationRange(std::floor(r->lo / (1 << low)) * (1 << low), std::floor(r->hi / (1 << low)) * (1 << low));
        return {{{"codes", Int(8), dims, r}},
                {{"low", UInt(8), dims, ApproximationRange(0, (1 << low) - 1)}, {"high", Int(8), dims, hr}}};
    }
    bool lossless() const {
        return true;
    }
};

// `bits`-bit fields packed densely, least significant first: (element,
// record, ...) <-> (byte, record, ...), 8 / bits fields per byte, so with
// bits 1, bit j of the record's little-endian word is element j (GGML's qh).
struct BitPack {
    int bits;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        int per = 8 / bits;
        Func f("packed_bits");
        Var p, b;
        Expr e = cast<uint8_t>(0);
        for (int i = 0; i < per; i++) {
            e = e | cast<uint8_t>((in[0](p * per + i, b, _) & cast<uint8_t>((1 << bits) - 1)) << (i * bits));
        }
        f(p, b, _) = e;
        return {f};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        int per = 8 / bits;
        Func f("unpacked_bits");
        Var j, b;
        f(j, b, _) = (e[0](j / per, b, _) >> cast<uint8_t>(j % per * bits)) & cast<uint8_t>((1 << bits) - 1);
        return {f};
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        std::optional<int> dims = in.empty() ? std::nullopt : in[0].dimensions;
        return {{{"fields", UInt(8), dims, ApproximationRange(0, (1 << bits) - 1)}}, {{"bits", UInt(8), dims}}};
    }
    bool lossless() const {
        return true;
    }
};

// A record's bytes <-> one little-endian word of type `t` (as GGML reads
// q5's qh: memcpy to a uint32_t), so a kernel loads them at once.
// LittleEndianScalarPack's inverse.
struct Words {
    Type t;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        return Internal::little_endian_scalar_decode(t, in);
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        return Internal::little_endian_scalar_encode(t, e);
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        std::optional<int> dims = in.empty() ? std::nullopt : in[0].dimensions;
        return {{{"bytes", UInt(8), dims}}, {{"word", t, dims ? std::optional<int>(*dims - 1) : std::nullopt}}};
    }
    bool lossless() const {
        return true;
    }
};

// `inner`, a stage onto bytes whose decode's element j reads only byte
// j / per (e.g. Compose{multiples, BitPack}), with an exact alternative
// decode: a constant table of its decodes of each byte value (a row of `per`
// per value; GGML's table_b2b_*), built by running inner's decode. A
// schedule computing a row at a time reads it with one load: the decode
// moves off the SIMD units.
struct Tabulate {
    Approximation inner;
    int per;
    Buffer<> table;

    Tabulate(Approximation a, int per)
        : inner(std::move(a)), per(per) {
        Func bytes("bytes"), random("random");
        Var p, v;
        bytes(p, v) = cast<uint8_t>(v);
        Buffer<> t = inner.decode({bytes}).decoded[0].realize({per, 256});
        // one Buffer per table: a pipeline tells its Buffers apart by name
        static std::map<std::vector<uint8_t>, Buffer<>> tables;
        std::vector<uint8_t> key((const uint8_t *)t.data(), (const uint8_t *)t.data() + t.size_in_bytes());
        key.push_back((uint8_t)t.type().code()), key.push_back((uint8_t)t.type().bits());
        auto it = tables.find(key);
        if (it == tables.end()) {
            t.set_name("table" + std::to_string(tables.size()));
            it = tables.emplace(key, t).first;
        }
        table = it->second;
        // inner reads as tabulated: compare on records of 8 random bytes
        random(p, v) = cast<uint8_t>(random_uint());
        Func d = inner.decode({random}).decoded[0], t2 = tabulated({random})[0], diffs;
        RDom r(0, 8 * per, 0, 64);
        diffs() = sum(cast<int>(d(r.x, r.y) != t2(r.x, r.y)));
        Buffer<int> n = diffs.realize();
        if (n() != 0) throw std::invalid_argument("Tabulate: a decoded element reads more than its byte");
    }
    std::vector<Func> encode(const std::vector<Func> &in, const ApproximationPorts &ports) const {
        return inner.encode(in, ports).encoded;
    }
    std::vector<Func> decode(const std::vector<Func> &e, const ApproximationPorts &) const {
        return tabulated(e);
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        return inner.signature(in);
    }
    bool lossless() const {
        return inner.lossless();
    }

private:
    std::vector<Func> tabulated(const std::vector<Func> &e) const {
        Func f("tabulated");
        Var j, b;
        f(j, b, _) = table(j % per, cast<int>(e[0](j / per, b, _)));
        return {f};
    }
};

// x * k for a power of two k: exact for normal floats (and fp16's are).
inline Approximation scale_by(float k) {
    return Pointwise{"scale_by", [=](const Expr &x) { return x * k; }, [=](const Expr &x) { return x * (1.0f / k); }}
        .with_types(Float(32), Float(32))
        .with_lossless();
}

// Round to fp16 (to nearest even): within half an fp16 ulp, relative to x
// (2^-25 below fp16's normal range); finite inputs below 65520 only. The
// rounding is strict_float: with fast math, LLVM may otherwise fold away an
// in-register round trip f32(f16(x)), so a consumer in the same pipeline (a
// Derive reading the rounded scale) would see x unrounded.
inline Approximation fp16() {
    return Pointwise{"fp16", [](const Expr &x) { return strict_float(cast<float16_t>(x)); },
                     [](const Expr &x) { return cast<float>(x); }}
        .with_types(Float(32), Float(16))
        .with_error_bound([](const Expr &x) { return max(abs(cast<double>(x)) * Expr(std::ldexp(1.0, -11)), Expr(std::ldexp(1.0, -25))); });
}

}  // namespace ggml
