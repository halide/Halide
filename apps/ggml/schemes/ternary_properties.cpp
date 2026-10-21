// Properties of tq1_0 and tq2_0 (schemes/ternary.h), without their layouts:
// the round trip is within the declared error bound, the codes are within
// their declared range, and every stage is closed. The blocks are drawn to
// reach the quantizer's corners: binades of every size, one-signed blocks,
// blocks dominated by one value, constants and zeros.
#include "Halide.h"
#include "halide_approximation_testing.h"
#include "schemes/ternary.h"

#include <iostream>

using namespace Halide;
using namespace Halide::ApproximationTesting;

int main() {
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

    // held: its bound checked at it
    Approximation q = ggml::TernaryQuant{};
    const std::vector<std::tuple<std::string, Approximation>> schemes = {
        {"tq2_0", Compose{BlockReshape{ggml::QK_K}, q, Parallel{{"codes", ggml::multiples(0, -1, 4)}, {"scale", ggml::fp16()}}, Parallel{{"codes", ggml::two_bit_planes()}}}},
        {"tq1_0", Compose{BlockReshape{ggml::QK_K}, q, Parallel{{"codes", ggml::multiples(0, -1, 3)}, {"scale", ggml::fp16()}}, Parallel{{"codes", ggml::Segments{{{"qs_lo", 160}, {"qs_hi", 80}, {"qh", 16}}}}}, Parallel{{"qs_lo", ggml::TritPack{32}}, {"qs_hi", ggml::TritPack{16}}, {"qh", ggml::TritPack{4, 4}}}}},
    };
    int failures = 0;
    for (const auto &[scheme, a] : schemes) {
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
