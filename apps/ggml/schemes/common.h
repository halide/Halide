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

// ---------------------------------------------------------------------------
// MiniFloat: a generic float format unit, independent of GGML (a candidate
// for the core: it uses nothing below this section).
// ---------------------------------------------------------------------------

// f32 values rounded to a float of `exp` exponent bits (bias `bias`, at most
// f32's 127) and `man` mantissa bits (with gradual underflow: subnormals),
// with a sign bit if `is_signed`, stored as its bits in the low bits of an
// unsigned field (sign, exponent, mantissa), or lowered to Halide's float16_t
// or bfloat16_t where it is one of theirs (IEEE binary16, bfloat16: to
// nearest even, overflow to inf). Encode rounds the magnitude by `rounding`
// (Down, Up: toward -inf, +inf); a rounded magnitude past the largest finite
// value `overflow`s: to inf (Infinity; NaN for NaNOnly formats, which have no
// inf) or to the largest finite value (Saturate). `specials`: IEEE (the top
// exponent is inf and NaN; a NaN keeps its payload's top bits, quieted, as
// hardware converts), NaNOnly (only the all-ones magnitude is NaN, as OCP's
// e4m3fn and e8m0) or None (every code finite; NaN inputs unspecified).
// Unsigned formats encode x <= 0 as 0. A format with no mantissa (e8m0) has
// no zero or subnormals: code 0 is 2^-bias. Decode is exact (NaNs quieted;
// values past f32's range, inf), and within half an ulp of x (rounding to
// nearest; else an ulp) for finite x up to the largest finite value (unsigned:
// x >= 0).
struct MiniFloat {
    enum class Specials { IEEE,
                          NaNOnly,
                          None };
    enum class Overflow { Infinity,
                          Saturate };
    enum class Rounding { NearestEven,
                          NearestAway,
                          TowardZero,
                          Down,
                          Up };

    int exp, man, bias;
    bool is_signed = true;
    Specials specials = Specials::IEEE;
    Overflow overflow = Overflow::Infinity;
    Rounding rounding = Rounding::NearestEven;

    static MiniFloat binary16() {
        return {5, 10, 15};
    }
    static MiniFloat bfloat16() {
        return {8, 7, 127};
    }

    // "fp16", "bf16", else e.g. "e4m3" ("ue8m0": unsigned).
    std::string name() const {
        if (native_fp16()) return "fp16";
        if (native_bf16()) return "bf16";
        return std::string(is_signed ? "" : "u") + "e" + std::to_string(exp) + "m" + std::to_string(man);
    }
    int bits() const {
        return is_signed + exp + man;
    }
    // The bits' field, whatever the stored type.
    Type field_type() const {
        return UInt(bits() <= 8 ? 8 : bits() <= 16 ? 16 :
                                                     32);
    }
    // The stored type: Halide's float where there is one, else the field.
    Type type() const {
        return native_fp16() ? Float(16) : native_bf16() ? BFloat(16) :
                                                           field_type();
    }

    // The unit: one Func in (f32), one out (type()), elementwise.
    Func encode(const Func &in) const {
        return pointwise().encode(in);
    }
    Func decode(const Func &in) const {
        return pointwise().decode(in);
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        return pointwise().signature(in);
    }
    Func error_bound(const std::vector<Func> &in, const std::vector<Func> &e) const {
        return pointwise().error_bound(in, e);
    }

    // The element-wise conversions.
    Expr encode(const Expr &x) const {
        // strict_float: with fast math, LLVM may otherwise fold away an
        // in-register round trip f32(f16(x)), so a consumer in the same
        // pipeline (one reading the rounded value) would see x unrounded.
        if (native_fp16()) return strict_float(cast<float16_t>(x));
        // (Halide's own f32 -> bf16 does not quiet NaNs: the bits instead)
        if (native_bf16()) return reinterpret(BFloat(16), encode_bits(x));
        return encode_bits(x);
    }
    Expr decode(const Expr &f) const {
        if (native_fp16()) return cast<float>(f);
        // (as bits, so Halide's bf16 emulation does not warn: the same shift)
        if (native_bf16()) return decode_bits(reinterpret<uint16_t>(f));
        return decode_bits(f);
    }

