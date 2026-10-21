#pragma once

// Units shared by GGML's block formats: blocks of QK values, an fp32 scale per
// block stored as fp16, codes stored as integers; the layout stage comes from
// layouts.h. Rank-polymorphic: extra dimensions (rows) pass through as
// implicit vars.
#include "layouts.h"
#include "tags.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
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

// Accumulate: a reduction (a Func with update definitions, e.g. a sum)
// computed at the narrower float `type` (Float(16), BFloat(16)): its value
// and every partial sum rounded to `type`, in the order the reduction adds
// them (as scheduled: rfactor and split decide that order, so this unit is
// applied to the Funcs whose partial sums are to be rounded). Implemented as
// Func::change_type(type, unsafe = true): change_type's check proves exact
// integer accumulations only, so this unit is what declares the value
// change. Encode retypes the reduction (its update schedules kept; the input
// Func becomes change_type's cast-back wrapper of it, which approximate_by
// leaves without consumers), decode widens it back to `from`. Lossy; no
// error_bound Func: with u = 2^-(mantissa bits + 1), a sum of n terms is
// within ((1 + u)^n - 1) * sum |term| of the exact one (each partial sum
// rounded once: change_type fuses a product of `type` values into it, an
// fma, as fmla), until a partial sum passes `type`'s largest finite value
// (inf). The rounding order is the schedule's, so it needs strict_float.
struct Accumulate {
    Type type, from = Float(32);

