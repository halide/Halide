// Properties of iq4_nl and iq4_xs (schemes/iq.h), without their layouts:
// the round trip is within the declared error bound, the codes (and iq4_xs's
// sub-block scales) are within their declared ranges, and every stage is
// closed. Not idempotent (enc dec enc != enc), with normal scales too: the
// search (16 trial scales, each refit by weighted least squares to the
// non-uniform, asymmetric kvalues grid) on the decoded values may settle on
// another trial, or the refit on another scale; counted per block by the
// class of its fp16 scale, with a small block for each. (A scale of 0, the
// block's max below 1e-15, keeps it.) The blocks are drawn to reach the
// search's corners:
// binades of every size, one-signed blocks, blocks dominated by one value,
// constants and zeros.
#include "Halide.h"
#include "halide_approximation_testing.h"
#include "schemes/idempotence.h"
#include "schemes/iq.h"

#include <iostream>

using namespace Halide;
using namespace Halide::ApproximationTesting;

int main() {
    // Each run of 32 (a block, or an iq4_xs sub-block) or of 256 (a
    // super-block) drawn from one of these.
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
        {"mixed per 32", Distribution::mixture(parts, 32)},
        {"mixed per 256", Distribution::mixture(parts, ggml::QK_K)},
    };

    // each format's stages before its layout, and its quantizer, held: its
    // bound checked at it
    std::vector<std::tuple<std::string, Approximation, Approximation>> schemes;
    for (const auto &[name, format] : {std::pair{"iq4_nl", ggml::iq4_nl("aos")}, {"iq4_xs", ggml::iq4_xs("aos")}}) {
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

    // enc dec enc = enc, per block: changes with normal scales
    for (const auto &[scheme, q, a] : schemes) {
        ggml::TallyOver t = ggml::requantize_tally_over(a, "scale", {dists[0]}, n, 1);
        std::cout << scheme << ": requantizing: " << t.tally.str() << "\n";
        bool ok = t.error.empty() && t.tally.changed_in(ggml::ScaleClass::Normal) > 0 && !t.tally.changed_in(ggml::ScaleClass::Zero);
        if (!ok) std::cout << t.error << scheme << ": FAILED: expected requantizing to change blocks of normal scale (and none of scale 0)\n";
        failures += !ok;
    }
    // a small block (three values; the rest 0), expected to fail: a code changes
    for (const auto &[scheme, q, a] : schemes) {
        std::vector<float> x(ggml::QK_K, 0.0f);
        x[4] = 1.94175363f, x[11] = 1.79283834f, x[28] = -1.73056149f;
        PropertyResult r = ggml::check_on(a, idempotent_requantize(0), x);
        std::cout << scheme << " {x4 = 1.9418, x11 = 1.7928, x28 = -1.7306}, expected to fail: " << r;
        failures += r.passed;
    }

    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
