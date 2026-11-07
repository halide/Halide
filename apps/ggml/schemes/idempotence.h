#pragma once

// Requantizing, block by block: whether encode(decode(e)) == e, for e =
// encode(x) (idempotent_requantize: a split idempotent's enc dec enc = enc),
// tallied by the class of each block's scale, to show where it holds (e.g.
// only where an fp16 scale is not 0) rather than only whether.
#include "Halide.h"
#include "halide_approximation_testing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ggml {

// A block's scale (the encoded one): 0 for a block that decodes exactly
// (Zero: e.g. all zero) or for one that does not (Underflow: the scale
// rounded to 0), below the normals of its format
// (Subnormal: fp16's, 2^-14, by default), a normal float, or inf or NaN.
enum class ScaleClass { Zero,
                        Underflow,
                        Subnormal,
                        Normal,
                        NonFinite };
constexpr int scale_classes = 5;

inline const char *scale_class_name(int c) {
    static const char *names[scale_classes] = {"zero", "underflow", "subnormal", "normal", "non-finite"};
    return names[c];
}

struct RequantizeTally {
    int64_t blocks[scale_classes] = {}, changed[scale_classes] = {};
    std::string example[scale_classes];  // the first changed block's

    int64_t changed_in(ScaleClass c) const {
        return changed[(int)c];
    }
    int64_t total_changed() const {
        int64_t n = 0;
        for (int64_t c : changed) {
            n += c;
        }
        return n;
    }
    int64_t blocks_in(ScaleClass c) const {
        return blocks[(int)c];
    }
    // Requantizing changes no block whose scale is finite and not 0 (normal
    // or subnormal; or 0 and exact), and every block whose scale underflowed
    // to 0 or is inf or NaN.
    bool holds_where_scale_finite_nonzero() const {
        return !changed_in(ScaleClass::Zero) && !changed_in(ScaleClass::Subnormal) && !changed_in(ScaleClass::Normal) &&
               changed_in(ScaleClass::Underflow) == blocks_in(ScaleClass::Underflow) &&
               changed_in(ScaleClass::NonFinite) == blocks_in(ScaleClass::NonFinite);
    }
    std::string str() const {
        std::string s;
        for (int c = 0; c < scale_classes; c++) {
            if (!blocks[c]) continue;
            s += std::string(s.empty() ? "" : ", ") + scale_class_name(c) + " " + std::to_string(changed[c]) + "/" + std::to_string(blocks[c]);
        }
        s += " blocks changed";
        for (int c = 0; c < scale_classes; c++) {
            if (changed[c]) s += "\n    e.g. (" + std::string(scale_class_name(c)) + ") " + example[c];
        }
        return s;
    }
};

// check_property on one input: `values`, flat.
inline Halide::ApproximationTesting::PropertyResult check_on(const Halide::Approximation &a, const Halide::ApproximationTesting::Property &p,
                                                             const std::vector<float> &values) {
    Halide::Buffer<float> in((int)values.size());
    std::copy(values.begin(), values.end(), in.begin());
    return Halide::ApproximationTesting::check_property(a, p, [=](uint64_t) { return std::vector<Halide::Buffer<>>{in}; }, 1, 0);
}

// `blocks` blocks of `n` values, flat: each `v` followed by zeros.
inline std::vector<float> blocks_of(int n, const std::vector<std::vector<float>> &blocks) {
    std::vector<float> out;
    for (const std::vector<float> &b : blocks) {
        std::vector<float> v(n, 0.0f);
        std::copy(b.begin(), b.end(), v.begin());
        out.insert(out.end(), v.begin(), v.end());
    }
    return out;
}

