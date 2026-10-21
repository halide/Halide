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

    // held: their bounds checked at them
    Approximation q8 = ggml::Q8KQuant{}, q4 = ggml::q4_K_quant(), q6 = ggml::Q6KQuant{};
    Approximation q5 = ggml::q5_K_quant(), q2 = ggml::q2_K_quant(), q3 = ggml::Q3KQuant{};
    const std::vector<std::tuple<std::string, Approximation, Approximation>> schemes = {
        {"q8_K", q8, Compose{BlockReshape{ggml::QK_K}, q8, Derive{"bsums", {"codes"}, ggml::partial_sums(16)}, ggml::layout("aos", {{"d", Float(32)}, {"qs", Int(8), ggml::QK_K}, {"bsums", Int(16), ggml::QK_K / 16}}, {"qs", "d", "bsums"})}},
        {"q4_K", q4, Compose{BlockReshape{ggml::QK_K}, q4, Parallel{{"codes", ggml::unsigned_codes(4)}}, Parallel{{"codes", ggml::q4_K_nibbles()}, {"scales", ggml::KScales{}}, {"scale", ggml::fp16()}, {"min", ggml::fp16()}}}},
        {"q6_K", q6, Compose{BlockReshape{ggml::QK_K}, q6, Parallel{{"codes", ggml::SumSplit{4}}}, Parallel{{"low", Compose{BlockReshape{128}, PlanarFieldPack{4, 64}, ggml::Unblock{64}}}, {"high", Compose{ggml::multiples(4, -32, 4), BlockReshape{128}, PlanarFieldPack{2, 32}, ggml::Unblock{32}}}, {"scale", ggml::fp16()}}}},
        {"q5_K", q5, Compose{BlockReshape{ggml::QK_K}, q5, Parallel{{"codes", ggml::SumSplit{4}}}, Parallel{{"low", ggml::q4_K_nibbles()}, {"high", Compose{ggml::multiples(4, 0, 2), PlanarFieldPack{1, 32}}}, {"scales", ggml::KScales{}}, {"scale", ggml::fp16()}, {"min", ggml::fp16()}}}},
        {"q2_K", q2, Compose{BlockReshape{ggml::QK_K}, q2, Parallel{{"codes", ggml::unsigned_codes(2)}}, Parallel{{"codes", ggml::two_bit_planes()}, {"scales", PlanarFieldPack{4, 16}}, {"scale", ggml::fp16()}, {"min", ggml::fp16()}}}},
        {"q3_K", q3, Compose{BlockReshape{ggml::QK_K}, q3, Parallel{{"codes", ggml::SumSplit{2}}}, Parallel{{"low", ggml::two_bit_planes()}, {"high", Compose{ggml::multiples(2, -4, 2), PlanarFieldPack{1, 32}}}}, Parallel{{"scales", Compose{ggml::SumSplit{4}, Parallel{{"low", PlanarFieldPack{4, 8}}, {"high", Compose{ggml::multiples(4, -32, 4), PlanarFieldPack{2, 4}}}}}}, {"scale", ggml::fp16()}}}},
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
