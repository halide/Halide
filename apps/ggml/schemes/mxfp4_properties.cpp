// Properties of mxfp4 and nvfp4 (schemes/mx.h), without their layouts: the
// round trip is within the declared error bound, the codes are within their
// declared range, requantizing is idempotent (enc dec enc = enc), and every
// stage is closed. The blocks are drawn to reach the scales' corners:
// binades of every size (nvfp4: per sub-block too), one-signed blocks, blocks
// dominated by one value, constants and zeros.
//
// Where requantizing is idempotent, by a sweep of blocks of one value, the
// block's (sub-block's) max |x| (the rest no matter: the scale is a function
// of it, and at the same scale every decoded value's code is its own):
// - mxfp4: in exactly the blocks of floorf(log2f(max |x|)) >= -125 (or max
//   0): max |x| >= 2^-125, and the floats just below whose log2f rounds up
//   to -125. The max's code is 8 or 12 (of d = 2^(floor(log2 max) - 3)),
//   its decoded value in the same binade, so the same e. Below, e wraps (to
//   255, ...), d is far above the block, its codes 0: decoded 0,
//   requantized e = 0.
// - nvfp4: in every sub-block but those of max |x| in [18/1024, 20/1024],
//   of 30/1024, or past 2^34 (but in (2^35, 2^36]). With a normal (or 0, or
//   saturated below 2^34) ue4m3 scale, the max's code is 12 (the scale
//   within 1/16 of max / 12), so the decoded max / 6 is the scale again.
//   Below the normals, GGML's subnormal
//   scales m / 1024, m = (int)(max / 6 * 512 + 0.5) <= 7: at m = 2, max in
//   [18/1024, 20/1024] gets code 8 (at 20/1024, a tie, to the lesser),
//   decoded 16/1024, whose m is 1 (requantizing then changes the decoded
//   values too: 12/1024); at m = 3, 30/1024 is a tie of 8 and 12, to 8,
//   decoded 24/1024, whose m is 2. Saturated (d = 224), the codes' errors
//   |kv d - x|, rounded to float, of x past about 2^34 can round alike: of
//   codes 8 and 12 in most of (2^34, 2^35], of all of them past 2^36; a
//   tie goes to the lesser code (decoded 1792 or 0: another scale). The
//   sampled distributions reach only the last (the "huge" ones: expected
//   to fail).
// Both hold over blocks of a large offset and a small spread
// (ggml::offset_blocks, where type-1 fails): no offset to round.
#include "Halide.h"
#include "halide_approximation_testing.h"
#include "schemes/idempotence.h"
#include "schemes/mx.h"
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
        {"outliers", Distribution::outliers(Distribution::normal(0, 1e-3), ggml::QK * blocks, 1e4)},
        {"constant", Distribution::constant(100)},
        {"zeros", Distribution::zeros()},
    };
    std::vector<Distribution> parts;
    for (const auto &[name, dist] : dists) {
        parts.push_back(dist);
    }

    int failures = 0;
    // the formats' stages before their layouts; their quantizers held: their
    // bounds checked at them
    std::vector<Approximation> mx = ggml::pre_layout(ggml::mxfp4("aos")), nvs = ggml::pre_layout(ggml::nvfp4("aos"));
    Approximation q = mx[1], a = Compose{mx};
    for (const auto &[name, dist] : dists) {
        for (const Property &p : {within_declared_bound().at(q), outputs_within_declared_ranges(), idempotent_requantize(0), closed()}) {
            PropertyResult r = check_property(a, p, dist, Float(32), {ggml::QK * blocks}, trials, 1);
            std::cout << "mxfp4 (" << name << "): " << r;
            failures += !r.passed;
        }
    }

    const int n = ggml::NVFP4Quant::n, block = ggml::NVFP4Quant::block;
    Approximation nq = nvs[1], nv = Compose{nvs};
    std::vector<std::pair<std::string, Distribution>> nvd = dists;
    nvd.emplace_back("mixed per 16", Distribution::mixture(parts, n));
    nvd.emplace_back("binades per 16", Distribution::mixture({Distribution::normal(0, 1e-3), Distribution::normal(0, 1), Distribution::normal(0, 3e2), Distribution::normal(0, 3e-3)}, n));
    for (const auto &[name, dist] : nvd) {
        for (const Property &p : {within_declared_bound().at(nq), outputs_within_declared_ranges(), closed()}) {
            PropertyResult r = check_property(nv, p, dist, Float(32), {block * blocks}, trials, 1);
            std::cout << "nvfp4 (" << name << "): " << r;
            failures += !r.passed;
        }
        // (past 2^34: see above)
        bool huge = name == "huge" || name == "mixed per 16";
        PropertyResult r = check_property(nv, idempotent_requantize(0), dist, Float(32), {block * blocks}, trials, 1);
        std::cout << "nvfp4 (" << name << ")" << (huge ? ", expected to fail: " : ": ") << r;
        failures += r.passed == huge;
    }

    // the sweeps: blocks of one value v, requantizing changes the blocks of
    // `fails(v)`, and only those
    auto sweep = [&](const std::string &name, const Approximation &a, int block, const std::vector<float> &vs, const std::function<bool(float)> &fails) {
        std::vector<std::vector<float>> blocks;
        for (float v : vs) {
            blocks.push_back({v});
        }
        auto changed = std::make_shared<std::vector<bool>>();
        PropertyResult r = ggml::check_on(a, ggml::requantize_changed_blocks(changed), ggml::blocks_of(block, blocks));
        int wrong = 0, n = 0;
        for (size_t k = 0; k < vs.size(); k++) {
            n += (*changed)[k];
            if ((*changed)[k] != fails(vs[k]) && wrong++ < 4) printf("  %s {%a, 0, ...}: %s\n", name.c_str(), vs[k], (*changed)[k] ? "changed" : "unchanged");
        }
        printf("%s: requantizing changes %d of %d blocks of one value (%d not as expected)\n", name.c_str(), n, (int)vs.size(), wrong);
        failures += !r.passed || wrong || !n;
    };
    std::vector<float> vs = {0.0f, 0x1p-125f, 0x1p-149f, -0x1p-126f};
    for (int k = 0; k < 64 * 12; k++) {
        vs.push_back(std::ldexp(1.0f + (k % 64) / 64.0f, -132 + k / 64));
    }
    float below = 0x1p-125f;
    for (int k = 0; k < 64; k++) {
        vs.push_back(below = std::nextafter(below, 0.0f));  // where log2f rounds up to -125
    }
    sweep("mxfp4", a, ggml::QK, vs, [](float v) {
        // (mx.h's correctly rounded log2f)
        return v != 0 && std::floor((float)(std::log((double)std::fabs(v)) * (1 / std::log(2.0)))) < -125;
    });
    vs = {std::nextafter(18 / 1024.0f, 0.0f), std::nextafter(20 / 1024.0f, 1.0f), -19 / 1024.0f, std::nextafter(30 / 1024.0f, 0.0f), std::nextafter(30 / 1024.0f, 1.0f), 448 * 6.0f, 1e30f, INFINITY};
    for (int k = 0; k < 2048; k++) {
        vs.push_back(k * 0x1p-14f);  // [0, 1/8): the subnormal scales' (max < 6 / 64) and on
    }
    for (int k = 0; k < 16 * 117; k++) {
        vs.push_back(std::ldexp(1.0f + (k % 16) / 16.0f, 11 + k / 16));  // 2^11 .. 2^128
    }
    for (float e : {0x1p34f, 0x1p35f, 0x1p36f}) {
        vs.insert(vs.end(), {std::nextafter(e, 0.0f), e, std::nextafter(e, INFINITY)});
    }
    // the code of x at the saturated scale (d = 224), by best_index_mxfp4's
    // fused errors
    const std::vector<int> kvalues = ggml::MXFP4Quant::kvalues();
    auto saturated_code = [&](float x) {
        int best = 0;
        float err = INFINITY;
        for (int kv : kvalues) {
            float e = std::fabs(std::fma((float)kv, 224.0f, -x));
            if (e < err) best = kv, err = e;
        }
        return best;
    };
    sweep("nvfp4", nv, block, vs, [&](float v) {
        float a = std::fabs(v);
        return (a >= 18 / 1024.0f && a <= 20 / 1024.0f) || a == 30 / 1024.0f || (a > 448 * 6.0f && saturated_code(a) != 12);
    });

    // the layout's Interleave (lossless) keeps it, on rows x blocks
    {
        ggml::Format fm = ggml::format("mxfp4.4x4");
        std::vector<Approximation> stages = ggml::pre_layout(fm.scheme);
        stages.push_back(fm.scheme.target<Compose>()->stages.back().target<Compose>()->stages[0]);
        PropertyResult r = check_property(Compose{stages}, idempotent_requantize(0), Distribution::normal(0, 1), Float(32), {fm.block * 32, fm.rows * 8}, 2, 1);
        std::cout << "mxfp4.4x4 (interleaved): " << r;
        failures += !r.passed;
    }

    // Large offsets, small spreads (ggml::offset_blocks: type-1's failures):
    // no offset to round, every block holds
    for (const auto &[name, f] : {std::pair{"mxfp4", a}, {"nvfp4", nv}}) {
        std::string example;
        int64_t changed = ggml::requantize_changed_count(f, ggml::offset_blocks(ggml::format(name).block), &example);
        std::cout << name << ": requantizing large offsets, small spreads: " << changed << " blocks changed " << example << "\n";
        failures += changed != 0;
    }

    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
