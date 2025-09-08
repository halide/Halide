#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(Target, EmptyTargetString) {
    EXPECT_EQ(Target(""), get_host_target());
}

TEST(Target, UnknownTargetString) {
    Target t1 = Target();
    std::string ts = t1.to_string();
    EXPECT_EQ(ts, "arch_unknown-0-os_unknown");
    // Note, this should *not* validate, since validate_target_string
    // now returns false if any of arch-bits-os are undefined
    EXPECT_FALSE(Target::validate_target_string(ts));

    // Don't attempt to roundtrip this: trying to create
    // a Target with unknown portions will now assert-fail.
    //
    // t2 = Target(ts);
    // if (t2 != t1) {
    //     printf("roundtrip failure: %s\n", ts.c_str());
    //     return 1;
    // }
}

TEST(Target, FullSpecificationRoundTrip) {
    // Full specification round-trip:
    Target t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41});
    std::string ts = t1.to_string();
    EXPECT_EQ(ts, "x86-32-linux-sse41");
    EXPECT_TRUE(Target::validate_target_string(ts));

    // Full specification round-trip, crazy features
    t1 = Target(Target::Android, Target::ARM, 32,
                {Target::JIT, Target::CUDA, Target::OpenCL,
                 Target::Debug});
    ts = t1.to_string();
    EXPECT_EQ(ts, "arm-32-android-cuda-debug-jit-opencl");
    EXPECT_TRUE(Target::validate_target_string(ts));
}

TEST(Target, ValidationFailures) {
    // Expected failures:
    std::string ts = "host-unknowntoken";
    EXPECT_FALSE(Target::validate_target_string(ts));

    ts = "x86-23";
    EXPECT_FALSE(Target::validate_target_string(ts));

    // bits == 0 is allowed only if arch_unknown and os_unknown are specified,
    // and no features are set
    ts = "x86-0";
    EXPECT_FALSE(Target::validate_target_string(ts));

    ts = "0-arch_unknown-os_unknown-sse41";
    EXPECT_FALSE(Target::validate_target_string(ts));

    // "host" is only supported as the first token
    ts = "opencl-host";
    EXPECT_FALSE(Target::validate_target_string(ts));
}

TEST(Target, FeatureModification) {
    // with_feature
    Target t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41});
    Target t2 = t1.with_feature(Target::NoAsserts).with_feature(Target::NoBoundsQuery);
    std::string ts = t2.to_string();
    EXPECT_EQ(ts, "x86-32-linux-no_asserts-no_bounds_query-sse41");

    // without_feature
    t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41, Target::NoAsserts});
    // Note that NoBoundsQuery wasn't set here, so 'without' is a no-op
    t2 = t1.without_feature(Target::NoAsserts).without_feature(Target::NoBoundsQuery);
    ts = t2.to_string();
    EXPECT_EQ(ts, "x86-32-linux-sse41");
}

TEST(Target, NaturalVectorSizeSSE41) {
    // natural_vector_size
    // SSE4.1 is 16 bytes wide
    Target t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41});
    EXPECT_EQ(t1.natural_vector_size<uint8_t>(), 16);
    EXPECT_EQ(t1.natural_vector_size<int16_t>(), 8);
    EXPECT_EQ(t1.natural_vector_size<uint32_t>(), 4);
    EXPECT_EQ(t1.natural_vector_size<float>(), 4);
}

TEST(Target, NaturalVectorSizeAVX) {
    // AVX is 32 bytes wide for float, but we treat as only 16 for integral types,
    // due to suboptimal integer instructions
    Target t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41, Target::AVX});
    EXPECT_EQ(t1.natural_vector_size<uint8_t>(), 16);
    EXPECT_EQ(t1.natural_vector_size<int16_t>(), 8);
    EXPECT_EQ(t1.natural_vector_size<uint32_t>(), 4);
    EXPECT_EQ(t1.natural_vector_size<float>(), 8);
}

TEST(Target, NaturalVectorSizeAVX2) {
    // AVX2 is 32 bytes wide
    Target t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41, Target::AVX, Target::AVX2});
    EXPECT_EQ(t1.natural_vector_size<uint8_t>(), 32);
    EXPECT_EQ(t1.natural_vector_size<int16_t>(), 16);
    EXPECT_EQ(t1.natural_vector_size<uint32_t>(), 8);
    EXPECT_EQ(t1.natural_vector_size<float>(), 8);
}

TEST(Target, NaturalVectorSizeNEON) {
    // NEON is 16 bytes wide
    Target t1 = Target(Target::Linux, Target::ARM, 32);
    EXPECT_EQ(t1.natural_vector_size<uint8_t>(), 16);
    EXPECT_EQ(t1.natural_vector_size<int16_t>(), 8);
    EXPECT_EQ(t1.natural_vector_size<uint32_t>(), 4);
    EXPECT_EQ(t1.natural_vector_size<float>(), 4);
}

TEST(Target, TraceAll) {
    Target t1 = Target("x86-64-linux-trace_all");
    std::string ts = t1.to_string();
    EXPECT_TRUE(t1.features_all_of({Target::TraceLoads, Target::TraceStores, Target::TraceRealizations}));
    EXPECT_EQ(ts, "x86-64-linux-trace_all");
}

TEST(Target, RuntimeCompatibleTarget) {
    Target t1 = Target("arm-64-linux-armv87a-armv8a");
    Target t2 = Target("arm-64-linux-armv82a-armv83a");
    EXPECT_TRUE(t1.get_runtime_compatible_target(t2, t1));
    std::string ts = t1.to_string();
    EXPECT_EQ(ts, "arm-64-linux-armv8a");
}
