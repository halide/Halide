#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "arm_cpu_detect.h"
#include "arm_i8mm_multitarget.h"
#include "armv86a_multitarget.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Checks that multitarget dispatch falls back gracefully on a CPU without the
// int8 matrix multiply extension: a library built for "arm_i8mm,<plain>" or
// "armv86a,<plain>" (Armv8.6-A implies arm_i8mm) must select the plain
// sub-target when the runtime's CPU features lack arm_i8mm.
//
// To simulate CPUs we don't have, this replaces the runtime's (weak)
// halide_get_cpu_features, keeping the runtime's own list of known features.
// Whether arm_i8mm is reported as available is controlled by the environment
// variable HL_TEST_HAVE_ARM_I8MM.

namespace {

constexpr int kWordCount = (halide_target_feature_end + 63) / 64;

// Mirrors the layout of Halide::Runtime::Internal::CpuFeatures.
struct CpuFeatures {
    uint64_t known[kWordCount];
    uint64_t available[kWordCount];
};

void set_bit(uint64_t *mask, int feature) {
    mask[feature / 64] |= (uint64_t)1 << (feature % 64);
}

bool have_i8mm() {
    const char *value = getenv("HL_TEST_HAVE_ARM_I8MM");
    return value && strcmp(value, "1") == 0;
}

int get_cpu_features_calls = 0;

int can_use(std::initializer_list<halide_target_feature_t> features) {
    uint64_t mask[kWordCount] = {0};
    for (halide_target_feature_t f : features) {
        set_bit(mask, f);
    }
    return halide_can_use_target_features(kWordCount, mask);
}

}  // namespace

extern "C" int halide_get_cpu_features(CpuFeatures *features) {
    get_cpu_features_calls++;
    memset(features, 0, sizeof(*features));
    Halide::Internal::CpuDetect::for_each_detectable_arm_feature(
        [&](halide_target_feature_t f) { set_bit(features->known, f); });
    if (have_i8mm()) {
        set_bit(features->available, halide_target_feature_arm_i8mm);
    }
    return halide_error_code_success;
}

int main(int argc, char **argv) {
    const bool i8mm = have_i8mm();
    printf("Simulating a CPU %s arm_i8mm\n", i8mm ? "with" : "without");

    bool ok = true;
    auto expect = [&](const char *what, int got, int want) {
        if (got != want) {
            printf("%s: got %d, expected %d\n", what, got, want);
            ok = false;
        }
    };

    // The runtime checks arm_i8mm directly...
    expect("can use arm_i8mm", can_use({halide_target_feature_arm_i8mm}), i8mm ? 1 : 0);
    expect("can use plain target", can_use({}), 1);
    // ...but has no way to check the architecture version, so it accepts
    // armv86a on its own. That's why compile_multitarget also asks about
    // arm_i8mm for an armv86a target.
    expect("can use armv86a alone", can_use({halide_target_feature_armv86a}), 1);
    expect("can use armv86a with arm_i8mm",
           can_use({halide_target_feature_armv86a, halide_target_feature_arm_i8mm}), i8mm ? 1 : 0);

    if (get_cpu_features_calls == 0) {
        printf("The runtime didn't call the replacement halide_get_cpu_features\n");
        return 1;
    }

    Halide::Runtime::Buffer<int32_t, 1> output(16);

    output.fill(-1);
    expect("arm_i8mm_multitarget call", arm_i8mm_multitarget(output), 0);
    expect("arm_i8mm_multitarget selected sub-target", output(0), i8mm ? 8 : 0);

    output.fill(-1);
    expect("armv86a_multitarget call", armv86a_multitarget(output), 0);
    expect("armv86a_multitarget selected sub-target", output(0), i8mm ? 86 : 0);

    if (!ok) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
