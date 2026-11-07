// Properties of q8_1 (schemes/legacy.h), without its layout, whose struct
// records the property checks cannot read back: requantizing is idempotent,
// the derived block sum holds, and every stage is closed. Also measured: with
// s derived GGML's way, from the unrounded scale, requantizing is not
// idempotent, and in how many blocks it is not.
//
// Then, for every legacy format (q4_0, with its other code encodings and
// scaled codes, q4_1, q5_0, q5_1, q8_0, q8_1; and the layouts' Interleave),
// where requantizing is idempotent (enc dec enc = enc), block by block: in
// exactly the blocks whose fp16 scale is finite and not 0, normal or
// subnormal. The extreme value's code is the extreme code (type-0: -2^(b-1),
// or +-127; type-1: 0 and 2^b - 1), so the decoded block's extreme is that
// code times the stored scale D, exactly, and the scale fit to it is D again.
// It fails in every block whose scale underflows to 0 (the codes were
// computed from the unrounded scale, the decoded block is 0, or type-1's
// min, and requantizes to codes 0, or 2^(b-1)) or overflows to inf. For
// q4_0: blocks of max |x| <= 2^-22 underflow (d = 2^-25 rounds to 0), and of
// max |x| >= 524160 overflow (d = 65520 rounds to inf).
//
// Except type-1 (q4_1, q5_1): its decoded values, q d + m, are rounded to
// f32, and where d is below a few f32 ulps of the block (a large offset, a
// small spread; or m rounding up across a binade), requantizing them can
// change the codes and d. It holds where every q d + m is a float, or d is
// at least 2^9 f32 ulps of the largest; tested over adversarial blocks
// (ggml::offset_blocks), with type-0 as a control.
#include "Halide.h"
#include "halide_approximation_testing.h"
#include "schemes/idempotence.h"
#include "schemes/schemes.h"