// For a format of one-dimensional input (values in blocks: every encoded
// port's last dimension the block's index): per block, whether re-encoding
// its decoded values changes any of its encoded values (floats: by more
// than float_rel_tol relative), tallied in `t` by the class of the block's
// value(s) of the encoded port `scale` (the least normal of them, if
// several; Subnormal: below min_normal). Passes but where a port is
// unreadable: the test checks the tally.
inline Halide::ApproximationTesting::Property requantize_tally(const std::string &scale, std::shared_ptr<RequantizeTally> t,
                                                               double float_rel_tol = 0, double min_normal = 0x1p-14) {
    using namespace Halide::ApproximationTesting;
    return Property("requantize_tally(" + scale + ")", [=](const PropertyContext &c) {
        for (size_t p = 0; p < c.encoded().size(); p++) {
            if (!c.encoded_readable(p)) return c.encoded_unreadable(p);
        }
        int sp = -1;
        for (size_t p = 0; p < c.encoded().size(); p++) {
            if (c.encoded_ports()[p].name == scale) sp = (int)p;
        }
        if (sp < 0) return Detail::fail("no encoded port '" + scale + "'");
        const Halide::Buffer<> &sb = c.encoded()[sp];
        const int nb = sb.dim(sb.dimensions() - 1).extent();
        const Halide::Buffer<> &in = c.inputs[0];
        const int per = in.number_of_elements() / nb;
        std::vector<bool> lost(nb), changed(nb);
        std::vector<std::string> what(nb);
        Detail::for_each_coord(in, [&](const int *pos) {
            int b = (pos[0] - in.dim(0).min()) / per;
            lost[b] = lost[b] || Detail::get(in, pos) != Detail::get(c.decoded()[0], pos);
        });
        const Detail::RoundTripRun &again = c.requantized();
        for (size_t p = 0; p < c.encoded().size(); p++) {
            const Halide::Buffer<> &e1 = c.encoded()[p], &e2 = again.encoded[p];
            const int last = e1.dimensions() - 1;
            Detail::for_each_coord(e1, [&](const int *pos) {
                double x = Detail::get(e1, pos), y = Detail::get(e2, pos);
                bool same = x == y || (std::isnan(x) && std::isnan(y)) ||
                            (Detail::is_floating(e1.type()) && std::fabs(x - y) <= float_rel_tol * std::max(std::fabs(x), std::fabs(y)));
                int b = pos[last] - e1.dim(last).min();
                if (!same && !changed[b]) {
                    changed[b] = true;
                    what[b] = "block " + std::to_string(b) + ": '" + c.encoded_ports()[p].name + "' at " +
                              Detail::coord_string(std::vector<int>(pos, pos + e1.dimensions())) + " " + Detail::describe_pair(x, y);
                }
            });
        }
        std::vector<int> cls(nb, (int)ScaleClass::Normal);
        Detail::for_each_coord(sb, [&](const int *pos) {
            double v = std::fabs(Detail::get(sb, pos));
            int b = pos[sb.dimensions() - 1] - sb.dim(sb.dimensions() - 1).min();
            int k = !std::isfinite(v) ? (int)ScaleClass::NonFinite :
                    v == 0            ? (lost[b] ? (int)ScaleClass::Underflow : (int)ScaleClass::Zero) :
                    v < min_normal    ? (int)ScaleClass::Subnormal :
                                        (int)ScaleClass::Normal;
            // the least normal: NonFinite, then Underflow, Zero, Subnormal
            auto rank = [](int k) { return k == (int)ScaleClass::NonFinite ? 0 : k == (int)ScaleClass::Underflow ? 1 :
                                                                             k == (int)ScaleClass::Zero          ? 2 :
                                                                             k == (int)ScaleClass::Subnormal     ? 3 :
                                                                                                                   4; };
            if (rank(k) < rank(cls[b])) cls[b] = k;
        });
        for (int b = 0; b < nb; b++) {
            t->blocks[cls[b]]++;
            if (changed[b]) {
                if (!t->changed[cls[b]]) t->example[cls[b]] = what[b];
                t->changed[cls[b]]++;
            }
        }
        return PropertyOutcome();
    });
}

// Per block (every encoded port's last dimension its index), whether
// requantizing changes any of its encoded values, into `out`.
inline Halide::ApproximationTesting::Property requantize_changed_blocks(std::shared_ptr<std::vector<bool>> out) {
    using namespace Halide::ApproximationTesting;
    return Property("requantize_changed_blocks", [=](const PropertyContext &c) {
        out->clear();
        for (size_t p = 0; p < c.encoded().size(); p++) {
            if (!c.encoded_readable(p)) return c.encoded_unreadable(p);
            const Halide::Buffer<> &e1 = c.encoded()[p], &e2 = c.requantized().encoded[p];
            const int last = e1.dimensions() - 1;
            out->resize(e1.dim(last).extent());
            Detail::for_each_coord(e1, [&](const int *pos) {
                double x = Detail::get(e1, pos), y = Detail::get(e2, pos);
                if (!(x == y || (std::isnan(x) && std::isnan(y)))) (*out)[pos[last] - e1.dim(last).min()] = true;
            });
        }
        return PropertyOutcome();
    });
}

// requantize_tally over `trials` inputs of `n` values from each of `dists`,
// summed; `error` set if a check failed (a port unreadable).
struct TallyOver {
    RequantizeTally tally;
    std::string error;
};
inline TallyOver requantize_tally_over(const Halide::Approximation &a, const std::string &scale,
                                       const std::vector<std::pair<std::string, Halide::ApproximationTesting::Distribution>> &dists,
                                       int n, int trials, double float_rel_tol = 0, double min_normal = 0x1p-14) {
    TallyOver out;
    auto t = std::make_shared<RequantizeTally>();
    for (const auto &[name, dist] : dists) {
        Halide::ApproximationTesting::PropertyResult r =
            Halide::ApproximationTesting::check_property(a, requantize_tally(scale, t, float_rel_tol, min_normal), dist, Halide::Float(32), {n}, trials, 1);
        if (!r.passed) out.error += name + ": " + r.message + "\n";
    }
    out.tally = *t;
    return out;
}

