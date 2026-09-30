#include "Halide.h"

#include <cstdio>
#include <sstream>

using namespace Halide;
using namespace Halide::ApproximationTesting;

namespace {

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                       \
        }                                                                   \
    } while (0)

bool same_buffers(const Buffer<> &a, const Buffer<> &b) {
    if (a.number_of_elements() != b.number_of_elements() || a.type() != b.type()) {
        return false;
    }
    return memcmp(a.data(), b.data(), a.size_in_bytes()) == 0;
}

int test_generators() {
    // Pinned so that generated data is reproducible across platforms.
    uint64_t state = 0;
    CHECK(splitmix64(state) == 0xe220a8397b1dcdafULL);

    std::vector<Distribution> dists = {
        Distribution::uniform(-1, 1),
        Distribution::uniform_int(-5, 5),
        Distribution::normal(0, 1),
        Distribution::constant(3),
        Distribution::zeros(),
        Distribution::blockwise_constant(4, Distribution::uniform(0, 1)),
        Distribution::outliers(Distribution::normal(0, 1), 8, 100),
        Distribution::mixture({Distribution::zeros(), Distribution::normal(0, 1)}, 4),
    };
    for (const Distribution &d : dists) {
        Buffer<> a = generate(d, Float(32), {32, 3}, 7);
        Buffer<> b = generate(d, Float(32), {32, 3}, 7);
        CHECK(same_buffers(a, b));
        CHECK(a.dimensions() == 2 && a.dim(0).extent() == 32 && a.dim(1).extent() == 3);
    }
    // Only the random ones differ across seeds.
    for (int i : {0, 1, 2, 5, 6, 7}) {
        CHECK(!same_buffers(generate(dists[i], Float(32), {32, 3}, 1), generate(dists[i], Float(32), {32, 3}, 2)));
    }

    // Ranges and structure.
    Buffer<float> u = generate(Distribution::uniform(2, 3), Float(32), {200}, 1);
    for (int i = 0; i < 200; i++) {
        CHECK(u(i) >= 2 && u(i) <= 3);
    }
    Buffer<int8_t> ui = generate(Distribution::uniform_int(-3, 4), Int(8), {200}, 1);
    for (int i = 0; i < 200; i++) {
        CHECK(ui(i) >= -3 && ui(i) <= 4);
    }
    Buffer<float> bc = generate(Distribution::blockwise_constant(4, Distribution::uniform(0, 1)), Float(32), {16}, 3);
    for (int i = 0; i < 16; i++) {
        CHECK(bc(i) == bc(i / 4 * 4));
    }
    Buffer<float> ol = generate(Distribution::outliers(Distribution::uniform(-1, 1), 8, 100), Float(32), {16}, 3);
    for (int b = 0; b < 2; b++) {
        int big = 0;
        for (int i = 0; i < 8; i++) {
            big += std::abs(ol(b * 8 + i)) >= 50;
        }
        CHECK(big == 1);
    }
    Buffer<uint8_t> ex = generate(Distribution::extremes(UInt(8)), UInt(8), {64}, 1);
    for (int i = 0; i < 64; i++) {
        CHECK(ex(i) == 0 || ex(i) == 255);
    }
    return 0;
}

Approximation make_quantizer(int block, BlockRoundingMode rounding = BlockRoundingMode::Nearest,
                             BlockScaleAnchor anchor = BlockScaleAnchor::AbsMax, int qmax = 127) {
    return SymmetricBlockQuantize{block, qmax, rounding, anchor};
}

