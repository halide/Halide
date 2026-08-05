#include "arm_cpu_detect.h"
#include "Halide.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

// Test the shared ARM host-CPU detection logic (used by both Target.cpp to
// compute the host target and by the runtime to answer
// halide_can_use_target_features). Because it is a pure function of what the
// platform reports, we can check the Linux, macOS and Windows paths for both
// ARM ABIs from whatever machine happens to be running the test.

namespace {

using namespace Halide::Internal::CpuDetect;

struct FakeOps {
    std::vector<halide_target_feature_t> features;

    void set_feature(halide_target_feature_t f) {
        features.push_back(f);
    }
};

struct FakeAuxvOps : FakeOps {
    using ArmDetectionSource = AuxvSource;

    uint64_t hwcap = 0;
    uint64_t hwcap2 = 0;

    uint64_t get_hwcap() {
        return hwcap;
    }

    uint64_t get_hwcap2() {
        return hwcap2;
    }
};

struct FakeSysctlOps : FakeOps {
    using ArmDetectionSource = SysctlSource;

    // This maps sysctl names to values; a name that isn't present fails the
    // lookup, like a sysctl that doesn't exist.
    std::vector<std::pair<std::string, int>> sysctls;
    std::vector<std::string> sysctls_queried;

    bool sysctl_get_int(const char *name, int *value) {
        sysctls_queried.push_back(name);
        for (const auto &s : sysctls) {
            if (s.first == name) {
                *value = s.second;
                return true;
            }
        }
        return false;
    }
};

struct FakeWindowsOps : FakeOps {
    using ArmDetectionSource = WindowsSource;

    // The feature codes IsProcessorFeaturePresent should answer yes to.
    std::vector<int> windows_features;

    bool processor_feature_present(int feature) {
        return std::find(windows_features.begin(), windows_features.end(), feature) !=
               windows_features.end();
    }
};

// Linux hwcap bits, spelled out here rather than reused from the header so that
// the test pins the actual ABI values.
constexpr uint64_t arm64_hwcap_asimdhp = 1ull << 10;
constexpr uint64_t arm64_hwcap_asimddp = 1ull << 20;
constexpr uint64_t arm64_hwcap_sve = 1ull << 22;
constexpr uint64_t arm64_hwcap2_sve2 = 1ull << 1;
constexpr uint64_t arm32_hwcap_asimdhp = 1ull << 23;
constexpr uint64_t arm32_hwcap_asimddp = 1ull << 24;

// Windows feature codes.
constexpr int pf_floating_point_emulated = 1;
constexpr int pf_arm_fmac = 27;
constexpr int pf_arm_v82_dp = 43;
constexpr int pf_arm_sve = 46;
constexpr int pf_arm_sve2 = 47;

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

template<typename Ops>
bool check(const char *name, const Ops &ops, const ArmDetection &detection,
           std::initializer_list<halide_target_feature_t> expected_features,
           bool expected_scalable_vector) {
    bool ok = true;
    const std::vector<int> got = sorted_features(ops.features);
    const std::vector<int> want = sorted_features(expected_features);
    if (got != want) {
        printf("%s: detected features\n  %s\nbut expected\n  %s\n",
               name, feature_names(got).c_str(), feature_names(want).c_str());
        ok = false;
    }
    if (detection.have_scalable_vector != expected_scalable_vector) {
        printf("%s: reported scalable vectors as %d but expected %d\n",
               name, (int)detection.have_scalable_vector, (int)expected_scalable_vector);
        ok = false;
    }
    return ok;
}

}  // namespace

