#pragma once

// Requantizing, block by block: whether encode(decode(e)) == e, for e =
// encode(x) (idempotent_requantize: a split idempotent's enc dec enc = enc),
// tallied by the class of each block's scale, to show where it holds (e.g.
// only where an fp16 scale is not 0) rather than only whether.
#include "Halide.h"
#include "halide_approximation_testing.h"

#include <cmath>
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

}  // namespace ggml
