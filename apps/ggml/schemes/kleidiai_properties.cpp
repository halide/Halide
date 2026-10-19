// Properties of KleidiAI's qsi8cx and qai8dx (schemes/kleidiai.h), on rows
// of K values: the round trip is within the declared error bound (half a
// step for qsi8cx, half a step plus 2^-10 for qai8dx: the zero point's
// clipping argued there), the codes and offsets are within their declared
// ranges, qsi8cx's derived row sum holds, and every stage is closed. The
// rows are drawn to reach the bounds' corners: one-signed rows (the zero
// point at a range end), rows dominated by one value, constants and zeros.
#include "Halide.h"
#include "halide_approximation_testing.h"
#include "schemes/kleidiai.h"

#include <iostream>

using namespace Halide;
using namespace Halide::ApproximationTesting;

int main() {
    const int blocks = 8, rows = 8, trials = 8;
    const std::vector<std::pair<std::string, Distribution>> dists = {
        {"normal", Distribution::normal(0, 1)},
        {"positive", Distribution::uniform(0.5, 2)},
        {"negative", Distribution::uniform(-3, -1e-3)},
        {"skewed", Distribution::uniform(-1e-2, 1e3)},
        {"outliers", Distribution::outliers(Distribution::normal(0, 1e-3), ggml::QK * blocks, 1e4)},
        {"constant", Distribution::constant(100)},
        {"zeros", Distribution::zeros()},
    };

    int failures = 0;
    for (const char *lay : {"aos", "4x8"}) {
        // as ggml::qsi8cx and qai8dx, holding the quantizers (their bounds)
        Approximation wq = ggml::Qsi8cxQuant{blocks}, aq = ggml::Qai8dxQuant{blocks};
        Approximation w = Compose{BlockReshape{ggml::QK}, wq, ggml::row_sum(blocks), ggml::row_codes_layout(lay)};
        Approximation a = Compose{BlockReshape{ggml::QK}, aq, ggml::row_codes_layout(lay)};
        for (const auto &[name, dist] : dists) {
            for (const Property &p : {within_declared_bound().at(wq), outputs_within_declared_ranges(), derived_ports_hold(), closed()}) {
                PropertyResult r = check_property(w, p, dist, Float(32), {ggml::QK * blocks, rows}, trials, 1);
                std::cout << "qsi8cx (" << lay << ", " << name << "): " << r;
                failures += !r.passed;
            }
            for (const Property &p : {within_declared_bound().at(aq), outputs_within_declared_ranges(), closed()}) {
                PropertyResult r = check_property(a, p, dist, Float(32), {ggml::QK * blocks, rows}, trials, 1);
                std::cout << "qai8dx (" << lay << ", " << name << "): " << r;
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