// Per block (every encoded port's last dimension its index), whether
// requantizing changes any of its encoded values (and the first that
// changes), and the block's first value of each of the encoded ports
// `params` (its scale, min, ...), into `out`.
struct BlockRequantized {
    bool changed = false;
    std::string what;
    std::map<std::string, double> param;
};
inline Halide::ApproximationTesting::Property requantize_blocks(std::shared_ptr<std::vector<BlockRequantized>> out,
                                                                std::vector<std::string> params = {}) {
    using namespace Halide::ApproximationTesting;
    return Property("requantize_blocks", [=](const PropertyContext &c) {
        out->clear();
        for (size_t p = 0; p < c.encoded().size(); p++) {
            if (!c.encoded_readable(p)) return c.encoded_unreadable(p);
            const Halide::Buffer<> &e1 = c.encoded()[p], &e2 = c.requantized().encoded[p];
            const int last = e1.dimensions() - 1;
            const std::string &name = c.encoded_ports()[p].name;
            bool is_param = std::find(params.begin(), params.end(), name) != params.end();
            out->resize(e1.dim(last).extent());
            Detail::for_each_coord(e1, [&](const int *pos) {
                double x = Detail::get(e1, pos), y = Detail::get(e2, pos);
                BlockRequantized &b = (*out)[pos[last] - e1.dim(last).min()];
                if (is_param && !b.param.count(name)) b.param[name] = x;
                if (!(x == y || (std::isnan(x) && std::isnan(y))) && !b.changed) {
                    b.changed = true;
                    b.what = "'" + name + "' at " + Detail::coord_string(std::vector<int>(pos, pos + e1.dimensions())) + " " + Detail::describe_pair(x, y);
                }
            });
        }
        return PropertyOutcome();
    });
}

// Adversarial blocks of `n` values for an affine format's min (a large
// offset, a small spread), flat: per block, its min M, its max fl(M + S),
// and the rest M (every other one) or fl(M + S u), u in [0, 1) from a fixed
// hash; for M = +-2^k, k in [-14, 15], nudged up 0, 1 or 3 f32 ulps or down
// 1 or 4, or below 2^k by about half an fp16 ulp (2^k (1 - 2^-12): ties up
// to 2^k, across a binade; 2^k (1 - 2^-13): rounds up; 2^k (1 - 3 2^-13):
// rounds down), and S = s ulp_f32(2^k), s in {1, 2, 3, 4, 6, 8, ..., 2^16, 3
// 2^15}: from spreads (and so steps d) far below an f32 ulp of M to far
// above. 30 x 16 x 33 = 15840 blocks; with every `stride`th, fewer.
inline std::vector<float> offset_blocks(int n, int stride = 1) {
    std::vector<float> out;
    std::vector<double> nudges = {0, 0x1p-23, 3 * 0x1p-23, -0x1p-24, -4 * 0x1p-24, -0x1p-12, -0x1p-13, -3 * 0x1p-13};
    std::vector<double> spreads;
    for (int j = 0; j <= 16; j++) {
        spreads.push_back(std::ldexp(1.0, j));
        if (j >= 1) spreads.push_back(3 * std::ldexp(1.0, j - 1));
    }
    int64_t index = 0;
    for (int k = -14; k <= 15; k++) {
        for (double nudge : nudges) {
            for (int sign : {1, -1}) {
                for (double s : spreads) {
                    if (index++ % stride) continue;
                    float m = (float)(sign * std::ldexp(1.0 + nudge, k));
                    float spread = (float)(s * std::ldexp(1.0, k - 23));
                    std::vector<float> v(n, m);
                    v[1 % n] = m + spread;
                    for (int j = 2; j < n; j += 2) {
                        uint32_t h = (uint32_t)(j * 2654435761u) ^ (uint32_t)(index * 40503u);
                        h = (h ^ (h >> 15)) * 2246822519u;
                        v[j] = m + spread * (float)((h >> 8) & 0xffff) * 0x1p-16f;
                    }
                    out.insert(out.end(), v.begin(), v.end());
                }
            }
        }
    }
    return out;
}

// Whether every value an affine block (step d, min m, codes 0 to top)
// decodes to, q d + m, is a float: d and m are fp16, so q d + m is exact
// in a double.
inline bool decodes_exactly(double d, double m, int top) {
    for (int q = 0; q <= top; q++) {
        double v = q * d + m;
        if ((double)(float)v != v) return false;
    }
    return true;
}

