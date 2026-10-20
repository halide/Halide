// MiniFloat (schemes/common.h), exhaustively: its generic encode (on the bits)
// against Halide's float16_t (to nearest even; NaNs quieted, as the host's
// conversion) and GGML's bf16 (ggml_compute_fp32_to_bf16, transcribed) on
// every f32, and its generic decode against theirs on every code; GGML's e8m0
// decode (ggml_e8m0_to_fp32) on every code; on every f32 and code, the
// rounding, overflow and specials of fp8/fp6/fp4/e8m0-like formats (to
// nearest, and every rounding); and units within their declared bounds.
#include "Halide.h"
#include "halide_approximation_testing.h"
#include "schemes/common.h"

#include <cstring>
#include <iostream>

using namespace Halide;
using namespace Halide::ApproximationTesting;

namespace {

// How many of the 2^bits values `bits`, as uint32, `differ` (a bool); prints
// the first few.
int64_t count(const std::string &what, int bits, const std::function<Expr(Expr)> &differ) {
    int lo = bits > 16 ? 16 : bits, hi = bits - lo;
    Var y("y");
    RDom r(0, 1 << lo);
    Func n("n");
    Expr u = (cast<uint32_t>(y) << lo) | cast<uint32_t>(r);
    n(y) = 0;
    n(y) += cast<int>(differ(u));
    n.parallel(y).update().parallel(y).atomic().vectorize(r, 16);
    Buffer<int> c = n.realize({1 << hi});
    int64_t total = 0;
    for (int i = 0; i < c.width(); i++) {
        total += c(i);
    }
    printf("%s: %lld of %lld differ\n", what.c_str(), (long long)total, (long long)1 << bits);
    if (total) {
        int row = 0;
        while (!c(row)) {
            row++;
        }
        Var x;
        Func d;
        d(x) = select(differ((cast<uint32_t>(row) << lo) | cast<uint32_t>(x)), 1, 0);
        Buffer<int> b = d.realize({1 << lo});
        for (int i = 0, shown = 0; i < b.width() && shown < 4; i++) {
            if (b(i)) printf("  e.g. 0x%08x\n", ((unsigned)row << lo) | (unsigned)i), shown++;
        }
    }
    return total;
}

Expr u32(uint32_t v) {
    return Internal::make_const(UInt(32), v);
}

// The bits of x and y (floats) differ.
Expr bits_differ(const Expr &x, const Expr &y) {
    return reinterpret(UInt(x.type().bits()), x) != reinterpret(UInt(y.type().bits()), y);
}

// The generic path on any format: every f32 up to the largest finite value
// rounds to a value of a code next to it (nearest, ties to even or away from
// zero; or toward zero, -inf, +inf), keeping its sign (unsigned: x <= 0 to
// 0); past it, to the largest finite value, or inf (NaN for NaNOnly) from
// where the rounding passes it; NaNs stay NaN; and every code but NaNs
// round-trips.
using MF = ggml::MiniFloat;
int64_t generic(const MF &mf) {
    const int mb = mf.exp + mf.man;
    const uint32_t emax = (1u << mf.exp) - 1, mags = (1u << mb) - 1;
    const uint32_t top = mf.specials == MF::Specials::IEEE ? (emax << mf.man) - 1 : mf.specials == MF::Specials::NaNOnly ? mags - 1 :
                                                                                                                           mags;
    const uint32_t past_code = mf.overflow == MF::Overflow::Saturate ? top : mf.specials == MF::Specials::IEEE ? emax << mf.man :
                                                                                                                 mags;
    const int top_exp = (int)(top >> mf.man);
    const Expr ulp = Expr(std::ldexp(1.0, top_exp - mf.bias - mf.man));
    Type ft = mf.field_type();
    auto value = [&](const Expr &c) { return cast<double>(mf.decode_bits(cast(ft, c))); };
    auto nan_code = [&](const Expr &cm) {
        if (mf.specials == MF::Specials::IEEE) return (cm >> mf.man) == u32(emax) && (cm & u32((1u << mf.man) - 1)) != 0;
        if (mf.specials == MF::Specials::NaNOnly) return cm == u32(mags);
        return Internal::const_false();
    };
    Expr maxv = value(u32(top));
    int64_t n = count(mf.name() + " encode_bits", 32, [&](Expr u) {
        Expr x = reinterpret<float>(u), xd = cast<double>(x), c = cast<uint32_t>(mf.encode_bits(x));
        Expr neg = (u >> 31) == u32(1), cm = c & u32(mags), sign = mf.is_signed ? c & u32(1u << mb) : u32(0);
        Expr ax = abs(xd), av = abs(value(c)), a_lo = abs(value((cm - 1) | sign)), a_hi = abs(value((cm + 1) | sign));
        Expr d = abs(ax - av);
        // the magnitude: to nearest, down (toward zero) or up
        Expr is_near = (cm == 0 || abs(ax - a_lo) >= d) && (cm >= u32(top) || abs(ax - a_hi) >= d);
        Expr tie = (cm > 0 && abs(ax - a_lo) == d) || (cm < u32(top) && abs(ax - a_hi) == d);
        Expr tie_ok = mf.rounding == MF::Rounding::NearestAway ? av >= ax : Internal::make_bool(mf.man == 0) || (cm & 1) == 0;
        Expr is_down = (cm == 0 || av <= ax) && (cm >= u32(top) || a_hi > ax);
        Expr is_up = av >= ax && (cm == 0 || a_lo < ax);
        Expr ok, thr;  // thr: from where a magnitude rounds past the largest
        switch (mf.rounding) {
        case MF::Rounding::NearestEven:
        case MF::Rounding::NearestAway:
            ok = is_near && (!tie || tie_ok), thr = maxv + ulp / 2;
            break;
        case MF::Rounding::TowardZero:
            ok = is_down, thr = maxv + ulp;
            break;
        case MF::Rounding::Down:
            ok = select(neg, is_up, is_down), thr = select(neg, maxv, maxv + ulp);
            break;
        case MF::Rounding::Up:
            ok = select(neg, is_down, is_up), thr = select(neg, maxv + ulp, maxv);
            break;
        }
        // (a tie at thr to even: past it only if the largest is odd)
        Expr passes = mf.rounding == MF::Rounding::NearestEven && !(top & 1) ? ax > thr : ax >= thr;
        Expr over = select(passes && ax > maxv, cm == u32(past_code), cm == u32(top));
        Expr sign_ok = mf.is_signed ? (c >> mb) == (u >> 31) : Internal::const_true();
        Expr nan = (u & u32(0x7fffffff)) > u32(0x7f800000), want;
        if (mf.is_signed) {
            want = select(nan, nan_code(cm) || mf.specials == MF::Specials::None, ax > maxv, over, ok) && sign_ok;
        } else {
            want = select(nan, nan_code(cm) || mf.specials == MF::Specials::None, x <= 0.0f, c == 0, ax > maxv, over, ok);
        }
        return !want;
    });
    n += count(mf.name() + " decode_bits: codes round-trip", mf.bits(), [&](Expr c) {
        // (but inf, saturating)
        Expr inf = mf.specials == MF::Specials::IEEE && mf.overflow == MF::Overflow::Saturate ? (c & u32(mags)) == u32(emax << mf.man) : Internal::const_false();
        return !nan_code(c & u32(mags)) && !inf && cast<uint32_t>(mf.encode_bits(mf.decode_bits(cast(ft, c)))) != c;
    });
    return n;
}

}  // namespace