int main(int argc, char **argv) {
    bool ok = true;

    // Linux, AArch64: dot product and fp16 come from AT_HWCAP, SVE2 from
    // AT_HWCAP2, and SVE2 means we should go measure the vector width.
    {
        FakeAuxvOps ops;
        ops.hwcap = arm64_hwcap_asimddp | arm64_hwcap_asimdhp;
        ops.hwcap2 = arm64_hwcap2_sve2;
        const ArmDetection detection = detect_arm_features(ops, ArmArch::Arm64);
        ok &= check("Linux aarch64 with dotprod, fp16, sve2", ops, detection,
                    {halide_target_feature_arm_dot_prod,
                     halide_target_feature_arm_fp16,
                     halide_target_feature_sve2},
                    true);
    }

    // Plain SVE detection is deliberately disabled; its probe is short-circuited.
    // See sve_detection_enabled and https://github.com/halide/Halide/issues/8872.
    // A machine with SVE but not SVE2 should report no scalable vector at all.
    {
        FakeAuxvOps ops;
        ops.hwcap = arm64_hwcap_sve;
        const ArmDetection detection = detect_arm_features(ops, ArmArch::Arm64);
        ok &= check("Linux aarch64 with SVE but not SVE2", ops, detection, {}, false);
    }

    // ...and a machine with both reports only SVE2.
    {
        FakeAuxvOps ops;
        ops.hwcap = arm64_hwcap_sve;
        ops.hwcap2 = arm64_hwcap2_sve2;
        const ArmDetection detection = detect_arm_features(ops, ArmArch::Arm64);
        ok &= check("Linux aarch64 with SVE and SVE2", ops, detection,
                    {halide_target_feature_sve2}, true);
    }

    // This is the assertion that the two halves agree about SVE: if someone
    // flips sve_detection_enabled, they should expect to update the cases
    // above, and the compiler and runtime will change together.
    if (sve_detection_enabled) {
        printf("SVE detection has been enabled; update this test and check that "
               "Target.cpp and the runtime agree.\n");
        ok = false;
    }

    // Linux, 32-bit ARM: the same features live at different hwcap bits, and
    // there is no SVE of any kind.
    {
        FakeAuxvOps ops;
        ops.hwcap = arm32_hwcap_asimddp | arm32_hwcap_asimdhp;
        const ArmDetection detection = detect_arm_features(ops, ArmArch::Arm32);
        ok &= check("Linux arm32 with dotprod and fp16", ops, detection,
                    {halide_target_feature_arm_dot_prod,
                     halide_target_feature_arm_fp16},
                    false);
    }

    // The 64-bit bit positions must not be read on a 32-bit machine, or we'd
    // claim features the CPU doesn't have.
    {
        FakeAuxvOps ops;
        ops.hwcap = arm64_hwcap_asimddp | arm64_hwcap_asimdhp;
        ops.hwcap2 = arm64_hwcap2_sve2;
        const ArmDetection detection = detect_arm_features(ops, ArmArch::Arm32);
        ok &= check("Linux arm32 doesn't read aarch64 hwcap bits", ops, detection, {}, false);
    }

    // macOS, Apple silicon.
    {
        FakeSysctlOps ops;
        ops.sysctls = {{"hw.optional.arm.FEAT_DotProd", 1},
                       {"hw.optional.arm.FEAT_FP16", 1}};
        const ArmDetection detection = detect_arm_features(ops, ArmArch::Arm64);
        ok &= check("macOS arm64", ops, detection,
                    {halide_target_feature_arm_dot_prod,
                     halide_target_feature_arm_fp16},
                    false);
        for (const std::string &name : ops.sysctls_queried) {
            if (name == "hw.cputype" || name == "hw.cpusubtype") {
                printf("macOS arm64: queried %s, but armv7s is 32-bit only\n", name.c_str());
                ok = false;
            }
        }
    }

    // A sysctl that exists but is zero means the feature is absent, and one
    // that doesn't exist at all must not be treated as present.
    {
        FakeSysctlOps ops;
        ops.sysctls = {{"hw.optional.arm.FEAT_DotProd", 0}};
        const ArmDetection detection = detect_arm_features(ops, ArmArch::Arm64);
        ok &= check("macOS arm64 without dotprod or fp16", ops, detection, {}, false);
    }

    // macOS, 32-bit: a Swift core is identified by cputype/cpusubtype.
    {
        FakeSysctlOps ops;
        ops.sysctls = {{"hw.cputype", 12}, {"hw.cpusubtype", 11}};
        const ArmDetection detection = detect_arm_features(ops, ArmArch::Arm32);
        ok &= check("macOS arm32 Swift", ops, detection,
                    {halide_target_feature_armv7s}, false);
    }

    {
        FakeSysctlOps ops;
        ops.sysctls = {{"hw.cputype", 12}, {"hw.cpusubtype", 9}};
        const ArmDetection detection = detect_arm_features(ops, ArmArch::Arm32);
        ok &= check("macOS arm32 non-Swift", ops, detection, {}, false);
    }

    // Windows on ARM.
    {
        FakeWindowsOps ops;
        ops.windows_features = {pf_arm_fmac, pf_arm_v82_dp, pf_arm_sve2};
        const ArmDetection detection = detect_arm_features(ops, ArmArch::Arm64);
        ok &= check("Windows arm64", ops, detection,
                    {halide_target_feature_arm_fp16,
                     halide_target_feature_arm_dot_prod,
                     halide_target_feature_sve2},
                    true);
    }

    // Emulated floating point means the fp16 instructions aren't real, even
    // though the FMAC feature is reported.
    {
        FakeWindowsOps ops;
        ops.windows_features = {pf_floating_point_emulated, pf_arm_fmac};
        const ArmDetection detection = detect_arm_features(ops, ArmArch::Arm64);
        ok &= check("Windows arm64 with emulated floating point", ops, detection, {}, false);
    }

    // SVE is masked off here too.
    {
        FakeWindowsOps ops;
        ops.windows_features = {pf_arm_sve};
        const ArmDetection detection = detect_arm_features(ops, ArmArch::Arm64);
        ok &= check("Windows arm64 with SVE but not SVE2", ops, detection, {}, false);
    }

    if (!ok) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
