#pragma once

// KleidiAI's 8-bit formats, as GGML's KleidiAI backend runs q8_0 matmuls:
// per-row (per-channel) scales, the whole row one quantization group.
// Weights, qsi8cx: symmetric int8 codes and a scale per row (GGML requantizes
// its q8_0 weights to it on load), plus the row's code sum, a derived port
// (KleidiAI's packer stores it for the activations' zero point). Activations,
// qai8dx: asymmetric int8 codes, a zero point and a scale per row
// (kai_lhs_quant_pack_qai8dxp_f32). A row is K values; the encode reduces
// over all of its `blocks` blocks of QK, a number known only at run time (an
// Expr: e.g. an input's extent). The values stay in QK-value blocks
// (BlockReshape), as the other formats' (j, b, row), so a kernel's reduction
// over a value of a block and the blocks is theirs; only the scales, zero
// points and sums are per row (row, ...).
#include "common.h"

namespace ggml {

// A row's values: (j, b) over its blocks.
inline RDom row_values(const Expr &blocks) {
    return RDom(0, QK, 0, blocks, "row");
}

// f's arguments after its (j, b): a row's (a reduction's initial value names
// them, as `_` cannot).
inline std::vector<Var> row_args(const Func &f) {
    return std::vector<Var>(f.dimensions() - 2);
}

// The args (j, b, row...) as Exprs.
inline std::vector<Expr> at(const Expr &j, const Expr &b, const std::vector<Var> &row) {
    std::vector<Expr> a{j, b};
    a.insert(a.end(), row.begin(), row.end());
    return a;
}

// GGML's q8_0 -> qsi8cx requantization (ggml-cpu/kleidiai/kleidiai.cpp):
// scale = max |x| / 127 over the row, code = clamp(lround(x / scale), -127,
// 127), with x / scale as x * (1 / scale) (codes 0 if the scale is 0). strict_float: as
// GGML's C, bit for bit (else / 127 becomes * (1 / 127)).
struct Qsi8cxQuant {
    Expr blocks;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0], amax("row_amax"), d("scale"), q("codes");
        Var j, b;
        RDom r = row_values(blocks);
        std::vector<Var> i = row_args(x);
        amax(i) = 0.0f;
        amax(i) = max(amax(i), abs(x(at(r.x, r.y, i))));
        d(_) = strict_float(amax(_) / 127.0f);
        Expr inv = select(d(_) > 0.0f, inverse(d(_)), 0.0f);
        q(j, b, _) = cast<int8_t>(clamp(strict_float(round_half_away(x(j, b, _) * inv)), -127.0f, 127.0f));
        return {q, d};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func v("values");
        Var j, b;
        v(j, b, _) = cast<float>(e[0](j, b, _)) * e[1](_);
        return {v};
    }
    static ApproximationSignature signature() {
        return codes_and_scale(-127, 127);
    }
    // Half a step: |x / scale| <= 127 (plus rounding), never clamped.
    Func error_bound(const std::vector<Func> &, const std::vector<Func> &e) const {
        Func f("bound");
        Var j, b;
        f(j, b, _) = abs(cast<double>(e[1](_))) * Expr(0.5 * 1.0001);
        return f;
    }
};

