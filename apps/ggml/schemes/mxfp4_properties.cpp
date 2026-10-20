// Properties of mxfp4 (schemes/mx.h), without its layout: the round trip is
// within the declared error bound, the codes are within their declared range,
// requantizing is idempotent, and every stage is closed. The blocks are drawn
// to reach the scale's corners: binades of every size, one-signed blocks,
// blocks dominated by one value, constants and zeros.
#include "Halide.h"
#include "halide_approximation_testing.h"
#include "schemes/mx.h"

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
        {"outliers", Distribution::outliers(Distribution::normal(0, 1e-3), ggml::QK * blocks, 1e4)},
        {"constant", Distribution::constant(100)},
        {"zeros", Distribution::zeros()},
    };

    int failures = 0;
    Approximation q = ggml::MXFP4Quant{};  // held: its bound checked at it
    Approximation a = Compose{BlockReshape{ggml::QK}, q,
                              Parallel{{"codes", ggml::minifloat_integers(ggml::e2m1())}, {"scale", ggml::e8m0_half()}},
                              Parallel{{"codes", PlanarFieldPack{4, ggml::QK / 2}}}};
    for (const auto &[name, dist] : dists) {
        for (const Property &p : {within_declared_bound().at(q), outputs_within_declared_ranges(), idempotent_requantize(0), closed()}) {
            PropertyResult r = check_property(a, p, dist, Float(32), {ggml::QK * blocks}, trials, 1);
            std::cout << "mxfp4 (" << name << "): " << r;
            failures += !r.passed;
        }
    }

    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
