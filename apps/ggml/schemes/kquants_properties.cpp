// Properties of the K-quants (schemes/kquants.h), without their layouts
// (but q8_K's, which bounds its derived bsums):
// the round trip is within the declared error bound, the codes (and
// sub-block scales) are within their declared ranges, and every stage is
// closed. The blocks are drawn to reach the searches' corners: binades of
// every size, one-signed blocks, blocks dominated by one value, constants
// and zeros.
//
// And requantizing (enc dec enc = enc), which q2_K..q6_K do not keep, with
// normal scales too: the sub-block searches (make_qkx2_quants: q2_K, q4_K,
// q5_K; make_q3_quants: q3_K; make_qx_quants: q6_K) refit the decoded
// values, and the decoded block's best fit need not be its own codes': fits
// within float noise of each other (one value: every trial fits it exactly)
// pick another, or the decoded block's ties (a max of either sign) another
// first max; and the sub-block scales are quantized against the greatest
// of them again. Counted per block, by the class of its fp16 d, and a
// small block for each (found by shrinking a failing normal(0, 1) one).
// q8_K (its quantizer: Derive's checks realize the derived bsums over the
// codes' region, out of the input's bounds, without the layout) keeps it
// but for f32 rounding and a sign: d = 1 / (-127 / max) rounds twice, so
// requantizing moves d by an ulp (codes the same); and where a value of
// the other sign, before the max, has the max's code magnitude (127), the
// decoded block's first max is that value: codes and d negate (decoding
// alike).
#include "Halide.h"
#include "halide_approximation_testing.h"
#include "schemes/idempotence.h"
#include "schemes/kquants.h"

#include <iostream>

using namespace Halide;
using namespace Halide::ApproximationTesting;

