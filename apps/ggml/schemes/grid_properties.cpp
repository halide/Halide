// Properties of the grid formats (schemes/grid.h). Their encoders are GGML's
// quantizers, called through an extern stage (TrustedInverse, linked here from
// the harness: harness/trusted.cpp, the JIT resolving ggml_trusted_quantize in
// this process). Checked:
// - Each full scheme (GGML's quantizer, then the native decode): every
//   decode stage's outputs are within their declared ranges, codes within the
//   grid's, odd scales within 1..2^bits-1. (Not closed(): it counts every
//   Buffer read as a foreign input, the embedded grid tables included.)
// - Each grid and sign table is a bijection from its indices: the inverse of
//   its decode, Lattice's native encode (the point's index, an exact match;
//   the nearest-point search is what GGML's quantizer does, trusted), is
//   lossless over all of its indices, through the Tabulate table and without
//   it.
// GGML's exactness (encode and decode bit-exact against quantize_iq* and
// dequantize_row_iq*) is the harness's codec check.
#include "Halide.h"
#include "halide_approximation_testing.h"
#include "schemes/grid.h"

#include <iostream>

using namespace Halide;
using namespace Halide::ApproximationTesting;

int main() {
    const int blocks = 64, trials = 2, n = ggml::QK_K * blocks;
    const std::vector<Distribution> parts = {
        Distribution::normal(0, 1),
        Distribution::normal(0, 1e-3),
        Distribution::normal(0, 1e3),
        Distribution::uniform(0.5, 2),
        Distribution::uniform(-3, -1e-3),
        Distribution::outliers(Distribution::normal(0, 1e-2), 32, 1e2),
        Distribution::constant(3),
        Distribution::zeros(),
    };
    const Distribution mixed = Distribution::mixture(parts, ggml::QK_K);

    int failures = 0;
    auto report = [&](const std::string &name, const PropertyResult &r) {
        std::cout << name << ": " << r;
        failures += !r.passed;
    };

    const std::vector<std::pair<std::string, Approximation>> schemes = {
        {"iq2_xxs", ggml::iq2_xxs("aos")},
        {"iq2_xs", ggml::iq2_xs("aos")},
        {"iq2_s", ggml::iq2_s("aos")},
        {"iq3_xxs", ggml::iq3_xxs("aos")},
        {"iq3_s", ggml::iq3_s("aos")},
        {"iq1_s", ggml::iq1_s("aos")},
        {"iq1_m", ggml::iq1_m("aos")},
    };
    for (const auto &[name, a] : schemes) {
        for (const Property &p : {outputs_within_declared_ranges()}) {
            try {
                report(name, check_property(a, p, mixed, Float(32), {n}, trials, 1));
            } catch (const Halide::Error &e) {
                std::cout << name << ": " << e.what() << "\n";
                return 1;
            }
        }
    }

    // index -> codes -> index, over every index (16 uniform draws per entry
    // reach them all), indices per (group, block)
    std::vector<std::tuple<std::string, Approximation, int, Type>> bijections;
    auto lattice = [&](const std::string &name, const std::vector<uint16_t> &points, int bits, int per, const std::vector<int> &levels) {
        Approximation g = ggml::grid(name, points, bits, per, levels);
        Type t = ggml::Lattice{name, points, bits, per}.index();
        bijections.emplace_back(name, ggml::Inverse{g}, (int)points.size(), t);
        bijections.emplace_back(name + " (untabulated)", ggml::Inverse{Compose{ggml::codebook(name + "_levels", levels), ggml::Lattice{name, points, bits, per}}}, (int)points.size(), t);
    };
    lattice("kgrid_2bit_256", ggml::kgrid_2bit_256(), 2, 8, {8, 25, 43});
    lattice("kgrid_2bit_512", ggml::kgrid_2bit_512(), 2, 8, {8, 25, 43});
    lattice("kgrid_2bit_1024", ggml::kgrid_2bit_1024(), 2, 8, {8, 25, 43});
    lattice("kgrid_256", ggml::kgrid_256(), 3, 4, {4, 12, 20, 28, 36, 44, 52, 62});
    lattice("kgrid_512", ggml::kgrid_512(), 3, 4, {1, 3, 5, 7, 9, 11, 13, 15});
    lattice("kgrid_1bit_2048", ggml::kgrid_1bit_2048(), 2, 8, {-1, 0, 1});
    bijections.emplace_back("even signs", ggml::Inverse{ggml::even_signs(true)}, 128, UInt(8));
    bijections.emplace_back("even signs (untabulated)", ggml::Inverse{ggml::even_signs(false)}, 128, UInt(8));
    for (const auto &[name, a, entries, t] : bijections) {
        Distribution d = Distribution::uniform_int(0, entries - 1);
        for (const Property &p : {lossless()}) {
            try {
                report(name, check_property(a, p, d, t, {16, entries}, 1, 2));
            } catch (const Halide::Error &e) {
                std::cout << name << ": " << e.what() << "\n";
                return 1;
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