    std::string name() const {
        return "accumulate_" + std::string(type.is_bfloat() ? "bf" : "f") + std::to_string(type.bits());
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        ApproximationPort i = in.empty() ? ApproximationPort("values") : in[0];
        i.type = from;
        return {{i}, {ApproximationPort("partials", type, i.dimensions)}};
    }
    Func encode(const Func &in) const {
        if (!type.is_float() || in.type() != from || !in.has_update_definition()) {
            throw std::invalid_argument("Accumulate: a reduction of " + type_name(from) + " values, to a float type");
        }
        return Func(in).change_type(type, /*unsafe=*/true);
    }
    Func decode(const Func &e) const {
        Func d(e.name() + "_wide");
        d(_) = cast(from, e(_));
        return d;
    }

private:
    static std::string type_name(Type t) {
        std::ostringstream s;
        s << t;
        return s.str();
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

// A quantizer's outputs: its codes in [lo, hi] and the parameters of its
// blocks, each tagged with what it is.
inline ApproximationPort codes_port(int lo, int hi) {
    return {"codes", Int(8), std::nullopt, ApproximationRange(lo, hi), ApproximationTags{tag::codes}};
}
inline ApproximationPort param_port(const std::string &name, Type t, const std::string &what, std::optional<ApproximationRange> range = std::nullopt) {
    return {name, t, std::nullopt, range, ApproximationTags{what}};
}
inline ApproximationSignature codes_and_scale(int lo, int hi) {
    return {{{"blocks", Float(32)}}, {codes_port(lo, hi), param_port("scale", Float(32), tag::scale)}};
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

// The values of a MiniFloat `f` with neither inf nor NaN (Specials::None),
// all integers once scaled by 2^(bias + man - 1), the inverse of its least
// subnormal (e2m1: OCP MX's FP4 values, doubled), as int8 codes <-> f's
// fields. Lossless on those codes; others round as f does. The decode is
// integer arithmetic: the mantissa with its implicit bit, shifted by the
// exponent field less one; for subnormals (exponent field 0), the magnitude's
// bits themselves. The lesser of the two picks between them.
inline Approximation minifloat_integers(const MiniFloat &f) {
    const int scale = f.bias + f.man - 1, emax = (1 << f.exp) - 1, top = ((2 << f.man) - 1) << (emax - 1);
    if (f.specials != MiniFloat::Specials::None || f.bits() > 8 || scale < 0 || top > 127) {
        throw std::invalid_argument("minifloat_integers: no int8 codes for " + f.name());
    }
    auto u8 = [](int v) { return cast<uint8_t>(v); };
    return Pointwise{f.name() + "_integers", [=](const Expr &x) { return f.encode_bits(cast<float>(x) * (float)std::ldexp(1.0, -scale)); },
                     [=](const Expr &x) {
                         Expr a = x & u8((1 << (f.exp + f.man)) - 1), m = (x & u8((1 << f.man) - 1)) | u8(1 << f.man);
                         Expr mag = cast<int8_t>(min(a, m) << saturating_sub(a >> f.man, u8(1)));
                         return f.is_signed ? select(x >= u8(1 << (f.exp + f.man)), -mag, mag) : mag;
                     }}
        .with_types(Int(8), UInt(8))
        .with_ranges(ApproximationRange(f.is_signed ? -top : 0, top), ApproximationRange(0, (1 << f.bits()) - 1))
        .with_lossless();
}

// A codebook: int8 codes drawn from a table of n <= 256 `values` <-> their
// indices in it, unsigned (GGML's kvalues_mxfp4, kvalues_iq4nl), the first
// index for a value the table repeats. The decode is a lookup in the constant
// table (a mux of constants: a register table lookup, e.g. NEON's tbl, where
// the target has one); the encode compares against each value in turn.
// Lossless on the table's values; others encode to index 0.
inline Approximation codebook(const std::string &name, const std::vector<int> &values) {
    const int n = (int)values.size();
    if (n < 2 || n > 256) {
        throw std::invalid_argument("codebook: " + name + " has " + std::to_string(n) + " values");
    }
    std::vector<Expr> table;
    for (int v : values) {
        if (v < -128 || v > 127) {
            throw std::invalid_argument("codebook: " + name + " has a value outside int8: " + std::to_string(v));
        }
        table.push_back(cast<int8_t>(v));
    }
    auto [lo, hi] = std::minmax_element(values.begin(), values.end());
    return Pointwise{name, [=](const Expr &x) {
                         Expr i = cast<uint8_t>(0);
                         for (int k = n - 1; k >= 0; k--) {
                             i = select(x == table[k], cast<uint8_t>(k), i);
                         }
                         return i; },
                     [=](const Expr &x) { return mux(x, table); }}
        .with_types(Int(8), UInt(8))
        .with_ranges(ApproximationRange(*lo, *hi), ApproximationRange(0, n - 1))
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

// Records of n elements back to one flat dimension: (i, o, rest...) <->
// (o * n + i, rest...), BlockReshape(n)'s inverse (e.g. a field's bytes
// packed in pieces, then stored as one array).
struct Unblock {
    int n;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func f("unblocked");
        Var k;
        f(k, _) = in[0](k % n, k / n, _);
        return {f};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func f("reblocked");
        Var i, o;
        f(i, o, _) = e[0](o * n + i, _);
        return {f};
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        ApproximationPort i = in.empty() ? ApproximationPort("records") : in[0], o = i;
        if (i.dimensions) o.dimensions = *i.dimensions - 1;
        return {{i}, {o}};
    }
    bool lossless() const {
        return true;
    }
};

// A record's elements in consecutive runs of the given sizes, each run its
// own records, under its own name: (element, record, ...) <-> one output per
// run, (element - start, record, ...) (GGML's block_tq1_0: runs of 160, 80
// and 16 values, each packed its own way). The decode concatenates them.
struct Segments {
    std::vector<std::pair<std::string, int>> runs;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        std::vector<Func> out;
        Var j;
        int start = 0;
        for (const auto &[name, n] : runs) {
            Func f(name);
            f(j, _) = in[0](j + start, _);
            out.push_back(f);
            start += n;
        }
        return out;
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func f("concatenated");
        Var j;
        int start = 0;
        for (const auto &r : runs) {
            start += r.second;
        }
        Expr v;
        for (int i = (int)runs.size() - 1; i >= 0; i--) {
            start -= runs[i].second;
            Expr ei = e[i](clamp(j - start, 0, runs[i].second - 1), _);
            v = v.defined() ? select(j < start + runs[i].second, ei, v) : ei;
        }
        f(j, _) = v;
        return {f};
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        ApproximationPort i = in.empty() ? ApproximationPort("elements") : in[0];
        ApproximationPorts out;
        for (const auto &r : runs) {
            ApproximationPort o = i;
            o.name = r.first;
            out.push_back(o);
        }
        return {{i}, out};
    }
    bool lossless() const {
        return true;
    }
};

// Base-3 fields (in [0, 2]) packed `trits` (<= 5; 3^5 = 243) per byte, GGML's
// block_tq1_0: byte p of a record holds elements p + n * positions, n <
// trits, the first the most significant base-3 digit of q = sum f_n 3^(4 - n)
// (so with trits < 5, the last digits 0), stored as ceil(q * 256 / 243): its
// digit n is then the top bits of (byte * 3^n mod 256) * 3. Arithmetic mod
// 256, as GGML's (fields past 2 still pack, as GGML packs them).
struct TritPack {
    int positions, trits = 5;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func f("packed_trits");
        Var p, b;
        Expr q = cast<uint8_t>(0);
        for (int n = 0; n < 5; n++) {
            q = q * cast<uint8_t>(3);
            if (n < trits) q = q + cast<uint8_t>(in[0](n * positions + p, b, _));
        }
        f(p, b, _) = cast<uint8_t>((cast<uint16_t>(q) * cast<uint16_t>(256) + cast<uint16_t>(242)) / cast<uint16_t>(243));
        return {f};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func f("unpacked_trits");
        Var j, b;
        std::vector<Expr> pow3;
        for (int n = 0, k = 1; n < trits; n++, k *= 3) {
            pow3.push_back(cast<uint8_t>(k));
        }
        Expr t = e[0](j % positions, b, _) * (trits == 1 ? pow3[0] : mux(j / positions, pow3));
        f(j, b, _) = cast<uint8_t>((cast<uint16_t>(t) * cast<uint16_t>(3)) >> 8);
        return {f};
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        std::optional<int> dims = in.empty() ? std::nullopt : in[0].dimensions;
        return {{{"fields", UInt(8), dims, ApproximationRange(0, 2)}}, {{"trits", UInt(8), dims}}};
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
// moves off the SIMD units. Or onto `entries` indices of type `index` (e.g.
// a Lattice's: GGML's grids, rows of a table of 2048).
struct Tabulate {
    Approximation inner;
    int per, entries;
    Buffer<> table;

    Tabulate(Approximation a, int per, int entries = 256, Type index = UInt(8))
        : inner(std::move(a)), per(per), entries(entries) {
        Func bytes("bytes"), random("random");
        Var p, v;
        bytes(p, v) = cast(index, v);
        Buffer<> t = inner.decode({bytes}).decoded[0].realize({per, entries});
        // one Buffer per table: a pipeline tells its Buffers apart by name
        static std::map<std::vector<uint8_t>, Buffer<>> tables;
        std::vector<uint8_t> key((const uint8_t *)t.data(), (const uint8_t *)t.data() + t.size_in_bytes());
        key.push_back((uint8_t)t.type().code()), key.push_back((uint8_t)t.type().bits()), key.push_back((uint8_t)per);
        auto it = tables.find(key);
        if (it == tables.end()) {
            t.set_name("table" + std::to_string(tables.size()));
            it = tables.emplace(key, t).first;
        }
        table = it->second;
        // inner reads as tabulated: compare on records of 8 random bytes
        random(p, v) = entries == 256 && index == UInt(8) ? cast<uint8_t>(random_uint()) : cast(index, random_uint() % Internal::make_const(UInt(32), entries));
        Func d = inner.decode({random}).decoded[0], t2 = tabulated({random})[0], diffs;
        RDom r(0, 8 * per, 0, 64);
        diffs() = sum(cast<int>(d(r.x, r.y) != t2(r.x, r.y)));
        Buffer<int> n = diffs.realize();
        if (n() != 0) throw std::invalid_argument("Tabulate: a decoded element reads more than its index");
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
        Expr i = cast<int>(e[0](j / per, b, _));
        if (!(entries == 256 && e[0].type() == UInt(8))) i = clamp(i, 0, entries - 1);  // indices past the table: undefined
        f(j, b, _) = table(j % per, i);
        return {f};
    }
};

// ---------------------------------------------------------------------------
// Units of GGML's grid formats (grid.h), each domain-free.
// ---------------------------------------------------------------------------

// The other way round: a's decode as the encode, its encode as the decode
// (e.g. Inverse{BitPack{4}}: bytes -> their nibbles). For units whose ports
// share their dimensionality.
struct Inverse {
    Approximation a;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        return a.decode(in).decoded;
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        return a.encode(e).encoded;
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        ApproximationSignature s = a.signature();
        std::swap(s.inputs, s.outputs);
        for (ApproximationPorts *ps : {&s.inputs, &s.outputs}) {
            for (ApproximationPort &p : *ps) {
                if (!in.empty()) p.dimensions = in[0].dimensions;
            }
        }
        return s;
    }
    bool lossless() const {
        return a.lossless();
    }
};

// An fp16 <-> its bits.
inline Approximation fp16_bits() {
    return Pointwise{"fp16_bits", [](const Expr &x) { return reinterpret<uint16_t>(x); }, [](const Expr &x) { return reinterpret<float16_t>(x); }}
        .with_types(Float(16), UInt(16))
        .with_lossless();
}

// Signed codes <-> their magnitudes and signs (+-1, 1 for 0): codes =
// magnitudes * signs (GGML's grids: unsigned grid values times sign bytes).
// Shape-preserving.
struct SignSplit {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func m("magnitudes"), s("signs");
        m(_) = cast<int8_t>(abs(in[0](_)));
        s(_) = select(in[0](_) < 0, cast<int8_t>(-1), cast<int8_t>(1));
        return {m, s};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func f("codes");
        f(_) = e[0](_) * e[1](_);
        return {f};
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        std::optional<int> dims = in.empty() ? std::nullopt : in[0].dimensions;
        std::optional<ApproximationRange> r = in.empty() ? std::nullopt : in[0].range, mr;
        if (r) mr = ApproximationRange(0, std::max(std::abs(r->lo), std::abs(r->hi)));
        return {{{"codes", Int(8), dims, r}},
                {{"magnitudes", Int(8), dims, mr}, {"signs", Int(8), dims, ApproximationRange(-1, 1)}}};
    }
    bool lossless() const {
        return true;
    }
};

// Signs +-1 <-> sign bits (1: negative).
inline Approximation sign_bits() {
    return Pointwise{"sign_bits", [](const Expr &x) { return cast<uint8_t>(x < 0); }, [](const Expr &x) { return cast<int8_t>(1) - (cast<int8_t>(x) << 1); }}
        .with_types(Int(8), UInt(8))
        .with_ranges(ApproximationRange(-1, 1), ApproximationRange(0, 1))
        .with_lossless();
}

// Each 8 bits of even parity <-> their first 7 (the 8th is their parity:
// GGML's ksigns_iq2xs, sign bits of grid points with an even number of
// negatives). Lossless on bits of even parity.
struct EvenSigns {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func f("sign_index");
        Var g, b;
        Expr e = cast<uint8_t>(0);
        for (int i = 0; i < 7; i++) {
            e = e | cast<uint8_t>((in[0](8 * g + i, b, _) & cast<uint8_t>(1)) << i);
        }
        f(g, b, _) = e;
        return {f};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func f("sign_bits");
        Var j, b;
        Expr i = e[0](j / 8, b, _);
        f(j, b, _) = select(j % 8 == 7, popcount(i) & cast<uint8_t>(1), (i >> cast<uint8_t>(j % 8)) & cast<uint8_t>(1));
        return {f};
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        std::optional<int> dims = in.empty() ? std::nullopt : in[0].dimensions;
        return {{{"bits", UInt(8), dims, ApproximationRange(0, 1)}}, {{"sign_index", UInt(8), dims, ApproximationRange(0, 127)}}};
    }
    bool lossless() const {
        return true;
    }
};