    // The bits, from f32's: its mantissa rounded at our last bit, its exponent
    // rebiased (below our normal range: its mantissa, with the implicit bit,
    // shifted to our subnormals'; f32's subnormals as exponent 1).
    Expr encode_bits(const Expr &x) const {
        check();
        const int shift = 23 - man, off = 127 - bias;
        const uint32_t emax = (1u << exp) - 1, mmask = (1u << man) - 1, magnitudes = (1u << (exp + man)) - 1;
        const uint32_t top = specials == Specials::IEEE ? (emax << man) - 1 : specials == Specials::NaNOnly ? magnitudes - 1 :
                                                                                                              magnitudes;
        const bool wide = exp < 8 || off;  // not f32's exponents
        // (NaNs and signs from the bits: fast math may assume no NaNs)
        Expr u = reinterpret<uint32_t>(cast<float>(x)), a = u & u32(0x7fffffff), neg = (u >> 31) == u32(1);
        Expr nan = a > u32(0x7f800000);
        auto rounded = [&](const Expr &v, const Expr &sh) {
            Expr half = u32(1) << (sh - 1), all = (u32(1) << sh) - 1, r;
            switch (rounding) {
            case Rounding::NearestEven:
                r = half - 1 + ((v >> sh) & 1);
                break;
            case Rounding::NearestAway:
                r = half;
                break;
            case Rounding::TowardZero:
                r = u32(0);
                break;
            case Rounding::Down:
                r = select(neg, all, u32(0));
                break;
            case Rounding::Up:
                r = select(neg, u32(0), all);
                break;
            }
            return (v + r) >> sh;
        };
        Expr t = shift ? rounded(a, u32(shift)) : a;
        if (off) t -= u32((uint32_t)off << man);
        if (wide) {
            Expr e = a >> 23, mm = (a & u32(0x7fffff)) | select(e > 0, u32(0x800000), u32(0));
            Expr sh = cast<uint32_t>(clamp((151 - bias - man) - max(cast<int>(e), 1), 1, 25));
            t = select(a < u32((uint32_t)(128 - bias) << 23), rounded(mm, sh), t);
        }
        // past the largest finite magnitude (f32's exponents with IEEE
        // specials: inf as is)
        uint32_t past = overflow == Overflow::Saturate ? top : specials == Specials::IEEE ? emax << man :
                                                                                            magnitudes;
        if (wide || specials != Specials::IEEE || overflow == Overflow::Saturate) t = select(t > u32(top), u32(past), t);
        if (specials == Specials::IEEE && man) {
            t = select(nan, u32(emax << man) | ((a >> shift) & mmask) | u32(1u << (man - 1)), t);
        } else if (specials == Specials::NaNOnly) {
            t = select(nan, u32(magnitudes), t);
        }
        if (is_signed) return cast(field_type(), t | ((u >> 31) << (exp + man)));
        Expr keep = !neg && a != u32(0);
        if (specials != Specials::None) keep = keep || nan;
        return cast(field_type(), select(keep, t, u32(0)));
    }
    // f32's bits, from ours: the exponent rebiased, the mantissa shifted;
    // subnormals as mantissa * 2^(1 - bias - man).
    Expr decode_bits(const Expr &f) const {
        check();
        const int shift = 23 - man, off = 127 - bias;
        const uint32_t emax = (1u << exp) - 1, mmask = (1u << man) - 1, magnitudes = (1u << (exp + man)) - 1;
        const bool wide = exp < 8 || off;
        Expr b = cast<uint32_t>(f), e = (b >> man) & u32(emax), m = b & u32(mmask);
        // (f32's exponents: its top bits)
        Expr v = wide ? ((off ? e + u32(off) : e) << 23) | (m << shift) : b << shift;
        if (!man) {
            v = select(e == u32(0), reinterpret<uint32_t>(Expr((float)std::ldexp(1.0, -bias))), v);
        } else if (wide) {
            v = select(e == u32(0), reinterpret<uint32_t>(cast<float>(m) * (float)std::ldexp(1.0, 1 - bias - man)), v);
        }
        // (NaNs quieted, as hardware widens fp16; on f32's exponents, as is)
        if (specials == Specials::IEEE && wide) {
            v = select(e == u32(emax), u32(0x7f800000) | (m << shift) | select(m != 0, u32(0x400000), u32(0)), v);
        } else if (specials == Specials::NaNOnly) {
            v = select((b & u32(magnitudes)) == u32(magnitudes), u32(0x7fc00000), v);
        }
        // (f32's exponents: the sign in place, but in NaNOnly's NaN)
        if (is_signed && (wide || specials == Specials::NaNOnly)) v = v | ((b >> (exp + man)) << 31);
        return reinterpret<float>(v);
    }

private:
    bool native(int e, int m, int b) const {
        return exp == e && man == m && bias == b && is_signed && specials == Specials::IEEE && overflow == Overflow::Infinity && rounding == Rounding::NearestEven;
    }
    bool native_fp16() const {
        return native(5, 10, 15);
    }
    bool native_bf16() const {
        return native(8, 7, 127);
    }
    bool nearest() const {
        return rounding == Rounding::NearestEven || rounding == Rounding::NearestAway;
    }
    static Expr u32(uint32_t v) {
        return Internal::make_const(UInt(32), v);
    }
    void check() const {
        if (exp < 1 || exp > 8 || man < 0 || man > 23 || bias > 127 || bits() > 32 || (!man && is_signed) ||
            (!man && specials == Specials::IEEE) || (specials == Specials::None && overflow == Overflow::Infinity)) {
            throw std::invalid_argument("MiniFloat: unsupported format " + name());
        }
    }
    // Half an ulp (an ulp, if not rounding to nearest), relative to x; below
    // the normal range, of the least subnormal.
    Pointwise pointwise() const {
        check();
        MiniFloat mf = *this;
        int k = nearest() ? 1 : 0;
        return Pointwise{name(), [=](const Expr &x) { return mf.encode(x); }, [=](const Expr &x) { return mf.decode(x); }}
            .with_types(Float(32), type())
            .with_error_bound([=](const Expr &x) { return max(abs(cast<double>(x)) * Expr(std::ldexp(1.0, -(mf.man + k))), Expr(std::ldexp(1.0, 1 - k - (mf.bias + mf.man)))); });
    }
};

// ---------------------------------------------------------------------------

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
// rounding is strict_float (see MiniFloat::encode).
inline Approximation fp16() {
    return MiniFloat::binary16();
}

// Round to bf16 (to nearest even; NaNs quieted, keeping their payload's top
// bits: GGML's ggml_compute_fp32_to_bf16): within half a bf16 ulp, relative
// to x.
inline Approximation bf16() {
    return MiniFloat::bfloat16();
}

}  // namespace ggml
