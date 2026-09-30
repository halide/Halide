#include "Halide.h"
#include "halide_approximation_testing.h"

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

// A per-block absmax quantizer on (within, block) floats: codes in
// [-qmax, qmax] and one float scale per block. Rounding to nearest keeps the
// error within half a scale step (plus slack for float rounding).
struct AbsMaxQuantizer {
    int block, qmax;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Var kk("kk"), blk("blk");
        RDom r(0, block);
        Func amax("absmax_stat"), scale("absmax_scale"), codes("absmax_codes");
        amax(blk) = 0.0f;
        amax(blk) = max(amax(blk), abs(in[0](r, blk)));
        scale(blk) = amax(blk) / (float)qmax;
        codes(kk, blk) = cast<int8_t>(round(in[0](kk, blk) / select(scale(blk) == 0.0f, 1.0f, scale(blk))));
        return {codes, scale};
    }

    std::vector<Func> decode(const std::vector<Func> &encoded) const {
        Var kk("kk"), blk("blk");
        Func out("absmax_decoded");
        out(kk, blk) = cast<float>(encoded[0](kk, blk)) * encoded[1](blk);
        return {out};
    }

    ApproximationSignature signature() const {
        return {{{"block", Float(32), 2}},
                {{"codes", Int(8), 2, ApproximationRange(-qmax, qmax)}, {"scale", Float(32), 1}}};
    }

    Func error_bound(const std::vector<Func> &, const std::vector<Func> &encoded) const {
        Var kk("kk"), blk("blk");
        Func bound("absmax_error_bound");
        bound(kk, blk) = abs(cast<double>(encoded[1](blk))) * Expr(0.5 + qmax / (double)(1 << 21));
        return bound;
    }
};

Approximation make_quantizer(int block, int qmax = 127) {
    return AbsMaxQuantizer{block, qmax};
}

// Signed q4 codes [-8, 7] to stored nibbles [0, 15]: exact within that range.
Approximation make_offset() {
    return Pointwise{"offset",
                     [](Expr x) { return cast<uint8_t>(x + 8); },
                     [](Expr x) { return cast<int8_t>(x - 8); }}
        .with_types(Int(8), UInt(8))
        .with_ranges(ApproximationRange(-8, 7), ApproximationRange(0, 15))
        .with_lossless();
}

// An int8 to its low nibble (uint8) and high nibble (int8): x = high * 16 + low.
struct SplitNibbles {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Var x("x");
        Func low("split_low"), high("split_high");
        low(x) = cast<uint8_t>(in[0](x) & 15);
        high(x) = cast<int8_t>(in[0](x) >> 4);
        return {low, high};
    }
    std::vector<Func> decode(const std::vector<Func> &in) const {
        Var x("x");
        Func out("split_joined");
        out(x) = cast<int8_t>(in[1](x) * 16 + in[0](x));
        return {out};
    }
    ApproximationSignature signature() const {
        return {{{"codes", Int(8), 1}},
                {{"low", UInt(8), 1, ApproximationRange(0, 15)},
                 {"high", Int(8), 1, ApproximationRange(-8, 7)}}};
    }
    bool lossless() const {
        return true;
    }
};

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
    Approximation offset = make_offset();
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
    Approximation offset = make_offset();
    Approximation quant = make_quantizer(16, 7);
    Approximation scheme = Compose{BlockReshape{16}, quant, Parallel{{"codes", Compose{offset, pack}}}};

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
    // Codes [-8, 8] are one too many for the offset's precondition [-8, 7].
    Approximation pack = PlanarFieldPack{4, 8};
    Approximation offset = make_offset();
    Approximation quant = make_quantizer(16, 8);
    Approximation bad = Compose{BlockReshape{16}, quant, Parallel{{"codes", Compose{offset, pack}}}};
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
    // The declared bound holds on hostile input.
    for (int qmax : {7, 127}) {
        Approximation q = make_quantizer(16, qmax);
        for (const Distribution &d : {Distribution::normal(0, 1), Distribution::uniform(-1, 1),
                                      Distribution::outliers(Distribution::uniform(-1, 1), 16, 50)}) {
            CHECK(check_property(q, within_declared_bound(), d, Float(32), {16, 4}, 6, 21).passed);
            CHECK(check_property(q, outputs_within_declared_ranges(), d, Float(32), {16, 4}, 6, 21).passed);
        }
    }
    return 0;
}

// Copies a (possibly struct-typed) Func unchanged.
struct Copy {
    Func encode(const Func &f) const {
        Func g("copy_encode");
        g(_) = f(_);
        return g;
    }
    Func decode(const Func &f) const {
        Func g("copy_decode");
        g(_) = f(_);
        return g;
    }
};

