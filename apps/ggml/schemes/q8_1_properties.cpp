// Properties of q8_1 (schemes/legacy.h), without its layout, whose struct
// records the property checks cannot read back: requantizing is idempotent,
// the derived block sum holds, and every stage is closed. Also measured: with
// s derived GGML's way, from the unrounded scale, requantizing is not
// idempotent, and in how many blocks it is not.
#include "Halide.h"
#include "halide_approximation_testing.h"
#include "schemes/legacy.h"

#include <iostream>
#include <memory>

using namespace Halide;
using namespace Halide::ApproximationTesting;

namespace {

// How many values of the encoded port `name` change on requantizing, of how
// many: idempotent_requantize, counted instead of stopping at the first.
struct Changes {
    int64_t changed = 0, total = 0;
};

Property count_requantize_changes(const std::string &name, std::shared_ptr<Changes> out) {
    return Property("requantize_changes(" + name + ")", [=](const PropertyContext &c) {
        for (size_t p = 0; p < c.encoded().size(); p++) {
            if (c.encoded_ports()[p].name != name) {
                continue;
            }
            const Buffer<> &e1 = c.encoded()[p], &e2 = c.requantized().encoded[p];
            Detail::for_each_coord(e1, [&](const int *pos) {
                out->changed += Detail::get(e1, pos) != Detail::get(e2, pos);
                out->total++;
            });
            return PropertyOutcome();
        }
        return Detail::fail("no encoded port '" + name + "'");
    });
}

}  // namespace

int main() {
    const int blocks = 1024, trials = 8;
    Distribution normal = Distribution::normal(0, 1);
    Approximation consistent = Compose{ggml::q8_1_stages()};
    Approximation ggml_style = Compose{ggml::q8_1_stages(false, true)};

    int failures = 0;
    for (const Property &p : {idempotent_requantize(0), derived_ports_hold(), closed()}) {
        PropertyResult r = check_property(consistent, p, normal, Float(32), {ggml::QK * blocks}, trials, 1);
        std::cout << "q8_1: " << r;
        failures += !r.passed;
    }

    PropertyResult r = check_property(ggml_style, idempotent_requantize(0), normal, Float(32), {ggml::QK * blocks}, trials, 1);
    std::cout << "q8_1, s from the unrounded scale (GGML's): " << r;
    failures += r.passed;

    for (bool ggml_sum : {false, true}) {
        auto changes = std::make_shared<Changes>();
        Approximation a = ggml_sum ? ggml_style : consistent;
        r = check_property(a, count_requantize_changes("sum", changes), normal, Float(32), {ggml::QK * blocks}, trials, 1);
        failures += !r.passed;
        printf("q8_1%s: requantizing changes s in %lld of %lld blocks (%.2f%%)\n",
               ggml_sum ? ", s from the unrounded scale (GGML's)" : "", (long long)changes->changed,
               (long long)changes->total, 100.0 * changes->changed / changes->total);
        failures += ggml_sum ? changes->changed == 0 : changes->changed != 0;
    }

    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