int main() {
    // Each run of 16 (a q2_K, q3_K or q6_K sub-block), of 32 (a q4_K or q5_K
    // one) or of 256 (a
    // super-block) drawn from one of these: super-blocks of sub-blocks of
    // every magnitude, and homogeneous ones. (One JIT compilation per
    // trial, so all in each trial.)
    const int blocks = 256, trials = 3, n = ggml::QK_K * blocks;
    const std::vector<Distribution> parts = {
        Distribution::normal(0, 1),
        Distribution::normal(0, 1e-30),
        Distribution::normal(0, 1e4),
        Distribution::uniform(0.5, 2),
        Distribution::uniform(-3, -1e-3),
        Distribution::outliers(Distribution::normal(0, 1e-3), 32, 1e2),
        Distribution::constant(100),
        Distribution::zeros(),
    };
    const std::vector<std::pair<std::string, Distribution>> dists = {
        {"mixed per 16", Distribution::mixture(parts, 16)},
        {"mixed per 32", Distribution::mixture(parts, 32)},
        {"mixed per 256", Distribution::mixture(parts, ggml::QK_K)},
    };

    // each format's stages before its layout (q8_K's with it), and its
    // quantizer, held: their bounds checked at it
    std::vector<std::tuple<std::string, Approximation, Approximation>> schemes;
    Approximation q8 = ggml::Q8KQuant{};
    schemes.emplace_back("q8_K", q8, Compose{BlockReshape{ggml::QK_K}, q8, Derive{"bsums", {"codes"}, ggml::partial_sums(16)}, ggml::layout("aos", {{"d", Float(32)}, {"qs", Int(8), ggml::QK_K}, {"bsums", Int(16), ggml::QK_K / 16}}, {"qs", "d", "bsums"})});
    for (const auto &[name, format] : {std::pair{"q4_K", ggml::q4_K("aos")}, {"q6_K", ggml::q6_K("aos")}, {"q5_K", ggml::q5_K("aos")}, {"q2_K", ggml::q2_K("aos")}, {"q3_K", ggml::q3_K("aos")}}) {
        std::vector<Approximation> stages = ggml::pre_layout(format);
        schemes.emplace_back(name, stages[1], Compose{stages});
    }
    int failures = 0;
    for (const auto &[scheme, q, a] : schemes) {
        for (const auto &[name, dist] : dists) {
            for (const Property &p : {within_declared_bound().at(q), outputs_within_declared_ranges(), closed()}) {
                PropertyResult r;
                try {
                    r = check_property(a, p, dist, Float(32), {n}, trials, 1);
                } catch (const Halide::Error &e) {
                    std::cout << e.what() << "\n";
                    return 1;
                }
                std::cout << scheme << " (" << name << "): " << r;
                failures += !r.passed;
            }
        }
    }

    // enc dec enc = enc, per block: changes with normal d
    using SC = ggml::ScaleClass;
    for (const auto &[scheme, q, a] : schemes) {
        if (scheme == "q8_K") continue;
        ggml::TallyOver t = ggml::requantize_tally_over(a, "scale", dists, n, 1);
        std::cout << scheme << ": requantizing: " << t.tally.str() << "\n";
        bool ok = t.error.empty() && t.tally.changed_in(SC::Normal) > 0;
        if (!ok) std::cout << t.error << scheme << ": FAILED: expected requantizing to change blocks of normal d\n";
        failures += !ok;
    }
    // the small blocks (index: value; the rest 0), expected to fail
    const std::vector<std::tuple<std::string, int, std::vector<std::pair<int, float>>>> small = {
        // one value: every trial iscale fits it exactly, float noise picks code -31 (d sc = -1.9137 / 31); decoded, -32
        {"q6_K", 2, {{0, 1.91372597f}}},
        // a pair: codes 3 and -4 (of -4..3, the max -1.1 to -4), decoded +-1.0547; their first max, +1.0547, now -4
        {"q3_K", 5, {{0, 1.0f}, {1, -1.1f}}},
        // one sub-block, and two values of others: the 6-bit sub-block scales and mins, quantized against their max, refit
        {"q4_K", 1, {{96, 1.50384939f}, {97, 1.0f}, {98, 0.897379339f}, {99, 0.7f}, {100, 0.6f}, {101, 1.64636242f}, {102, 1.05512631f}, {103, 0.8f}, {104, 1.0f}, {105, 0.6f}, {106, 0.815261006f}, {107, 0.643128872f}, {108, 1.62913334f}, {109, 0.743308306f}, {110, 0.6f}, {111, 1.03469551f}, {112, 0.9f}, {113, 0.64296174f}, {114, 1.70713186f}, {115, 0.7f}, {116, 0.790151417f}, {117, 1.89651942f}, {118, 1.45919132f}, {119, 1.36487627f}, {120, 1.28700542f}, {121, 1.75946784f}, {122, 1.88721192f}, {123, 1.43493819f}, {124, 0.894945145f}, {125, 1.0f}, {126, 0.598941624f}, {127, 1.59201753f}, {232, 2.0f}, {233, 1.38325989f}, {250, 2.0f}}},
        {"q5_K", 3, {{225, 0.9f}, {228, -0.867776752f}, {229, -1.0f}, {232, 0.8f}, {233, -2.0f}, {236, -0.9f}, {237, -1.30977058f}, {238, 2.93072605f}, {239, -1.0f}, {242, 1.26964939f}, {248, -2.879143f}, {249, 1.29775512f}}},
        {"q2_K", 4, {{224, -0.6f}, {225, -0.1f}, {226, -0.8f}, {227, 1.26057148f}, {228, 1.0f}, {229, 0.9f}, {230, -0.8f}, {231, 1.76053691f}, {232, 0.6f}, {233, 1.92459393f}, {234, -1.34685135f}, {235, -0.211814299f}, {236, -1.64426219f}, {237, -0.6f}, {238, -3.71460056f}, {239, 0.7f}}},
    };
    for (const auto &[scheme, k, values] : small) {
        std::vector<float> x(ggml::QK_K, 0.0f);
        for (const auto &[i, v] : values) {
            x[i] = v;
        }
        PropertyResult r = ggml::check_on(std::get<2>(schemes[k]), idempotent_requantize(0), x);
        std::cout << scheme << " (" << values.size() << " nonzero values), expected to fail: " << r;
        failures += r.passed;
    }

    // q8_K's quantizer: per block, the requantized codes and d the same, d an
    // ulp (2^-23 relative) off, or both negated (and d within an ulp)
    struct Q8K {
        int64_t same = 0, ulp = 0, negated = 0, other = 0;
    };
    auto q8k = std::make_shared<Q8K>();
    Property classify("q8_K requantized", [=](const PropertyContext &c) {
        const Buffer<> &q1 = c.encoded()[0], &d1 = c.encoded()[1], &q2 = c.requantized().encoded[0], &d2 = c.requantized().encoded[1];
        for (int b = 0; b < d1.dim(0).extent(); b++) {
            bool same = true, neg = true;
            for (int j = 0; j < ggml::QK_K; j++) {
                double u = Detail::get(q1, std::vector<int>{j, b}.data()), v = Detail::get(q2, std::vector<int>{j, b}.data());
                same = same && u == v, neg = neg && u == -v;
            }
            double x = Detail::get(d1, &b), y = Detail::get(d2, &b);
            auto near = [](double x, double y) { return std::fabs(x - y) <= 0x1p-23 * std::max(std::fabs(x), std::fabs(y)); };
            if (same && x == y) {
                q8k->same++;
            } else if (same && near(x, y)) {
                q8k->ulp++;
            } else if (neg && near(x, -y)) {
                q8k->negated++;
            } else {
                q8k->other++;
            }
        }
        return PropertyOutcome();
    });
    Approximation q8k_quant = Compose{BlockReshape{ggml::QK_K}, ggml::Q8KQuant{}};
    for (const auto &[name, dist] : dists) {
        PropertyResult r = check_property(q8k_quant, classify, dist, Float(32), {n}, trials, 1);
        failures += !r.passed;
    }
    printf("q8_K: requantizing: %lld blocks the same, %lld with d an ulp off, %lld negated, %lld otherwise\n",
           (long long)q8k->same, (long long)q8k->ulp, (long long)q8k->negated, (long long)q8k->other);
    failures += q8k->other || !q8k->ulp || !q8k->negated;
    std::vector<float> x(ggml::QK_K, 0.0f);
    x[0] = -0.999f, x[1] = 1.0f;  // codes 127, -127; decoded -1, 1: first max -1
    PropertyResult r = ggml::check_on(q8k_quant, idempotent_requantize(0), x);
    std::cout << "q8_K {-0.999, 1, 0, ...}, expected to fail: " << r;
    failures += r.passed;

    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