// kai_lhs_quant_pack_qai8dxp_f32 (KleidiAI v1.24.0), on a row: its range
// [rmin, rmax] = [min(min x, 0), max(max x, 0)] onto [-128, 127]: scale s =
// 255 / (rmax - rmin) (1 if 0), zero point z the one of -128 - rmin s and 127
// - rmax s with the smaller rounding error (as KleidiAI picks it), clamped to
// [-128, 127] and rounded to nearest even; code = clamp(round_half_away(x s)
// + z, -128, 127). Encoded: the codes, offset = -z (int32), and 1 / s (0 if s
// is 0). The row parameters are strict_float: bit-exact with KleidiAI's C,
// whatever fast math would reassociate or contract. Decoded: (code + offset)
// / s, as (code + offset) * (1 / s).
//
// Error: within (0.5 + 2^-10) / s of x. With t = fl(x s) + z, every code is
// round(t) clamped to [-128, 127]; t stays within [-128.5 - e, 127.5 + e]
// (e < 1e-4), because the chosen z is within 1/2 of -128 - rmin s (so the
// row's bottom maps to >= -128.5) or of 127 - rmax s (its top to <= 127.5),
// and fl(rmax s) - fl(rmin s) <= 255 (1 + 3 2^-24): the other end then maps
// within 1/2 + e of its bound too. Rounding and clamping such a t moves it by
// at most 1/2 + e, and the float steps (x s, 1 / s, the decode's product)
// add < 2^-12 of a step. Preconditions: finite x, and rmax - rmin either 0
// or within [2^-116, FLT_MAX] (s and 1 / s finite and normal).
struct Qai8dxQuant {
    Expr blocks;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func x = in[0], range("row_range"), s("qscale"), z("zero_point"), q("codes"), o("offset"), rs("scale");
        Var j, b;
        RDom r = row_values(blocks);
        std::vector<Var> i = row_args(x);
        Expr v = x(at(r.x, r.y, i));
        range(i) = Tuple(Float(32).max(), Float(32).min());
        range(i) = Tuple(min(range(i)[0], v), max(range(i)[1], v));
        Expr lo = min(range(_)[0], 0.0f), hi = max(range(_)[1], 0.0f);
        s(_) = strict_float(select(lo == hi, 1.0f, 255.0f / (hi - lo)));
        Expr dmin = lo * s(_), dmax = hi * s(_);
        Expr zp = select((-128.0f + dmin) + (127.0f + dmax) > 0.0f, -128.0f - dmin, 127.0f - dmax);
        z(_) = cast<int>(strict_float(round(clamp(zp, -128.0f, 127.0f))));
        q(j, b, _) = cast<int8_t>(clamp(cast<int>(round_half_away(x(j, b, _) * s(_))) + z(_), -128, 127));
        o(_) = -z(_);
        rs(_) = strict_float(select(s(_) != 0.0f, inverse(s(_)), 0.0f));
        return {q, o, rs};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func v("values");
        Var j, b;
        v(j, b, _) = (cast<float>(e[0](j, b, _)) + cast<float>(e[1](_))) * e[2](_);
        return {v};
    }
    static ApproximationSignature signature() {
        return {{{"blocks", Float(32)}},
                {{"codes", Int(8), std::nullopt, ApproximationRange(-128, 127)},
                 {"offset", Int(32), std::nullopt, ApproximationRange(-127, 128)},
                 {"scale", Float(32)}}};
    }
    Func error_bound(const std::vector<Func> &, const std::vector<Func> &e) const {
        Func f("bound");
        Var j, b;
        f(j, b, _) = abs(cast<double>(e[2](_))) * Expr(0.5 + std::ldexp(1.0, -10));
        return f;
    }
};

// The row's code sum, from codes(j, b, ...): sum(qs) per row (...).
inline Derive row_sum(const Expr &blocks) {
    return Derive{"sum", {"codes"}, [blocks](const std::vector<Func> &from) {
                      Func s("row_sum");
                      RDom r = row_values(blocks);
                      std::vector<Var> i = row_args(from[0]);
                      s(i) = 0;
                      s(i) += cast<int>(from[0](at(r.x, r.y, i)));
                      return s;
                  }};
}

// The codes' layout: planar ("aos", "soa": the codes [QK, blocks, rows], each
// per-row field [rows]), or "<rows>x<chunk>": records of `rows` rows' codes
// interleaved in `chunk`-byte pieces (KleidiAI's qsi8cxp4x8 is 4x8: the same
// bytes as its packed codes), [rows * QK, blocks, rows / rows]; the per-row
// fields stay planar.
inline Approximation row_codes_layout(const std::string &lay) {
    if (lay == "aos" || lay == "soa") return Identity{};
    int rows = 0, chunk = 0;
    if (sscanf(lay.c_str(), "%dx%d", &rows, &chunk) != 2) throw std::invalid_argument("unknown layout " + lay);
    return Parallel{{"codes", Interleave{rows, chunk, {{"qs", Int(8), QK}}, {"qs"}}}};
}

// qsi8cx: codes, scale, sum.
inline Approximation qsi8cx(const std::string &lay, const Expr &blocks, bool indexed = false) {
    return Compose{BlockReshape{QK, indexed}, Qsi8cxQuant{blocks}, row_sum(blocks), row_codes_layout(lay)};
}

// qai8dx: codes, offset, scale.
inline Approximation qai8dx(const std::string &lay, const Expr &blocks, bool indexed = false) {
    return Compose{BlockReshape{QK, indexed}, Qai8dxQuant{blocks}, row_codes_layout(lay)};
}

}  // namespace ggml
