#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target t1, t2;
    std::string ts;

    // Target("") should be exactly like get_host_target().
    t1 = get_host_target();
    t2 = Target("");
    if (t2 != t1) {
        printf("parse_from_string failure: %s\n", ts.c_str());
        return 1;
    }

    t1 = Target();
    ts = t1.to_string();
    if (ts != "arch_unknown-0-os_unknown") {
        printf("to_string failure: %s\n", ts.c_str());
        return 1;
    }
    // Note, this should *not* validate, since validate_target_string
    // now returns false if any of arch-bits-os are undefined
    if (Target::validate_target_string(ts)) {
        printf("validate_target_string failure: %s\n", ts.c_str());
        return 1;
    }

    // Don't attempt to roundtrip this: trying to create
    // a Target with unknown portions will now assert-fail.
    //
    // t2 = Target(ts);
    // if (t2 != t1) {
    //     printf("roundtrip failure: %s\n", ts.c_str());
    //     return 1;
    // }

    // Full specification round-trip:
    t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41});
    ts = t1.to_string();
    if (ts != "x86-32-linux-sse41") {
        printf("to_string failure: %s\n", ts.c_str());
        return 1;
    }
    if (!Target::validate_target_string(ts)) {
        printf("validate_target_string failure: %s\n", ts.c_str());
        return 1;
    }

    // Full specification round-trip, crazy features
    t1 = Target(Target::Android, Target::ARM, 32,
                {Target::JIT, Target::CUDA, Target::OpenCL,
                 Target::Debug});
    ts = t1.to_string();
    if (ts != "arm-32-android-cuda-debug-jit-opencl") {
        printf("to_string failure: %s\n", ts.c_str());
        return 1;
    }
    if (!Target::validate_target_string(ts)) {
        printf("validate_target_string failure: %s\n", ts.c_str());
        return 1;
    }

    // Expected failures:
    ts = "host-unknowntoken";
    if (Target::validate_target_string(ts)) {
        printf("validate_target_string failure: %s\n", ts.c_str());
        return 1;
    }

    ts = "x86-23";
    if (Target::validate_target_string(ts)) {
        printf("validate_target_string failure: %s\n", ts.c_str());
        return 1;
    }

    // bits == 0 is allowed only if arch_unknown and os_unknown are specified,
    // and no features are set
    ts = "x86-0";
    if (Target::validate_target_string(ts)) {
        printf("validate_target_string failure: %s\n", ts.c_str());
        return 1;
    }

    ts = "0-arch_unknown-os_unknown-sse41";
    if (Target::validate_target_string(ts)) {
        printf("validate_target_string failure: %s\n", ts.c_str());
        return 1;
    }

    // "host" is only supported as the first token
    ts = "opencl-host";
    if (Target::validate_target_string(ts)) {
        printf("validate_target_string failure: %s\n", ts.c_str());
        return 1;
    }

    // with_feature
    t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41});
    t2 = t1.with_feature(Target::NoAsserts).with_feature(Target::NoBoundsQuery);
    ts = t2.to_string();
    if (ts != "x86-32-linux-no_asserts-no_bounds_query-sse41") {
        printf("to_string failure: %s\n", ts.c_str());
        return 1;
    }

    // without_feature
    t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41, Target::NoAsserts});
    // Note that NoBoundsQuery wasn't set here, so 'without' is a no-op
    t2 = t1.without_feature(Target::NoAsserts).without_feature(Target::NoBoundsQuery);
    ts = t2.to_string();
    if (ts != "x86-32-linux-sse41") {
        printf("to_string failure: %s\n", ts.c_str());
        return 1;
    }

    // natural_vector_size
    // SSE4.1 is 16 bytes wide
    t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41});
    if (t1.natural_vector_size<uint8_t>() != 16) {
        printf("natural_vector_size failure\n");
        return 1;
    }
    if (t1.natural_vector_size<int16_t>() != 8) {
        printf("natural_vector_size failure\n");
        return 1;
    }
    if (t1.natural_vector_size<uint32_t>() != 4) {
        printf("natural_vector_size failure\n");
        return 1;
    }
    if (t1.natural_vector_size<float>() != 4) {
        printf("natural_vector_size failure\n");
        return 1;
    }

    // AVX is 32 bytes wide for float, but we treat as only 16 for integral types,
    // due to suboptimal integer instructions
    t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41, Target::AVX});
    if (t1.natural_vector_size<uint8_t>() != 16) {
        printf("natural_vector_size failure\n");
        return 1;
    }
    if (t1.natural_vector_size<int16_t>() != 8) {
        printf("natural_vector_size failure\n");
        return 1;
    }
    if (t1.natural_vector_size<uint32_t>() != 4) {
        printf("natural_vector_size failure\n");
        return 1;
    }
    if (t1.natural_vector_size<float>() != 8) {
        printf("natural_vector_size failure\n");
        return 1;
    }

    // AVX2 is 32 bytes wide
    t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41, Target::AVX, Target::AVX2});
    if (t1.natural_vector_size<uint8_t>() != 32) {
        printf("natural_vector_size failure\n");
        return 1;
    }
    if (t1.natural_vector_size<int16_t>() != 16) {
        printf("natural_vector_size failure\n");
        return 1;
    }
    if (t1.natural_vector_size<uint32_t>() != 8) {
        printf("natural_vector_size failure\n");
        return 1;
    }
    if (t1.natural_vector_size<float>() != 8) {
        printf("natural_vector_size failure\n");
        return 1;
    }

    // NEON is 16 bytes wide
    t1 = Target(Target::Linux, Target::ARM, 32);
    if (t1.natural_vector_size<uint8_t>() != 16) {
        printf("natural_vector_size failure\n");
        return 1;
    }
    if (t1.natural_vector_size<int16_t>() != 8) {
        printf("natural_vector_size failure\n");
        return 1;
    }
    if (t1.natural_vector_size<uint32_t>() != 4) {
        printf("natural_vector_size failure\n");
        return 1;
    }
    if (t1.natural_vector_size<float>() != 4) {
        printf("natural_vector_size failure\n");
        return 1;
    }

    t1 = Target("x86-64-linux-trace_all");
    ts = t1.to_string();
    if (!t1.features_all_of({Target::TraceLoads, Target::TraceStores, Target::TraceRealizations})) {
        printf("trace_all failure: %s\n", ts.c_str());
        return 1;
    }
    if (ts != "x86-64-linux-trace_all") {
        printf("trace_all to_string failure: %s\n", ts.c_str());
        return 1;
    }

    t1 = Target("arm-64-linux-armv87a-armv8a");
    t2 = Target("arm-64-linux-armv82a-armv83a");
    if (!t1.get_runtime_compatible_target(t2, t1)) {
        printf("get_runtime_compatible_target failure\n");
        return 1;
    }
    ts = t1.to_string();
    if (ts != "arm-64-linux-armv8a") {
        printf("get_runtime_compatible_target failure: %s\n", ts.c_str());
        return 1;
    }

    // Every Target::Feature must have a name, and that name must map back to
    // the same feature.
    for (int i = 0; i < (int)Target::FeatureEnd; i++) {
        Target::Feature f = (Target::Feature)i;
        std::string name = Target::feature_to_name(f);
        if (Target::feature_from_name(name) != f) {
            printf("Feature %d does not round-trip through its name (%s)\n", i, name.c_str());
            return 1;
        }
    }

    // gcd(a, b) == c, computed via get_runtime_compatible_target. An empty c
    // means the two targets have no compatible runtime.
    struct GcdTest {
        const char *a, *b, *c;
    };
    const GcdTest gcd_tests[] = {
        {"x86-64-linux-sse41-fma", "x86-64-linux-sse41-fma", "x86-64-linux-sse41-fma"},
        {"x86-64-linux-sse41-fma-no_asserts-no_runtime", "x86-64-linux-sse41-fma", "x86-64-linux-sse41-fma"},
        {"x86-64-linux-avx2-sse41", "x86-64-linux-sse41-fma", "x86-64-linux-sse41"},
        {"x86-64-linux-avx2-sse41", "x86-32-linux-sse41-fma", ""},
        {"x86-64-linux-cuda", "x86-64-linux", "x86-64-linux-cuda"},
        {"x86-64-linux-cuda-cuda_capability_50", "x86-64-linux-cuda", "x86-64-linux-cuda"},
        {"x86-64-linux-cuda-cuda_capability_50", "x86-64-linux-cuda-cuda_capability_30", "x86-64-linux-cuda-cuda_capability_30"},
        {"x86-64-linux-vulkan", "x86-64-linux", "x86-64-linux-vulkan"},
        {"x86-64-linux-vulkan-vk_v13", "x86-64-linux-vulkan", "x86-64-linux-vulkan"},
        {"x86-64-linux-vulkan-vk_v13", "x86-64-linux-vulkan-vk_v10", "x86-64-linux-vulkan-vk_v10"},
        {"hexagon-32-qurt-hvx_v65", "hexagon-32-qurt-hvx_v62", "hexagon-32-qurt-hvx_v62"},
        {"hexagon-32-qurt-hvx_v62", "hexagon-32-qurt", "hexagon-32-qurt"},
        {"hexagon-32-qurt-hvx_v62-hvx", "hexagon-32-qurt", ""},
        {"hexagon-32-qurt-hvx_v62-hvx", "hexagon-32-qurt-hvx", "hexagon-32-qurt-hvx"},
        {"x86-64-windows-d3d12compute-hlsl_sm66", "x86-64-windows-d3d12compute", "x86-64-windows-d3d12compute"},
        {"x86-64-windows-d3d12compute-hlsl_sm66", "x86-64-windows-d3d12compute-hlsl_sm60", "x86-64-windows-d3d12compute-hlsl_sm60"},
        {"x86-64-windows-d3d12compute-hlsl_sm62", "x86-64-windows-d3d12compute-hlsl_sm62", "x86-64-windows-d3d12compute-hlsl_sm62"},
        {"x86-64-windows-d3d12compute-hlsl_sm69", "x86-64-windows-d3d12compute", "x86-64-windows-d3d12compute"},
        {"x86-64-windows-d3d12compute-hlsl_sm69", "x86-64-windows-d3d12compute-hlsl_sm60", "x86-64-windows-d3d12compute-hlsl_sm60"},
    };
    for (const auto &test : gcd_tests) {
        Target result{};
        Target a{test.a};
        Target b{test.b};
        if (a.get_runtime_compatible_target(b, result)) {
            if (std::string(test.c).empty() || result != Target{test.c}) {
                printf("Targets %s and %s were computed to have gcd %s but expected '%s'\n",
                       a.to_string().c_str(), b.to_string().c_str(), result.to_string().c_str(), test.c);
                return 1;
            }
        } else if (!std::string(test.c).empty()) {
            printf("Targets %s and %s were computed to have no gcd but %s was expected\n",
                   a.to_string().c_str(), b.to_string().c_str(), test.c);
            return 1;
        }
    }

    if (Target().vector_bits != 0) {
        printf("Default Target vector_bits not 0.\n");
        return 1;
    }
    if (Target("arm-64-linux-sve2-vector_bits_512").vector_bits != 512) {
        printf("Vector bits not parsed correctly.\n");
        return 1;
    }
    Target with_vector_bits(Target::Linux, Target::ARM, 64, Target::ProcessorGeneric, {Target::SVE}, 512);
    if (with_vector_bits.vector_bits != 512) {
        printf("Vector bits not populated in constructor.\n");
        return 1;
    }
    if (Target(with_vector_bits.to_string()).vector_bits != 512) {
        printf("Vector bits not round tripped properly.\n");
        return 1;
    }

    // Feature implications. Each entry is {input, set_implied_features result,
    // normalize result}.
    struct ImpliedTest {
        const char *input;
        const char *set_implied;
        const char *normalized;
    };
    const ImpliedTest implied_tests[] = {
        // x86 AVX family
        {"x86-64-linux-avx2",
         "x86-64-linux-sse41-avx-avx2-f16c-fma",
         "x86-64-linux-avx2"},
        {"x86-64-linux-avx512_skylake",
         "x86-64-linux-sse41-avx-avx2-f16c-fma-avx512-avx512_skylake",
         "x86-64-linux-avx512_skylake"},
        {"x86-64-linux-avx512_sapphirerapids",
         "x86-64-linux-sse41-avx-avx2-f16c-fma-avxvnni-avx512-avx512_skylake-avx512_cannonlake-avx512_zen4-avx512_sapphirerapids",
         "x86-64-linux-avx512_sapphirerapids"},
        // Redundantly-specified features collapse to the minimal form.
        {"x86-64-linux-sse41-avx-avx2-f16c-fma",
         "x86-64-linux-sse41-avx-avx2-f16c-fma",
         "x86-64-linux-avx2"},
        // AVX10.1 implications depend on vector_bits.
        {"x86-64-linux-avx10_1-vector_bits_512",
         "x86-64-linux-sse41-avx-avx2-f16c-fma-avxvnni-avx512-avx512_skylake-avx512_cannonlake-avx512_zen4-avx512_sapphirerapids-avx10_1-vector_bits_512",
         "x86-64-linux-avx10_1-vector_bits_512"},
        {"x86-64-linux-avx10_1",
         "x86-64-linux-avx10_1",
         "x86-64-linux-avx10_1"},
        // ARM v8.x cascade, and SVE/SVE2 cascading through arm_fp16.
        {"arm-64-linux-armv84a",
         "arm-64-linux-armv8a-armv81a-armv82a-armv83a-armv84a",
         "arm-64-linux-armv84a"},
        {"arm-64-linux-sve2",
         "arm-64-linux-armv8a-armv81a-armv82a-arm_fp16-arm_dot_prod-sve2",
         "arm-64-linux-sve2"},
        // Apple silicon implies at least ARM v8.4a.
        {"arm-64-osx",
         "arm-64-osx-armv8a-armv81a-armv82a-armv83a-armv84a",
         "arm-64-osx"},
        // Tracing loads/stores implies tracing realizations.
        {"x86-64-linux-trace_loads",
         "x86-64-linux-trace_loads-trace_realizations",
         "x86-64-linux-trace_loads"},
    };
    for (const auto &test : implied_tests) {
        Target set_result(test.input);
        set_result.set_implied_features();
        if (set_result.get_features_bitset() != Target(test.set_implied).get_features_bitset()) {
            printf("set_implied_features(%s) gave %s but expected %s\n",
                   test.input, set_result.to_string().c_str(), test.set_implied);
            return 1;
        }
        Target norm_result(test.input);
        norm_result.normalize();
        if (norm_result.get_features_bitset() != Target(test.normalized).get_features_bitset()) {
            printf("normalize(%s) gave %s but expected %s\n",
                   test.input, norm_result.to_string().c_str(), test.normalized);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
