// Properties of mxfp4 and nvfp4 (schemes/mx.h), without their layouts: the
// round trip is within the declared error bound, the codes are within their
// declared range, requantizing is idempotent (mxfp4: its scales are powers of
// two; nvfp4's ue4m3 scale of a decoded sub-block may round elsewhere), and
// every stage is closed. The blocks are drawn to reach the scales' corners:
// binades of every size (nvfp4: per sub-block too), one-signed blocks, blocks
// dominated by one value, constants and zeros.
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
    std::vector<Distribution> parts;
    for (const auto &[name, dist] : dists) {
        parts.push_back(dist);
    }

    int failures = 0;
    Approximation q = ggml::MXFP4Quant{};  // held: its bound checked at it
    Approximation a = Compose{BlockReshape{ggml::QK}, q,
                              Parallel{{"codes", ggml::e2m1_codes()}, {"scale", ggml::e8m0_half()}},
                              Parallel{{"codes", PlanarFieldPack{4, ggml::QK / 2}}}};
    for (const auto &[name, dist] : dists) {
        for (const Property &p : {within_declared_bound().at(q), outputs_within_declared_ranges(), idempotent_requantize(0), closed()}) {
            PropertyResult r = check_property(a, p, dist, Float(32), {ggml::QK * blocks}, trials, 1);
            std::cout << "mxfp4 (" << name << "): " << r;
            failures += !r.passed;
        }
    }

    const int n = ggml::NVFP4Quant::n, block = ggml::NVFP4Quant::block;
    Approximation nq = ggml::NVFP4Quant{};
    Approximation nv = Compose{BlockReshape{block}, nq, Parallel{{"codes", ggml::e2m1_codes()}, {"scales", ggml::ue4m3_half()}},
                               Parallel{{"codes", Compose{BlockReshape{n}, PlanarFieldPack{4, n / 2}, ggml::Unblock{n / 2}}}}};
    std::vector<std::pair<std::string, Distribution>> nvd = dists;
    nvd.emplace_back("mixed per 16", Distribution::mixture(parts, n));
    nvd.emplace_back("binades per 16", Distribution::mixture({Distribution::normal(0, 1e-3), Distribution::normal(0, 1), Distribution::normal(0, 3e2), Distribution::normal(0, 3e-3)}, n));
    for (const auto &[name, dist] : nvd) {
        for (const Property &p : {within_declared_bound().at(nq), outputs_within_declared_ranges(), closed()}) {
            PropertyResult r = check_property(nv, p, dist, Float(32), {block * blocks}, trials, 1);
            std::cout << "nvfp4 (" << name << "): " << r;
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