int test_round_trip_report() {
    // (within, block) input: 4 blocks of 32.
    Approximation q = make_quantizer(32);
    RoundTripReport r = verify_round_trip(q, Distribution::normal(0, 1), Float(32), {32, 4}, 42);
    std::cout << r;
    CHECK(r.count == 128);
    CHECK(r.seed == 42);
    CHECK(r.bound_declared);
    CHECK(r.bound_violations == 0);
    CHECK(r.max_abs_error > 0 && r.max_abs_error < 0.05);
    CHECK(r.rmse > 0 && r.rmse <= r.max_abs_error);
    CHECK(!r.distribution.empty());

    // Adversarial inputs still respect the declared bound.
    for (const Distribution &d : {Distribution::zeros(),
                                  Distribution::outliers(Distribution::normal(0, 1), 32, 1000),
                                  Distribution::blockwise_constant(32, Distribution::uniform(-4, 4))}) {
        RoundTripReport rr = verify_round_trip(q, d, Float(32), {32, 4}, 5);
        CHECK(rr.bound_violations == 0);
    }

    // Same seed, same report.
    RoundTripReport r2 = verify_round_trip(q, Distribution::normal(0, 1), Float(32), {32, 4}, 42);
    CHECK(r2.max_abs_error == r.max_abs_error && r2.rmse == r.rmse);
    return 0;
}

int test_lossless() {
    Approximation reshape = BlockReshape{8};
    CHECK(check_property(reshape, lossless(), Distribution::normal(0, 1), Float(32), {64}, 4, 1).passed);
    CHECK(check_property(reshape, lossless(), Distribution::uniform_int(-100, 100), Int(16), {64}, 4, 1).passed);
    return 0;
}

// The bit-packing unit at the heart of the motivating example: 4-bit fields
// are exact only in [0, 15].
int test_precondition_conditioning() {
    Approximation pack = PlanarFieldPack{4, 8};
    CHECK(pack.signature().inputs[0].range.has_value());

    // (a) Inputs generated within the declared precondition.
    PropertyResult ok = check_property(pack, lossless(), {InputSpec{UInt(8), {16, 4}, Distribution::uniform_int(0, 15)}}, 4, 3);
    std::cout << ok;
    CHECK(ok.passed);

    // With no generator given, inputs come from the declared port (type and
    // range): here the whole valid range of int8 codes.
    Approximation offset = AdditiveOffset<int8_t, uint8_t>{8};
    CHECK(check_property(offset, lossless(), {16, 4}, 4, 3).passed);

    // Outside it: by default the precondition failure is reported ...
    std::vector<InputSpec> wide = {InputSpec{UInt(8), {16, 4}, Distribution::uniform_int(0, 255)}};
    PropertyResult pre = check_property(pack, lossless(), wide, 4, 3);
    CHECK(!pre.passed && pre.precondition_violated);

    // ... and with checking off, the property itself fails (without aborting).
    PropertyOptions opts;
    opts.check_preconditions = false;
    PropertyResult bad = check_property(pack, lossless(), wide, 4, 3, opts);
    std::cout << bad;
    CHECK(!bad.passed && !bad.precondition_violated);
    CHECK(!bad.failing_coord.empty());

    // The failing seed reproduces the failure in one trial.
    PropertyResult again = check_property(pack, lossless(), wide, 1, bad.failing_seed, opts);
    CHECK(!again.passed && again.trials_run == 1);
    CHECK(again.failing_coord == bad.failing_coord);
    CHECK(again.message == bad.message);
    return 0;
}

// Quantize (4-bit, symmetric) -> shift codes to [0, 15] -> pack in 4 bits.
// The packing is exact only because the quantizer guarantees its codes.
int test_stage_targeting() {
    Approximation pack = PlanarFieldPack{4, 8};
    Approximation offset = AdditiveOffset<int8_t, uint8_t>{8};
    Approximation quant = SymmetricBlockQuantize{16, 8, BlockRoundingMode::TruncateHalfUpWithOffset,
                                                 BlockScaleAnchor::ExtremeSignedValue};
    Approximation scheme = Compose{Apply{0, Compose{pack, offset}}, quant, BlockReshape{16}};

    CHECK(check_ranges(scheme).empty());
    std::cout << scheme.describe();

    // Packing alone, on arbitrary bytes, is not lossless...
    // ...but the values that actually reach it here are in [0, 15].
    Distribution normal = Distribution::normal(0, 1);
    PropertyResult at_pack = check_property(scheme, lossless().at(pack), normal, Float(32), {64}, 8, 11);
    std::cout << at_pack;
    CHECK(at_pack.passed);
    CHECK(at_pack.stage_label == pack.label());

    PropertyResult at_offset = check_property(scheme, lossless().at(offset), normal, Float(32), {64}, 8, 11);
    CHECK(at_offset.passed);

    CHECK(check_property(scheme, outputs_within_declared_ranges(), normal, Float(32), {64}, 8, 11).passed);
    CHECK(check_property(scheme, outputs_within_declared_ranges().at(quant), normal, Float(32), {64}, 8, 11).passed);
    return 0;
}