// Random blocks of `n` values, `count` of them, as offset_blocks: M = +-2^k
// (1 + u), k in [-10, 15] (or, one in four, 2^k (2 - u 2^-11): within an
// fp16 ulp below 2^(k + 1)), and S = 15 2^r ulp_f32(M), r in [0, 12).
inline std::vector<float> random_offset_blocks(int n, int count, uint32_t seed = 1) {
    uint64_t state = seed;
    auto uniform = [&]() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return (double)(state >> 11) * 0x1p-53;
    };
    std::vector<float> out;
    for (int b = 0; b < count; b++) {
        int k = (int)(uniform() * 26) - 10;
        double big = std::ldexp(1.0 + uniform(), k);
        if (uniform() < 0.25) big = std::ldexp(2.0 - std::ldexp(uniform(), -11), k);
        float m = (float)(uniform() < 0.5 ? -big : big);
        int e;
        std::frexp(m, &e);
        float spread = (float)(15 * std::exp2(uniform() * 12) * std::ldexp(1.0, e - 24));
        std::vector<float> v(n, m);
        v[1 % n] = m + spread;
        for (int j = 2; j < n; j++) {
            if (j & 1) v[j] = m + spread * (float)uniform();
        }
        out.insert(out.end(), v.begin(), v.end());
    }
    return out;
}

// For an affine format (codes 0 to top, fp16 step d "scale" and min m
// "min"): requantizing `values`' blocks, tallied by the block's d and m:
// d 0 (underflowed), not finite, every decoded value q d + m a float
// (exact), or not, and of those, d >= 2^9 ulp_f32(max |q d + m|) (far).
struct AffineTally {
    int64_t blocks[5] = {}, changed[5] = {};
    double most_ulps_changed = 0;  // the most d / ulp_f32(max |q d + m|) of a changed block
    std::string example, error;
    enum { zero,
           non_finite,
           exact,
           inexact,
           inexact_far };
    std::string str() const {
        static const char *names[5] = {"d 0", "not finite", "decoding exactly", "not exactly, d < 2^9 ulps", "not exactly, d >= 2^9 ulps"};
        std::string s;
        for (int c = 0; c < 5; c++) {
            s += std::string(c ? ", " : "") + names[c] + " " + std::to_string(changed[c]) + "/" + std::to_string(blocks[c]);
        }
        char most[64];
        snprintf(most, sizeof(most), "%g", most_ulps_changed);
        return s + " blocks changed; most d/ulp_f32 changed (d finite, not 0): " + most + (example.empty() ? "" : "\n    e.g. " + example);
    }
};
inline AffineTally affine_requantize_tally(const Halide::Approximation &a, int top, int n, const std::vector<float> &values) {
    AffineTally t;
    auto out = std::make_shared<std::vector<BlockRequantized>>();
    Halide::ApproximationTesting::PropertyResult r = check_on(a, requantize_blocks(out, {"scale", "min"}), values);
    if (!r.passed) {
        t.error = r.message;
        return t;
    }
    for (size_t b = 0; b < out->size(); b++) {
        const BlockRequantized &B = (*out)[b];
        double d = B.param.at("scale"), m = B.param.at("min");
        double most = std::max(std::fabs(m), std::fabs(m + top * d));
        int e;
        std::frexp(most, &e);
        double ulps = d / std::ldexp(1.0, std::max(e - 24, -149));
        int c = d == 0                                 ? AffineTally::zero :
                !std::isfinite(d) || !std::isfinite(m) ? AffineTally::non_finite :
                decodes_exactly(d, m, top)             ? AffineTally::exact :
                ulps >= 512                            ? AffineTally::inexact_far :
                                                         AffineTally::inexact;
        t.blocks[c]++;
        t.changed[c] += B.changed;
        if (B.changed && c >= AffineTally::exact) {
            t.most_ulps_changed = std::max(t.most_ulps_changed, ulps);
            if (t.example.empty()) {
                char s[256];
                snprintf(s, sizeof(s), "{%a, %a, ...} d %a m %a (d %g ulps): ", values[b * n], values[b * n + 1], d, m, ulps);
                t.example = s + B.what;
            }
        }
    }
    return t;
}

// How many of `values`' blocks requantizing changes (-1: a port unreadable).
inline int64_t requantize_changed_count(const Halide::Approximation &a, const std::vector<float> &values, std::string *example = nullptr) {
    auto out = std::make_shared<std::vector<BlockRequantized>>();
    if (!check_on(a, requantize_blocks(out), values).passed) return -1;
    int64_t n = 0;
    for (size_t b = 0; b < out->size(); b++) {
        if ((*out)[b].changed && example && example->empty()) *example = "block " + std::to_string(b) + ": " + (*out)[b].what;
        n += (*out)[b].changed;
    }
    return n;
}

}  // namespace ggml
