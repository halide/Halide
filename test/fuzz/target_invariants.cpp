#include "fuzz_helpers.h"

#include <Halide.h>

#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Halide;

namespace {

// The concrete values we sample each Target property from.
constexpr Target::Arch archs[] = {
    Target::ArchUnknown,
    Target::X86,
    Target::ARM,
    Target::POWERPC,
    Target::Hexagon,
    Target::WebAssembly,
    Target::RISCV,
};

constexpr Target::OS oses[] = {
    Target::OSUnknown,
    Target::Linux,
    Target::Windows,
    Target::OSX,
    Target::Android,
    Target::IOS,
    Target::QuRT,
    Target::NoOS,
    Target::Fuchsia,
    Target::WebAssemblyRuntime,
};

// Weighted toward the concrete widths (32/64): a 0 makes the whole target
// "unknown", which is excluded from the round-trip guarantee, so we only want a
// minority of bases to land there.
constexpr int bits_settings[] = {0, 32, 32, 64, 64};

constexpr Target::Processor processors[] = {
    Target::ProcessorGeneric,
    Target::K8,
    Target::K8_SSE3,
    Target::AMDFam10,
    Target::BtVer1,
    Target::BdVer1,
    Target::BdVer2,
    Target::BdVer3,
    Target::BdVer4,
    Target::BtVer2,
    Target::ZnVer1,
    Target::ZnVer2,
    Target::ZnVer3,
    Target::ZnVer4,
    Target::ZnVer5,
};

constexpr int vector_bits_settings[] = {0, 64, 128, 256, 512, 1024};

// Raised when one of the invariants below is violated (i.e. we found a real
// bug). This is deliberately distinct from Halide::Error: a Halide::Error
// thrown *by a setter* is the API correctly refusing to build an invalid
// target, which is allowed, whereas an InvariantViolation is a failure of
// the properties we are testing.
struct InvariantViolation : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void require(bool cond, const std::string &msg) {
    if (!cond) {
        throw InvariantViolation{msg};
    }
}

// Any complete target reachable through the public API must round-trip through
// to_string() unchanged, and reconstructing it must never throw.
void check_roundtrip(const Target &t) {
    // Targets with unknown arch/os/bits intentionally produce strings that
    // cannot be parsed back, so they are excluded by the round-trip guarantee.
    if (t.has_unknowns()) {
        return;
    }

    const std::string s = t.to_string();

    Target parsed;
    try {
        parsed = Target(s);
    } catch (const Error &e) {
        throw InvariantViolation{
            "Target(t.to_string()) threw, i.e. to_string() produced a string "
            "that cannot be parsed back:\n"
            "  to_string() = " +
            s + "\n  error = " + e.what()};
    }

    require(parsed == t,
            "Round-trip through to_string() changed the target:\n"
            "  original = " +
                s + "\n  reparsed = " + parsed.to_string());

    // Parsing a canonical string is stable (re-parsing yields the same target).
    require(Target(parsed.to_string()) == parsed,
            "Re-parsing a to_string() result was not stable:\n  " + s);
}

// Attempt a mutation via a public setter. The public setters are required to
// be transactional: if a mutation would produce an invalid target the setter
// rejects it by throwing, and in that case must leave the target completely
// unchanged. Returns true if the mutation was applied.
template<typename Fn>
bool try_mutate(Target &t, Fn fn) {
    const Target before = t;
    try {
        fn(t);
    } catch (const Error &) {
        // The API refused to perform this mutation. That's fine, but the
        // target must be exactly as it was.
        require(t == before,
                "A rejected setter left the target in a mutated state "
                "(not transactional):\n  before = " +
                    before.to_string() + "\n  after  = " + t.to_string());
        return false;
    }
    return true;
}

Target::Feature random_feature(FuzzingContext &fuzz) {
    return static_cast<Target::Feature>(fuzz.ConsumeIntegralInRange<int>(0, static_cast<int>(Target::FeatureEnd) - 1));
}

void test_one_target(FuzzingContext &fuzz) {
    // Pick a random starting base target.
    Target t;
    try_mutate(t, [&](Target &t_) { t_.set_arch(fuzz.PickValueInArray(archs)); });
    try_mutate(t, [&](Target &t_) { t_.set_bits(fuzz.PickValueInArray(bits_settings)); });
    try_mutate(t, [&](Target &t_) { t_.set_os(fuzz.PickValueInArray(oses)); });
    try_mutate(t, [&](Target &t_) { t_.set_processor_tune(fuzz.PickValueInArray(processors)); });
    try_mutate(t, [&](Target &t_) { t_.set_vector_bits(fuzz.PickValueInArray(vector_bits_settings)); });

    // All bases must round-trip
    check_roundtrip(t);

    // Saturate the target with features. We add them one at a time (rather than
    // as a batch, which validates as a whole and would be rejected outright if
    // any single feature is incompatible): each incompatible add is rejected on
    // its own, so the compatible ones accumulate into a feature-rich,
    // arch-consistent target. This is what pushes the checked targets toward the
    // dense feature combinations where implied/sub-feature interactions live.
    const int adds = fuzz.ConsumeIntegralInRange(0, 80);
    for (int i = 0; i < adds; i++) {
        try_mutate(t, [&](Target &t_) { t_.set_feature(random_feature(fuzz), true); });
    }
    check_roundtrip(t);

    // Now apply a random sequence of individual setter calls, including removals
    // and structural changes. After *every* step, whatever state the API left
    // the target in must round-trip.
    const int steps = fuzz.ConsumeIntegralInRange(0, 50);
    for (int i = 0; i < steps; i++) {
        switch (fuzz.ConsumeIntegralInRange(0, 6)) {
        case 0:
            try_mutate(t, [&](Target &t_) { t_.set_feature(random_feature(fuzz), fuzz.ConsumeBool()); });
            break;
        case 1:
            try_mutate(t, [&](Target &t_) { t_.set_arch(fuzz.PickValueInArray(archs)); });
            break;
        case 2:
            try_mutate(t, [&](Target &t_) { t_.set_os(fuzz.PickValueInArray(oses)); });
            break;
        case 3:
            try_mutate(t, [&](Target &t_) { t_.set_bits(fuzz.PickValueInArray(bits_settings)); });
            break;
        case 4:
            try_mutate(t, [&](Target &t_) { t_.set_vector_bits(fuzz.PickValueInArray(vector_bits_settings)); });
            break;
        case 5:
            try_mutate(t, [&](Target &t_) { t_.set_processor_tune(fuzz.PickValueInArray(processors)); });
            break;
        case 6: {
            // Batch feature set: a vector of features set/cleared together.
            std::vector<Target::Feature> fs;
            const int n = fuzz.ConsumeIntegralInRange(0, 5);
            fs.reserve(n);
            for (int k = 0; k < n; k++) {
                fs.push_back(random_feature(fuzz));
            }
            const bool value = fuzz.ConsumeBool();
            try_mutate(t, [&](Target &t_) { t_.set_features(fs, value); });
            break;
        }
        default: {
            internal_error;
        }
        }

        check_roundtrip(t);
    }

    // Both the minimal public representation and the complete debugging
    // representation must name the same Target.
    if (!t.has_unknowns()) {
        std::string complete = t.to_complete_string();
        require(Target(complete) == t,
                "Round-trip through to_complete_string() changed the target:\n  " +
                    complete);
    }
}

}  // namespace

FUZZ_TEST(target_invariants, FuzzingContext &fuzz) {
    try {
        test_one_target(fuzz);
    } catch (const InvariantViolation &v) {
        std::cerr << "Target invariant violated:\n"
                  << v.what() << "\n";
        return 1;
    }
    return 0;
}