// Codes c = step * base + shift, the shift (+-1) shared by each run of n
// codes (GGML's iq1 deltas: grid values -1, 0, 1 shifted by +-1/8, at 8
// times their value) <-> (base, shift per run). Lossless on codes = +-1 mod
// step, one residue per run.
struct RunShift {
    int n, step;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func base("base"), shift("shifts");
        Var j, t, b;
        auto residue = [&](const Expr &c) { return (cast<int>(c) + step / 2) % step - step / 2; };
        shift(t, b, _) = cast<int8_t>(residue(in[0](t * n, b, _)));
        base(j, b, _) = cast<int8_t>((cast<int>(in[0](j, b, _)) - shift(j / n, b, _)) / step);
        return {base, shift};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func f("codes");
        Var j, b;
        f(j, b, _) = e[0](j, b, _) * cast<int8_t>(step) + e[1](j / n, b, _);
        return {f};
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        std::optional<int> dims = in.empty() ? std::nullopt : in[0].dimensions;
        std::optional<ApproximationRange> r = in.empty() ? std::nullopt : in[0].range, br;
        if (r) br = ApproximationRange(std::floor((r->lo + 1) / step), std::ceil((r->hi - 1) / step));
        return {{{"codes", Int(8), dims, r}}, {{"base", Int(8), dims, br}, {"shifts", Int(8), dims, ApproximationRange(-1, 1)}}};
    }
    bool lossless() const {
        return true;
    }
};

