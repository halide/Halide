// Properties of iq4_nl and iq4_xs (schemes/iq.h), without their layouts:
// the round trip is within the declared error bound, the codes (and iq4_xs's
// sub-block scales) are within their declared ranges, and every stage is
// closed. (Not idempotent: requantizing the decoded values, the search may
// fit another scale.) The blocks are drawn to reach the search's corners:
// binades of every size, one-signed blocks, blocks dominated by one value,
// constants and zeros.
#include "Halide.h"
#include "halide_approximation_testing.h"
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

    // held: their bounds checked at them
    Approximation nl = ggml::IQ4Quant{ggml::QK}, xs = ggml::IQ4Quant{ggml::QK_K};
    const std::vector<std::tuple<std::string, Approximation, Approximation>> schemes = {
        {"iq4_nl", nl, Compose{BlockReshape{ggml::QK}, nl, Parallel{{"codes", ggml::iq4nl_codes()}, {"scale", ggml::fp16()}}, Parallel{{"codes", PlanarFieldPack{4, ggml::QK / 2}}}}},
        {"iq4_xs", xs, Compose{BlockReshape{ggml::QK_K}, xs, Parallel{{"codes", ggml::iq4nl_codes()}}, Parallel{{"codes", Compose{BlockReshape{ggml::QK}, PlanarFieldPack{4, ggml::QK / 2}, ggml::Unblock{ggml::QK / 2}}}, {"scales", Compose{ggml::SumSplit{4}, Parallel{{"low", ggml::BitPack{4}}, {"high", Compose{ggml::multiples(4, -32, 4), ggml::BitPack{2}}}}}}, {"scale", ggml::fp16()}}}},
    };
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

    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
