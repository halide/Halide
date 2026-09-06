#include "powerpc_cpu_detect.h"
#include "Halide.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

// Test the shared PowerPC host-CPU detection logic (used by both Target.cpp to
// compute the host target and by the runtime to answer
// halide_can_use_target_features). Linux and FreeBSD read the auxiliary vector
// differently but report the same bits, so there is one mapping to check, and
// we can check it from any host.

namespace {

using namespace Halide::Internal::CpuDetect;

struct FakeOps {
    uint64_t hwcap;
    uint64_t hwcap2;
    std::vector<halide_target_feature_t> features;

    void set_feature(halide_target_feature_t f) {
        features.push_back(f);
    }

    uint64_t get_hwcap() {
        return hwcap;
    }

    uint64_t get_hwcap2() {
        return hwcap2;
    }
};

// Auxiliary vector bits, spelled out here rather than reused from the header so
// that the test pins the actual ABI values.
constexpr uint64_t hwcap_altivec = 0x10000000;
constexpr uint64_t hwcap_vsx = 0x00000080;
constexpr uint64_t hwcap2_arch_2_07 = 0x80000000;

std::vector<int> sorted_features(const std::vector<halide_target_feature_t> &features) {
    std::vector<int> result;
    for (halide_target_feature_t f : features) {
        result.push_back((int)f);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::string feature_names(const std::vector<int> &features) {
    std::string result;
    for (int f : features) {
        if (!result.empty()) {
            result += " ";
        }
        result += Halide::Target::feature_to_name((Halide::Target::Feature)f);
    }
    return result.empty() ? "(none)" : result;
}

bool check(const char *name, uint64_t hwcap, uint64_t hwcap2,
           std::initializer_list<halide_target_feature_t> expected_features,
           bool expected_altivec) {
    FakeOps ops{hwcap, hwcap2};
    const PowerPCDetection detection = detect_powerpc_features(ops);

    bool ok = true;
    const std::vector<int> got = sorted_features(ops.features);
    const std::vector<int> want = sorted_features(expected_features);
    if (got != want) {
        printf("%s: detected features\n  %s\nbut expected\n  %s\n",
               name, feature_names(got).c_str(), feature_names(want).c_str());
        ok = false;
    }
    if (detection.have_altivec != expected_altivec) {
        printf("%s: reported AltiVec as %d but expected %d\n",
               name, (int)detection.have_altivec, (int)expected_altivec);
        ok = false;
    }
    return ok;
}

}  // namespace

int main(int argc, char **argv) {
    bool ok = true;

    // POWER8 and later: AltiVec, VSX and ISA 2.07.
    ok &= check("POWER8", hwcap_altivec | hwcap_vsx, hwcap2_arch_2_07,
                {halide_target_feature_vsx, halide_target_feature_power_arch_2_07},
                true);

    // POWER7: AltiVec and VSX, but not ISA 2.07.
    ok &= check("POWER7", hwcap_altivec | hwcap_vsx, 0,
                {halide_target_feature_vsx}, true);

    // AltiVec alone. This is the minimum the PowerPC backend supports, so
    // Target.cpp accepts it and nothing else gets set.
    ok &= check("AltiVec only", hwcap_altivec, 0, {}, true);

    // No AltiVec at all: Target.cpp turns this into a user error, so the flag
    // has to be reported faithfully rather than folded into the feature set.
    ok &= check("No AltiVec", 0, 0, {}, false);

    // AltiVec lives in AT_HWCAP and ISA 2.07 in AT_HWCAP2; confusing the two
    // would misreport both.
    ok &= check("ISA 2.07 bit in the wrong word", hwcap2_arch_2_07, hwcap_altivec,
                {}, false);

    if (!ok) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
