// Properties of tq1_0, tq2_0 and q2_0 (schemes/ternary.h) and q1_0
// (schemes/binary.h), without their layouts:
// the round trip is within the declared error bound, the codes are within
// their declared range, and every stage is closed. The blocks are drawn to
// reach the quantizer's corners: binades of every size, one-signed blocks,
// blocks dominated by one value, constants and zeros.
//
// And where requantizing is idempotent (enc dec enc = enc), block by block.
// tq1_0, tq2_0, q2_0: in exactly the blocks whose fp16 scale (the block's max
// |x|) is finite and not 0, normal or subnormal: the max's code is +-1, so
// the decoded max is the stored scale D, the scale again, and the codes
// round(c D / D) = c. It fails where the scale underflows to 0 (codes from
// the unrounded scale, decoded 0, requantized 0) or overflows to inf (inf *
// 0 = NaN; q2_0's code 2, of blocks of max |x| < 2^-128, among the former).
// q1_0: in every block whose scale (mean |x|) is not 0, inf
// included (the decoded block is +-D, its mean |x| D: 128 multiples of an
// fp16 are exact in f32), and in no block whose scale underflows to 0 with
// a negative value: its code -1 decodes to -0, and requantizes to 1 (x >= 0).
// Both hold over blocks of a large offset and a small spread
// (ggml::offset_blocks, where type-1 fails): no offset to round.
#include "Halide.h"
#include "halide_approximation_testing.h"
#include "schemes/binary.h"
#include "schemes/idempotence.h"
#include "schemes/schemes.h"
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

    // each format's stages before its layout, and its quantizer, held: its
    // bound checked at it
    std::vector<std::tuple<std::string, Approximation, Approximation>> schemes;
    for (const auto &[name, format] : {std::pair{"q1_0", ggml::q1_0("aos")}, {"tq2_0", ggml::tq2_0("aos")}, {"tq1_0", ggml::tq1_0("aos")}, {"q2_0", ggml::q2_0("aos")}}) {
        std::vector<Approximation> stages = ggml::pre_layout(format);
        schemes.emplace_back(name, stages[1], Compose{stages});
    }
    int failures = 0;
    for (const auto &[scheme, q, a] : schemes) {
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

    // q2_0's blocks of max |x| < 2^-128 (f32 subnormals), whose 1 / max is
    // inf: the code 2 (x > 0; -1 on x86-64) and its wider bound
    {
        const auto &[scheme, q, a] = schemes.back();
        for (const Property &p : {within_declared_bound().at(q), outputs_within_declared_ranges(), closed()}) {
            PropertyResult r = check_property(a, p, Distribution::normal(0, 1e-40), Float(32), {n}, trials, 1);
            std::cout << scheme << " (f32 subnormal): " << r;
            failures += !r.passed;
        }
    }

    // enc dec enc = enc per block, by its fp16 scale
    std::vector<std::pair<std::string, Distribution>> domains = dists;
    domains.emplace_back("subnormal scales", Distribution::normal(0, 1e-5));
    domains.emplace_back("overflowing scales", Distribution::normal(0, 1e5));
    using SC = ggml::ScaleClass;
    for (const auto &[scheme, q, a] : schemes) {
        ggml::TallyOver t = ggml::requantize_tally_over(a, "scale", domains, n, 1);
        const ggml::RequantizeTally &k = t.tally;
        std::cout << scheme << ": requantizing: " << k.str() << "\n";
        bool covered = k.blocks_in(SC::Subnormal) && k.blocks_in(SC::Underflow) && k.blocks_in(SC::NonFinite);
        bool ok = scheme == "q1_0" ? !k.changed_in(SC::Zero) && !k.changed_in(SC::Subnormal) && !k.changed_in(SC::Normal) &&
                                         !k.changed_in(SC::NonFinite) && k.changed_in(SC::Underflow) :
                                     k.holds_where_scale_finite_nonzero();
        if (!t.error.empty() || !covered || !ok) std::cout << t.error << scheme << ": FAILED: requantizing not idempotent where expected\n";
        failures += !t.error.empty() || !covered || !ok;
    }
    // q1_0, its scale underflowing: {2^-30, 0, ...} holds (codes 1), {-2^-30,
    // 0, ...} not (code -1 decodes to -0)
    Approximation q1 = std::get<2>(schemes[0]);
    for (float v : {0x1p-30f, -0x1p-30f}) {
        PropertyResult r = ggml::check_on(q1, idempotent_requantize(0), ggml::blocks_of(ggml::SignQuant::block, {{v}}));
        std::cout << "q1_0 {" << v << ", 0, ...}" << (v < 0 ? ", expected to fail: " : ": ") << r;
        failures += r.passed == (v < 0);
    }
    // the layout's Interleave (lossless) keeps it, on rows x blocks
    {
        ggml::Format fm = ggml::format("q1_0.4x1");
        std::vector<Approximation> stages = ggml::pre_layout(fm.scheme);
        stages.push_back(fm.scheme.target<Compose>()->stages.back().target<Compose>()->stages[0]);
        PropertyResult r = check_property(Compose{stages}, idempotent_requantize(0), Distribution::normal(0, 1), Float(32), {fm.block * 8, fm.rows * 8}, 2, 1);
        std::cout << "q1_0.4x1 (interleaved): " << r;
        failures += !r.passed;
    }

    // Large offsets, small spreads (ggml::offset_blocks: type-1's failures):
    // no offset to round, every block holds
    for (const auto &[scheme, q, a] : schemes) {
        std::string example;
        std::vector<float> x = ggml::offset_blocks(ggml::format(scheme).block, scheme == "q1_0" ? 1 : 4);
        int64_t changed = ggml::requantize_changed_count(a, x, &example);
        std::cout << scheme << ": requantizing large offsets, small spreads: " << changed << " blocks changed " << example << "\n";
        failures += changed != 0;
    }

    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