#include <iostream>
#include <memory>
#include <string>
#include <tuple>

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

    // enc dec enc = enc per block, by its scale: every legacy format
    const std::vector<std::pair<std::string, Distribution>> domains = {
        {"normal", normal},
        {"positive", Distribution::uniform(0.5, 2)},
        {"negative", Distribution::uniform(-3, -1e-3)},
        {"offset", Distribution::uniform(1000, 1000.02)},
        {"outliers", Distribution::outliers(Distribution::normal(0, 1e-3), 32, 1e2)},
        {"constant", Distribution::constant(100)},
        {"zeros", Distribution::zeros()},
        {"big", Distribution::normal(0, 1e4)},
        {"subnormal scales", Distribution::normal(0, 1e-5)},
        {"underflowing scales", Distribution::normal(0, 1e-8)},
        {"overflowing scales", Distribution::normal(0, 1e6)},
    };
    std::vector<std::pair<std::string, Approximation>> legacy;
    for (const char *f : {"q4_0", "q4_0.i4", "q4_1", "q5_0", "q5_1", "q8_0", "q8_1"}) {
        legacy.emplace_back(f, Compose{ggml::pre_layout(ggml::format(f).scheme)});
    }
    legacy.emplace_back("q4_0 (scaled codes)", Compose{ggml::pre_layout(ggml::format("q4_0", false, true).scheme)});
    for (const auto &[name, a] : legacy) {
        ggml::TallyOver t = ggml::requantize_tally_over(a, "scale", domains, ggml::QK * 256, 2);
        std::cout << name << ": requantizing: " << t.tally.str() << "\n";
        bool ok = t.error.empty() && t.tally.holds_where_scale_finite_nonzero() &&
                  t.tally.blocks_in(ggml::ScaleClass::Subnormal) && t.tally.blocks_in(ggml::ScaleClass::Underflow);
        if (!ok) std::cout << t.error << name << ": FAILED: not idempotent exactly where the scale is finite and not 0\n";
        failures += !ok;
    }

    // q4_0's edges: max |x| = 2^-22 (d = 2^-25: fp16 0), just above it (the
    // least subnormal), just below 524160 (65504) and at it (d = 65520: inf)
    const Approximation &q4_0 = legacy[0].second;
    auto edges = std::make_shared<ggml::RequantizeTally>();
    r = ggml::check_on(q4_0, ggml::requantize_tally("scale", edges),
                       ggml::blocks_of(ggml::QK, {{0x1p-22f}, {std::nextafter(0x1p-22f, 1.0f)}, {std::nextafter(524160.0f, 0.0f)}, {524160.0f}}));
    std::cout << "q4_0 at its scale's edges: " << edges->str() << "\n";
    using SC = ggml::ScaleClass;
    bool ok = r.passed && edges->blocks_in(SC::Underflow) == 1 && edges->blocks_in(SC::Subnormal) == 1 && edges->blocks_in(SC::Normal) == 1 &&
              edges->blocks_in(SC::NonFinite) == 1 && edges->holds_where_scale_finite_nonzero();
    failures += !ok;

    // Two blocks that decode alike, both 0 (their d, -2^-26, fp16 -0), but
    // are encoded differently (codes -8, and -8 then -4s): requantizing
    // cannot return both. Neither is a fixed point.
    std::vector<float> a(ggml::QK, 0x1p-23f), b(ggml::QK, 0x1p-24f);
    b[0] = 0x1p-23f;
    a.insert(a.end(), b.begin(), b.end());
    r = ggml::check_on(q4_0, Property("same decode, other encoding", [](const PropertyContext &c) {
                           const Buffer<> &d = c.decoded()[0];
                           bool zero = true, differ = false;
                           Detail::for_each_coord(d, [&](const int *pos) { zero = zero && Detail::get(d, pos) == 0; });
                           for (const Buffer<> &e : c.encoded()) {
                               Detail::for_each_coord(e, [&](const int *pos) {
                                   std::vector<int> other(pos, pos + e.dimensions());
                                   other.back() = 1 - other.back();
                                   differ = differ || Detail::get(e, pos) != Detail::get(e, other.data());
                               });
                           }
                           return zero && differ ? PropertyOutcome() : Detail::fail("decoded not 0, or encoded alike");
                       }),
                       a);
    std::cout << "q4_0 {2^-23 x 32} and {2^-23, 2^-24 x 31}: " << r;
    failures += !r.passed;
    r = ggml::check_on(q4_0, idempotent_requantize(0), a);
    std::cout << "q4_0 {2^-23 x 32} and {2^-23, 2^-24 x 31}, expected to fail: " << r;
    failures += r.passed;

    // q4_1's scale underflows with no small values: {1 + 2^-23, 1, ...}, d =
    // 2^-23 / 15 (fp16 0), codes 15 and 0, decoded all 1, requantized codes 0
    std::vector<float> ones(ggml::QK, 1.0f);
    ones[0] = 1.0f + 0x1p-23f;
    r = ggml::check_on(legacy[2].second, idempotent_requantize(0), ones);
    std::cout << "q4_1 {1 + 2^-23, 1, ...}, expected to fail: " << r;
    failures += r.passed;

    // The layouts' Interleave (lossless) keeps it, on 2-D input: rows x
    // blocks of each record
    for (const char *f : {"q8_0.4x4", "q8_0.4x8", "q4_0.i4.4x4", "q4_0.i4.4x8", "q4_0.i4.8x8"}) {
        ggml::Format fm = ggml::format(f);
        std::vector<Approximation> stages = ggml::pre_layout(fm.scheme);
        stages.push_back(fm.scheme.target<Compose>()->stages.back().target<Compose>()->stages[0]);
        for (const auto &[dn, dist] : {domains[0], domains[8]}) {
            r = check_property(Compose{stages}, idempotent_requantize(0), dist, Float(32), {ggml::QK * 32, fm.rows * 8}, 2, 1);
            std::cout << f << " (interleaved, " << dn << "): " << r;
            failures += !r.passed;
        }
    }

    // Type-1's decoded values, q d + m, rounded to f32: with a large offset
    // and a small spread, d below a few f32 ulps of the block, they need not
    // be q d + m, and requantizing them can change the codes, and d.
    // (A reviewer's block: codes 0, 4, 15 decode to 4096, 4096 + 2^-11,
    // 4096 + 2^-10, requantized to codes 0, 7, 15.)
    std::vector<float> offset(ggml::QK, 4095.9990234375f);
    offset[1] = 4095.999267578125f;
    offset[2] = 4096.0f;
    r = ggml::check_on(legacy[2].second, idempotent_requantize(0), offset);
    std::cout << "q4_1 {4096 - 2^-10, 4096 - 3 2^-12, 4096, 4096 - 2^-10, ...}, expected to fail: " << r;
    failures += r.passed;
    // Over blocks of large offsets and small spreads (ggml::offset_blocks,
    // random_offset_blocks): it holds in every block whose fp16 d is finite
    // and not 0 and that decodes exactly (every q d + m a float), or whose d
    // is at least 2^9 f32 ulps of its largest decoded value (the rounding
    // then moves d by under half an fp16 ulp, and the codes by under 1/2);
    // fails in some of the others, with d up to 2^7 ulps (q4_1: d moves by
    // up to ulp/30, half an fp16 ulp being d 2^-12 to 2^-11).
    std::vector<float> offsets = ggml::offset_blocks(ggml::QK), random_offsets = ggml::random_offset_blocks(ggml::QK, 20000);
    offsets.insert(offsets.end(), random_offsets.begin(), random_offsets.end());
    for (const auto &[name, a, top] : {std::tuple{"q4_1", legacy[2].second, 15}, {"q5_1", legacy[4].second, 31}}) {
        using T = ggml::AffineTally;
        T t = ggml::affine_requantize_tally(a, top, ggml::QK, offsets);
        std::cout << name << ": requantizing large offsets, small spreads: " << t.str() << "\n";
        bool ok = t.error.empty() && !t.changed[T::exact] && !t.changed[T::inexact_far] && t.blocks[T::inexact_far] && t.changed[T::inexact] &&
                  t.changed[T::zero] == t.blocks[T::zero] && t.changed[T::non_finite] == t.blocks[T::non_finite];
        if (!ok) std::cout << t.error << name << ": FAILED: not idempotent exactly where expected\n";
        failures += !ok;
    }
    // Type-0 (no offset: q d, exact) is unaffected
    for (const auto &[name, a] : legacy) {
        if (name == "q4_1" || name == "q5_1") continue;
        std::string example;
        int64_t changed = ggml::requantize_changed_count(a, offsets, &example);
        std::cout << name << ": requantizing large offsets, small spreads: " << changed << "/" << offsets.size() / ggml::QK << " blocks changed " << example << "\n";
        failures += changed != 0;
    }

    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
