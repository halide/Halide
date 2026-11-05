// Properties of the K-quants (schemes/kquants.h), without their layouts
// (but q8_K's, which bounds its derived bsums):
// the round trip is within the declared error bound, the codes (and
// sub-block scales) are within their declared ranges, and every stage is
// closed. The blocks are drawn to reach the searches' corners: binades of
// every size, one-signed blocks, blocks dominated by one value, constants
// and zeros.
#include "Halide.h"
#include "halide_approximation_testing.h"
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

    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
