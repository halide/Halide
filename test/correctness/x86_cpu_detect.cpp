#include "x86_cpu_detect.h"
#include "Halide.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

// Test the shared x86 host-CPU detection logic (used by both Target.cpp to
// compute the host target and by the runtime to answer
// halide_can_use_target_features). Its decisions are driven by the CPU state
// supplied by FakeOps, so we can check recorded/synthesized machines we don't
// have in front of us.

namespace {

using namespace Halide::Internal::CpuDetect;

struct Leaf {
    uint32_t leaf, subleaf;
    CpuidResult result;
};

struct FakeOps {
    std::vector<Leaf> leaves;
    uint64_t xcr0 = 0;
    int amx_permission_requests = 0;

    std::vector<halide_target_feature_t> features;

    CpuidResult cpuid(uint32_t leaf, uint32_t subleaf) {
        for (const Leaf &l : leaves) {
            if (l.leaf == leaf && l.subleaf == subleaf) {
                return l.result;
            }
        }
        return {0, 0, 0, 0};
    }

    uint64_t xgetbv(uint32_t) {
        return xcr0;
    }

    void request_amx_os_permission() {
        ++amx_permission_requests;
    }

    void set_feature(halide_target_feature_t f) {
        features.push_back(f);
    }
};

// Vendor strings, as returned in ebx/edx/ecx of leaf 0.
constexpr CpuidResult amd_vendor = {0x10, 0x68747541, 0x444d4163, 0x69746e65};
constexpr CpuidResult intel_vendor = {0x20, 0x756e6547, 0x6c65746e, 0x49656e69};

// Assemble the leaf-1 eax that encodes a given family and model, inverting the
// base/extended field split.
constexpr uint32_t family_model_eax(uint32_t family, uint32_t model) {
    // Everything we care about is family >= 0xF, which uses the extended
    // fields for both family and model.
    return ((family - 0xF) << 20) | ((model >> 4) << 16) | (0xF << 8) | ((model & 0xF) << 4);
}

constexpr uint32_t bit(int i) {
    return 1U << i;
}

// Leaf-1 ecx/edx bits for a machine with everything the pre-AVX512 checks look
// for: sse3, fma, sse41, osxsave, avx, f16c, rdrand, and sse2 in edx.
constexpr uint32_t leaf1_ecx_full = bit(0) | bit(12) | bit(19) | bit(27) | bit(28) | bit(29) | bit(30);
constexpr uint32_t leaf1_edx_sse2 = bit(26);

// XCR0 with XMM+YMM (avx), opmask+ZMM (avx512).
constexpr uint64_t xcr0_avx512 = 0x6 | 0xE0;

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

bool check(const char *name, FakeOps &ops,
           std::initializer_list<halide_target_feature_t> expected_features,
           halide_target_processor_t expected_processor, int expected_vector_bits,
           int expected_amx_permission_requests = -1) {
    const X86Detection detection = detect_x86_features(ops);

    const std::vector<int> got = sorted_features(ops.features);
    const std::vector<int> want = sorted_features(expected_features);
    bool ok = true;
    if (got != want) {
        printf("%s: detected features\n  %s\nbut expected\n  %s\n",
               name, feature_names(got).c_str(), feature_names(want).c_str());
        ok = false;
    }
    if (detection.processor != expected_processor) {
        printf("%s: detected processor %d but expected %d\n",
               name, (int)detection.processor, (int)expected_processor);
        ok = false;
    }
    if (detection.vector_bits != expected_vector_bits) {
        printf("%s: detected vector_bits %d but expected %d\n",
               name, detection.vector_bits, expected_vector_bits);
        ok = false;
    }
    if (expected_amx_permission_requests >= 0 &&
        ops.amx_permission_requests != expected_amx_permission_requests) {
        printf("%s: requested AMX permission %d times but expected %d\n",
               name, ops.amx_permission_requests, expected_amx_permission_requests);
        ok = false;
    }
    return ok;
}

}  // namespace