int test_idempotent_requantize() {
    Approximation q = make_quantizer(32);
    PropertyResult r = check_property(q, idempotent_requantize(), Distribution::normal(0, 1), Float(32), {32, 4}, 6, 9);
    std::cout << r;
    CHECK(r.passed);
    CHECK(check_property(q, within_declared_bound(), Distribution::normal(0, 3), Float(32), {32, 4}, 6, 9).passed);
    CHECK(check_property(q, outputs_within_declared_ranges(), Distribution::normal(0, 3), Float(32), {32, 4}, 6, 9).passed);
    CHECK(check_property(q, zero_preserving(), Distribution::zeros(), Float(32), {32, 4}, 2, 1).passed);
    CHECK(check_property(q, sign_preserving(), Distribution::normal(0, 1), Float(32), {32, 4}, 4, 1).passed);
    CHECK(!check_property(q, lossless(), Distribution::normal(0, 1), Float(32), {32, 4}, 2, 1).passed);
    CHECK(!check_property(q, bounded_error(1e-9), Distribution::normal(0, 1), Float(32), {32, 4}, 2, 1).passed);
    return 0;
}

int test_range_diagnostics() {
    // Codes [-8, 8] shifted by 8 are [0, 16]: one too many for 4 bits.
    Approximation pack = PlanarFieldPack{4, 8};
    Approximation offset = AdditiveOffset<int8_t, uint8_t>{8};
    Approximation quant = SymmetricBlockQuantize{16, 8, BlockRoundingMode::Nearest, BlockScaleAnchor::AbsMax};
    Approximation bad = Compose{Apply{0, Compose{pack, offset}}, quant, BlockReshape{16}};
    std::vector<std::string> issues = check_ranges(bad);
    for (const std::string &s : issues) {
        std::cout << "issue: " << s << "\n";
    }
    CHECK(!issues.empty());
    std::string text = bad.describe();
    std::cout << text;
    CHECK(text.find("not within") != std::string::npos);
    return 0;
}

int test_declared_bound_is_exact() {
    // Every rounding mode with a declared bound honours it on hostile input.
    for (BlockRoundingMode m : {BlockRoundingMode::Nearest, BlockRoundingMode::NearestEvenClampedHigh,
                                BlockRoundingMode::TruncateHalfUpWithOffset}) {
        for (BlockScaleAnchor a : {BlockScaleAnchor::AbsMax, BlockScaleAnchor::ExtremeSignedValue}) {
            Approximation q = make_quantizer(16, m, a, 8);
            for (const Distribution &d : {Distribution::normal(0, 1), Distribution::uniform(-1, 1),
                                          Distribution::outliers(Distribution::uniform(-1, 1), 16, 50)}) {
                CHECK(check_property(q, within_declared_bound(), d, Float(32), {16, 4}, 6, 21).passed);
                CHECK(check_property(q, outputs_within_declared_ranges(), d, Float(32), {16, 4}, 6, 21).passed);
            }
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    int (*tests[])() = {test_generators, test_round_trip_report, test_lossless, test_precondition_conditioning,
                        test_stage_targeting, test_idempotent_requantize, test_range_diagnostics,
                        test_declared_bound_is_exact};
    for (auto t : tests) {
        if (t()) {
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}