int main() {
    int64_t failures = 0;
    const MF f16 = MF::binary16(), b16 = MF::bfloat16();

    failures += count("fp16 encode_bits vs float16_t", 32, [&](Expr u) {
        Expr x = reinterpret<float>(u);
        return f16.encode_bits(x) != reinterpret<uint16_t>(strict_float(cast<float16_t>(x)));
    });
    failures += count("fp16 decode_bits vs float16_t", 16, [&](Expr u) {
        Expr h = cast<uint16_t>(u);
        return bits_differ(f16.decode_bits(h), cast<float>(reinterpret<float16_t>(h)));
    });
    failures += count("bf16 encode vs ggml_compute_fp32_to_bf16", 32, [&](Expr u) {
        Expr x = reinterpret<float>(u);
        Expr ggml = cast<uint16_t>(select((u & u32(0x7fffffff)) > u32(0x7f800000), (u >> 16) | 64, (u + u32(0x7fff) + ((u >> 16) & 1)) >> 16));
        return reinterpret<uint16_t>(b16.encode(x)) != ggml;
    });
    failures += count("bf16 decode_bits vs bfloat16_t", 16, [&](Expr u) {
        Expr h = cast<uint16_t>(u);
        return bits_differ(b16.decode_bits(h), cast<float>(reinterpret<bfloat16_t>(h)));
    });
    // e8m0: unsigned, exponent only (no zero)
    failures += count("e8m0 decode_bits vs ggml_e8m0_to_fp32", 8, [&](Expr u) {
        Expr ggml = select(u == 0, u32(0x00400000), u << 23);
        return reinterpret<uint32_t>(MF{8, 0, 127, false, MF::Specials::None, MF::Overflow::Saturate}.decode_bits(cast<uint8_t>(u))) != ggml;
    });

    // OCP's fp8 e5m2 and e4m3fn (saturating, and not: overflow to NaN),
    // fp6 e3m2, fp4 e2m1 (MXFP4's values) and e8m0 (scales: floor of log2);
    // unsigned e4m3 rounding half away from zero; other roundings; and
    // binary16 and bfloat16 themselves, on the generic path
    using S = MF::Specials;
    using O = MF::Overflow;
    using R = MF::Rounding;
    for (const MF &mf : std::vector<MF>{
             {5, 2, 15},
             {4, 3, 7, true, S::NaNOnly, O::Saturate},
             {4, 3, 7, true, S::NaNOnly, O::Infinity},
             {3, 2, 3, true, S::None, O::Saturate},
             {2, 1, 1, true, S::None, O::Saturate},
             {8, 0, 127, false, S::NaNOnly, O::Saturate, R::Down},
             {4, 3, 7, false, S::None, O::Saturate, R::NearestAway},
             {4, 3, 7, true, S::None, O::Saturate, R::Up},
             {5, 2, 15, true, S::IEEE, O::Infinity, R::TowardZero},
             {3, 2, 3, true, S::IEEE, O::Saturate, R::Down},
             f16,
             b16}) {
        try {
            failures += generic(mf);
        } catch (const Halide::Error &e) {
            printf("%s: %s\n", mf.name().c_str(), e.what());
            failures++;
        }
    }

    const std::vector<std::pair<std::string, Distribution>> dists = {
        {"normal", Distribution::normal(0, 1)},
        {"wide", Distribution::uniform(-6e4, 6e4)},
        {"tiny", Distribution::normal(0, 1e-6)},
        {"zeros", Distribution::zeros()},
    };
    // (the fp8s: in range, not "wide")
    for (auto [name, a] : {std::pair{"fp16", ggml::fp16()}, {"bf16", ggml::bf16()}, {"e4m3", Approximation(MF{4, 3, 7, true, S::NaNOnly, O::Saturate})}, {"e5m2", Approximation(MF{5, 2, 15, true, S::IEEE, O::Infinity, R::TowardZero})}}) {
        for (const auto &[dn, dist] : dists) {
            if (dn == "wide" && name[0] == 'e') continue;
            for (const Property &p : {within_declared_bound(), closed()}) {
                PropertyResult r = check_property(a, p, dist, Float(32), {4096}, 8, 1);
                std::cout << name << " (" << dn << "): " << r;
                failures += !r.passed;
            }
        }
    }

    if (failures) {
        printf("%lld failures\n", (long long)failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