// A Tuple-valued encoded Func: (x, x + 1).
struct TuplePair {
    Func encode(const Func &f) const {
        Func t("tuple_pair");
        t(_) = Tuple(f(_), f(_) + cast<uint8_t>(1));
        return t;
    }
    Func decode(const Func &f) const {
        Func g("tuple_first");
        g(_) = f(_)[0];
        return g;
    }
    ApproximationSignature signature() const {
        return {{{"value", UInt(8), 1}}, {{"pair", std::nullopt, 1, ApproximationRange(0, 255)}}};
    }
    bool lossless() const {
        return true;
    }
};

bool mentions_unreadable(const PropertyResult &r) {
    return !r.passed && r.message.find("cannot be read back") != std::string::npos;
}

// Encoded Funcs that are struct-typed or Tuple-valued cannot be read back:
// properties that need their values fail with a message; the others work.
int test_unreadable_encoded() {
    Type record = Type::Struct({{"low", UInt(8)}, {"high", Int(8)}});
    Approximation split = SplitNibbles{};
    Approximation layout = StructLayout(record, {"low", "high"});
    Approximation copy = Copy{};
    Approximation scheme = Compose{split, layout};

    const std::vector<int> extents = {32};
    const std::vector<InputSpec> codes = {InputSpec{Int(8), extents, Distribution::uniform_int(-16, 15)}};
    CHECK(scheme.lossless());
    PropertyResult lossless_result = check_property(scheme, lossless(), codes, 3, 5);
    std::cout << lossless_result;
    CHECK(lossless_result.passed);
    CHECK(check_property(scheme, bounded_error(0), codes, 2, 5).passed);
    CHECK(check_property(scheme, within_declared_bound(), codes, 2, 5).passed);
    CHECK(check_property(scheme, zero_preserving(), codes, 1, 5).passed);
    CHECK(check_property(scheme, sign_preserving(), codes, 2, 5).passed);

    // The record port declares no range, so there is nothing to check.
    CHECK(check_property(scheme, outputs_within_declared_ranges(), codes, 2, 5).passed);

    PropertyResult idem = check_property(scheme, idempotent_requantize(), codes, 2, 5);
    std::cout << idem;
    CHECK(mentions_unreadable(idem));
    CHECK(idem.message.find("'record'") != std::string::npos && idem.message.find("struct-typed") != std::string::npos);

    // A stage whose input is struct-typed cannot be targeted, but others can.
    Approximation with_copy = Compose{split, layout, copy};
    CHECK(check_property(with_copy, lossless(), codes, 2, 5).passed);
    PropertyResult at_copy = check_property(with_copy, lossless().at(copy), codes, 2, 5);
    std::cout << at_copy;
    CHECK(mentions_unreadable(at_copy));
    CHECK(check_property(with_copy, lossless().at(split), codes, 2, 5).passed);
    CHECK(check_property(with_copy, lossless().at(layout), codes, 2, 5).passed);

    // The layout on its own, over two inputs of different types.
    Approximation two = StructLayout(record, {"low", "high"});
    std::vector<InputSpec> specs = {InputSpec{UInt(8), extents, Distribution::uniform_int(0, 255)},
                                    InputSpec{Int(8), extents, Distribution::uniform_int(-128, 127)}};
    CHECK(check_property(two, lossless(), specs, 2, 5).passed);
    RoundTripReport report = verify_round_trip(two, {generate(specs[0].dist, UInt(8), extents, 1),
                                                     generate(specs[1].dist, Int(8), extents, 2)});
    CHECK(report.max_abs_error == 0);

    // Tuple-valued.
    Approximation tuple = TuplePair{};
    CHECK(check_property(tuple, lossless(), extents, 2, 5).passed);
    CHECK(mentions_unreadable(check_property(tuple, idempotent_requantize(), extents, 2, 5)));
    PropertyResult ranges = check_property(tuple, outputs_within_declared_ranges(), extents, 2, 5);
    CHECK(mentions_unreadable(ranges) && ranges.message.find("Tuple-valued") != std::string::npos);
    return 0;
}

// SplitNibbles' declared output ranges hold for every code.
int test_split_ranges() {
    Approximation split = SplitNibbles{};
    CHECK(check_property(split, outputs_within_declared_ranges(), {InputSpec{Int(8), {256}, Distribution::uniform_int(-128, 127)}}, 4, 3).passed);
    CHECK(check_property(split, lossless(), {InputSpec{Int(8), {256}, Distribution::uniform_int(-128, 127)}}, 4, 3).passed);
    CHECK(check_property(split, idempotent_requantize(), {InputSpec{Int(8), {256}, Distribution::uniform_int(-128, 127)}}, 2, 3).passed);
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    int (*tests[])() = {test_generators, test_round_trip_report, test_lossless, test_precondition_conditioning,
                        test_stage_targeting, test_idempotent_requantize, test_range_diagnostics,
                        test_declared_bound_is_exact, test_unreadable_encoded, test_split_ranges};
    for (auto t : tests) {
        if (t()) {
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}
