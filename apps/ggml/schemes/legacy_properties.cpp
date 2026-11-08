// Properties of the legacy formats (schemes/legacy.h: q4_0, q4_1, q5_0,
// q5_1, q8_0, q8_1), without their layouts: the round trip is within the
// declared error bound, the codes (and q8_1's block sum) are within their
// declared ranges, and every stage is closed. (Requantizing: see
// q8_1_properties.cpp.) The blocks are drawn to reach the scales' corners:
// binades of every size, one-signed blocks, blocks dominated by one value,
// large offsets with small spreads (type-1's min), constants and zeros.
#include "Halide.h"
#include "halide_approximation_testing.h"
#include "schemes/schemes.h"

#include <iostream>

using namespace Halide;
using namespace Halide::ApproximationTesting;

int main() {
    const int blocks = 1024, trials = 8;
    const std::vector<std::pair<std::string, Distribution>> dists = {
        {"normal", Distribution::normal(0, 1)},
        {"tiny", Distribution::normal(0, 1e-30)},
        {"huge", Distribution::normal(0, 1e30)},
        {"positive", Distribution::uniform(0.5, 2)},
        {"negative", Distribution::uniform(-3, -1e-3)},
        {"offset", Distribution::uniform(1000, 1000.02)},
        {"far", Distribution::uniform(-1e6 - 1, -1e6)},
        {"outliers", Distribution::outliers(Distribution::normal(0, 1e-3), ggml::QK * blocks, 1e4)},
        {"constant", Distribution::constant(100)},
        {"zeros", Distribution::zeros()},
    };

    int failures = 0;
    for (const char *f : {"q4_0", "q4_1", "q5_0", "q5_1", "q8_0", "q8_1"}) {
        // the stages before the layout; the quantizer held: its bound
        // checked at it
        std::vector<Approximation> stages = ggml::pre_layout(ggml::format(f).scheme);
        Approximation q = stages[1], a = Compose{stages};
        for (const auto &[name, dist] : dists) {
            for (const Property &p : {within_declared_bound().at(q), outputs_within_declared_ranges(), closed()}) {
                PropertyResult r = check_property(a, p, dist, Float(32), {ggml::QK * blocks}, trials, 1);
                std::cout << f << " (" << name << "): " << r;
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