// Unsigned indices <-> their low `low` bits ("low") and the rest, shifted
// down ("high"), each a byte (GGML's grid indices: a byte of qs and the top
// bits in qh). Shape-preserving.
struct BitSplit {
    int low;
    Type index = UInt(16);

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func l("low"), h("high");
        l(_) = cast<uint8_t>(in[0](_) & cast(index, (1 << low) - 1));
        h(_) = cast<uint8_t>(in[0](_) >> cast(index, low));
        return {l, h};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func f("index");
        f(_) = cast(index, e[0](_)) + (cast(index, e[1](_)) << cast(index, low));  // (+: bounds inference follows it)
        return {f};
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        std::optional<int> dims = in.empty() ? std::nullopt : in[0].dimensions;
        std::optional<ApproximationRange> r = in.empty() ? std::nullopt : in[0].range, hr;
        if (r) hr = ApproximationRange(0, std::floor(r->hi / (1 << low)));
        return {{{"index", index, dims, r}},
                {{"low", UInt(8), dims, ApproximationRange(0, (1 << low) - 1)}, {"high", UInt(8), dims, hr}}};
    }
    bool lossless() const {
        return true;
    }
};

// Bit fields packed into words of type `word`: each input (element, record,
// ...) a field of `bits` bits, `count` of its elements per word (element g
// in word g / count, at bit at + stride * (g % count)) <-> (word, record,
// ...) (GGML's packed indices, signs and scales: iq2_xxs's 8 bytes per 32
// values, iq1_s's qh). Lossless on fields that fit their bits.
struct BitFields {
    struct Field {
        std::string name;
        int bits, count = 1, at = 0, stride = 0;
        Type type = UInt(8);
    };
    Type word;
    std::vector<Field> fields;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func f("bit_fields");
        Var s, b;
        Expr w = Internal::make_zero(word);
        for (size_t i = 0; i < fields.size(); i++) {
            const Field &fd = fields[i];
            for (int l = 0; l < fd.count; l++) {
                Expr v = cast(word, in[i](s * fd.count + l, b, _)) & Internal::make_const(word, (uint64_t)((1ull << fd.bits) - 1));
                w = w | (v << Internal::make_const(word, fd.at + fd.stride * l));
            }
        }
        f(s, b, _) = w;
        return {f};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        std::vector<Func> out;
        Var g, b;
        for (const Field &fd : fields) {
            Func f(fd.name);
            Expr w = e[0](fd.count > 1 ? g / fd.count : Expr(g), b, _);
            Expr at = fd.count > 1 ? fd.at + fd.stride * (g % fd.count) : Expr(fd.at);
            f(g, b, _) = cast(fd.type, (w >> cast(word, at)) & Internal::make_const(word, (uint64_t)((1ull << fd.bits) - 1)));
            out.push_back(f);
        }
        return out;
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        std::optional<int> dims = in.empty() ? std::nullopt : in[0].dimensions;
        ApproximationPorts ins;
        for (const Field &fd : fields) {
            ins.push_back({fd.name, fd.type, dims, ApproximationRange(0, (double)((1ull << fd.bits) - 1))});
        }
        return {ins, {{"word", word, dims}}};
    }
    bool lossless() const {
        return true;
    }
};