int main(int argc, char **argv) {
    if (sizeof(size_t) != 8) {
        // The AVX2-and-up paths are gated on a 64-bit host.
        printf("[SKIP] x86 CPU detection test assumes a 64-bit host.\n");
        return 0;
    }

    bool ok = true;

    // Zen 4, Raphael (family 19h, model 61h). This is the only Zen 4 model the
    // runtime's copy of this logic used to recognize.
    {
        FakeOps ops;
        ops.leaves = {
            {0, 0, amd_vendor},
            {1, 0, {family_model_eax(0x19, 0x61), 0, leaf1_ecx_full, leaf1_edx_sse2}},
        };
        ops.xcr0 = xcr0_avx512;
        ok &= check("Zen 4 (Raphael, 19h/61h)", ops,
                    {halide_target_feature_sse41,
                     halide_target_feature_avx,
                     halide_target_feature_f16c,
                     halide_target_feature_fma,
                     halide_target_feature_avx2,
                     halide_target_feature_avx512,
                     halide_target_feature_avx512_skylake,
                     halide_target_feature_avx512_cannonlake,
                     halide_target_feature_avx512_zen4},
                    halide_target_processor_znver4, 0);
    }

    // Zen 4, Genoa (family 19h, model A0h). Same feature set: the model ranges
    // come from the shared table, not from a single hard-coded model number.
    {
        FakeOps ops;
        ops.leaves = {
            {0, 0, amd_vendor},
            {1, 0, {family_model_eax(0x19, 0xA0), 0, leaf1_ecx_full, leaf1_edx_sse2}},
        };
        ops.xcr0 = xcr0_avx512;
        ok &= check("Zen 4 (Genoa, 19h/A0h)", ops,
                    {halide_target_feature_sse41,
                     halide_target_feature_avx,
                     halide_target_feature_f16c,
                     halide_target_feature_fma,
                     halide_target_feature_avx2,
                     halide_target_feature_avx512,
                     halide_target_feature_avx512_skylake,
                     halide_target_feature_avx512_cannonlake,
                     halide_target_feature_avx512_zen4},
                    halide_target_processor_znver4, 0);
    }

    // Zen 5 (family 1Ah).
    {
        FakeOps ops;
        ops.leaves = {
            {0, 0, amd_vendor},
            {1, 0, {family_model_eax(0x1A, 0x44), 0, leaf1_ecx_full, leaf1_edx_sse2}},
        };
        ops.xcr0 = xcr0_avx512;
        ok &= check("Zen 5 (1Ah/44h)", ops,
                    {halide_target_feature_sse41,
                     halide_target_feature_avx,
                     halide_target_feature_f16c,
                     halide_target_feature_fma,
                     halide_target_feature_avx2,
                     halide_target_feature_avxvnni,
                     halide_target_feature_avx512,
                     halide_target_feature_avx512_skylake,
                     halide_target_feature_avx512_cannonlake,
                     halide_target_feature_avx512_zen4,
                     halide_target_feature_avx512_zen5},
                    halide_target_processor_znver5, 0);
    }

    // An older AMD part gets a processor for tuning, but takes the generic
    // feature-bit path rather than a hard-coded feature set.
    {
        FakeOps ops;
        ops.leaves = {
            {0, 0, amd_vendor},
            {1, 0, {family_model_eax(0x17, 0x31), 0, bit(0) | bit(19), leaf1_edx_sse2}},
        };
        ok &= check("Zen 2 (17h/31h)", ops, {halide_target_feature_sse41},
                    halide_target_processor_znver2, 0, 0);
    }

    // Intel Skylake-X: detected purely from feature bits. avx512f + cd + dq +
    // bw + vl, but no ifma, so skylake and not cannonlake.
    {
        const uint32_t leaf7_ebx =
            bit(5) | bit(16) | bit(17) | bit(28) | bit(30) | bit(31);
        FakeOps ops;
        ops.leaves = {
            {0, 0, intel_vendor},
            {1, 0, {family_model_eax(0xF, 0x55), 0, leaf1_ecx_full, leaf1_edx_sse2}},
            {7, 0, {0, leaf7_ebx, 0, 0}},
        };
        ops.xcr0 = xcr0_avx512;
        ok &= check("Intel Skylake-X", ops,
                    {halide_target_feature_sse41,
                     halide_target_feature_avx,
                     halide_target_feature_f16c,
                     halide_target_feature_fma,
                     halide_target_feature_avx2,
                     halide_target_feature_avx512,
                     halide_target_feature_avx512_skylake},
                    halide_target_processor_generic, 0, 1);
    }

    // An AVX512 machine with no OS support for the ZMM state gets neither
    // AVX512 nor AVX2's OS-gated companions.
    {
        const uint32_t leaf7_ebx =
            bit(5) | bit(16) | bit(17) | bit(28) | bit(30) | bit(31);
        FakeOps ops;
        ops.leaves = {
            {0, 0, intel_vendor},
            {1, 0, {family_model_eax(0xF, 0x55), 0, leaf1_ecx_full, leaf1_edx_sse2}},
            {7, 0, {0, leaf7_ebx, 0, 0}},
        };
        ops.xcr0 = 0x6;  // XMM + YMM only
        ok &= check("Skylake-X without OS AVX512 support", ops,
                    {halide_target_feature_sse41,
                     halide_target_feature_avx,
                     halide_target_feature_f16c,
                     halide_target_feature_fma,
                     halide_target_feature_avx2},
                    halide_target_processor_generic, 0);
    }

    // AVX10.1 reports its maximum vector width separately from the feature.
    {
        FakeOps ops;
        ops.leaves = {
            {0, 0, intel_vendor},
            {1, 0, {family_model_eax(0xF, 0x55), 0, leaf1_ecx_full, leaf1_edx_sse2}},
            {7, 0, {0, bit(5), 0, 0}},
            {7, 1, {0, 0, 0, bit(19)}},
            {0x24, 0, {0, 0x01 | bit(16) | bit(17) | bit(18), 0, 0}},
        };
        ops.xcr0 = xcr0_avx512;
        ok &= check("AVX10.1 at 512 bits", ops,
                    {halide_target_feature_sse41,
                     halide_target_feature_avx,
                     halide_target_feature_f16c,
                     halide_target_feature_fma,
                     halide_target_feature_avx2,
                     halide_target_feature_avx10_1},
                    halide_target_processor_generic, 512);
    }

    // Sapphire Rapids requires AMX support in both the CPU and the OS.
    {
        const uint32_t leaf7_ebx =
            bit(5) | bit(16) | bit(17) | bit(21) | bit(28) | bit(30) | bit(31);
        const uint32_t leaf7_edx_amx = bit(22) | bit(24) | bit(25);
        const auto sapphire_leaves = [&](uint32_t edx) {
            return std::vector<Leaf>{
                {0, 0, intel_vendor},
                {1, 0, {family_model_eax(0xF, 0x8F), 0, leaf1_ecx_full, leaf1_edx_sse2}},
                {7, 0, {0, leaf7_ebx, 0, edx}},
                {7, 1, {bit(4), 0, 0, 0}},
            };
        };

        const std::initializer_list<halide_target_feature_t> without_amx = {
            halide_target_feature_sse41,
            halide_target_feature_avx,
            halide_target_feature_f16c,
            halide_target_feature_fma,
            halide_target_feature_avx2,
            halide_target_feature_avx512,
            halide_target_feature_avx512_skylake,
            halide_target_feature_avx512_cannonlake,
            halide_target_feature_avxvnni};

        {
            FakeOps ops;
            ops.leaves = sapphire_leaves(leaf7_edx_amx);
            ops.xcr0 = xcr0_avx512 | 0x60000;  // + XTILECFG/XTILEDATA
            ok &= check("Sapphire Rapids with OS AMX support", ops,
                        {halide_target_feature_sse41,
                         halide_target_feature_avx,
                         halide_target_feature_f16c,
                         halide_target_feature_fma,
                         halide_target_feature_avx2,
                         halide_target_feature_avx512,
                         halide_target_feature_avx512_skylake,
                         halide_target_feature_avx512_cannonlake,
                         halide_target_feature_avxvnni,
                         halide_target_feature_avx512_sapphirerapids},
                        halide_target_processor_generic, 0);
        }
        {
            FakeOps ops;
            ops.leaves = sapphire_leaves(leaf7_edx_amx);
            ops.xcr0 = xcr0_avx512;  // OS has not enabled the AMX state
            ok &= check("Sapphire Rapids without OS AMX support", ops, without_amx,
                        halide_target_processor_generic, 0);
        }
    }

    if (!ok) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