// Points of a lattice of `per` digits of `bits` bits each, from a list of
// them (each point a word, digit i in bits bits * i), as (digit, record,
// ...) <-> their indices in the list, (point, record, ...) (GGML's grids:
// kgrid_2bit_256 etc., whose digits a codebook then maps to values). The
// decode reads the list (as a Tabulate, a table of the digits per index);
// the encode searches it. Lossless on the listed points.
struct Lattice {
    std::string name;
    std::vector<uint16_t> points;
    int bits, per;

    Type index() const {
        return points.size() <= 256 ? UInt(8) : UInt(16);
    }
    Buffer<uint16_t> list() const {
        static std::map<std::string, Buffer<uint16_t>> lists;
        auto it = lists.find(name);
        if (it == lists.end()) {
            Buffer<uint16_t> l((int)points.size(), name);
            for (int i = 0; i < (int)points.size(); i++) {
                l(i) = points[i];
            }
            it = lists.emplace(name, l).first;
        }
        return it->second;
    }
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func key("lattice_key"), f("lattice_index");
        Var g, b;
        Expr k = cast<uint16_t>(0);
        for (int i = 0; i < per; i++) {
            k = k | (cast<uint16_t>(in[0](g * per + i, b, _)) << (bits * i));
        }
        key(g, b, _) = k;
        RDom r(0, (int)points.size());
        f(g, b, _) = cast(index(), argmin(select(list()(r) == key(g, b, _), 0, 1))[0]);
        return {f};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func f("lattice_digits");
        Var j, b;
        Expr p = list()(clamp(cast<int>(e[0](j / per, b, _)), 0, (int)points.size() - 1));
        f(j, b, _) = cast<uint8_t>((p >> cast<uint16_t>(bits * (j % per))) & cast<uint16_t>((1 << bits) - 1));
        return {f};
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        std::optional<int> dims = in.empty() ? std::nullopt : in[0].dimensions;
        return {{{"digits", UInt(8), dims, ApproximationRange(0, (1 << bits) - 1)}},
                {{"index", index(), dims, ApproximationRange(0, (double)points.size() - 1)}}};
    }
    bool lossless() const {
        return true;
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
