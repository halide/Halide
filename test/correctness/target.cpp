#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Target t1, t2;
    std::string ts;

    // Target("") should be exactly like get_host_target().
    t1 = get_host_target();
    t2 = Target("");
    if (t2 != t1){
       printf("parse_from_string failure: %s\n", ts.c_str());
       return -1;
    }

    t1 = Target();
    ts = t1.to_string();
    if (ts != "arch_unknown-0-os_unknown") {
       printf("to_string failure: %s\n", ts.c_str());
       return -1;
    }
    // We expect from_string() to fail, since it doesn't know about 'unknown'
    if (Target::validate_target_string(ts)) {
       printf("validate_target_string failure: %s\n", ts.c_str());
       return -1;
    }

    // Full specification round-trip:
    t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41});
    ts = t1.to_string();
    if (ts != "x86-32-linux-sse41") {
       printf("to_string failure: %s\n", ts.c_str());
       return -1;
    }
    if (!Target::validate_target_string(ts)) {
       printf("validate_target_string failure: %s\n", ts.c_str());
       return -1;
    }

    // Full specification round-trip, crazy features
    t1 = Target(Target::Android, Target::ARM, 32,
                {Target::JIT, Target::SSE41, Target::AVX, Target::AVX2,
                 Target::CUDA, Target::OpenCL, Target::OpenGL, Target::OpenGLCompute, Target::Renderscript,
                 Target::Debug});
    ts = t1.to_string();
    if (ts != "arm-32-android-avx-avx2-cuda-debug-jit-opencl-opengl-openglcompute-renderscript-sse41") {
       printf("to_string failure: %s\n", ts.c_str());
       return -1;
    }
    if (!Target::validate_target_string(ts)) {
       printf("validate_target_string failure: %s\n", ts.c_str());
       return -1;
    }

    // Full specification round-trip, PNacl
    t1 = Target(Target::NaCl, Target::PNaCl, 32);
    ts = t1.to_string();
    if (ts != "pnacl-32-nacl") {
       printf("to_string failure: %s\n", ts.c_str());
       return -1;
    }
    if (!Target::validate_target_string(ts)) {
       printf("validate_target_string failure: %s\n", ts.c_str());
       return -1;
    }

    // Expected failures:
    ts = "host-unknowntoken";
    if (Target::validate_target_string(ts)) {
        printf("validate_target_string failure: %s\n", ts.c_str());
        return -1;
    }

    ts = "x86-23";
    if (Target::validate_target_string(ts)) {
       printf("validate_target_string failure: %s\n", ts.c_str());
       return -1;
    }

    // "host" is only supported as the first token
    ts = "opencl-host";
    if (Target::validate_target_string(ts)) {
       printf("validate_target_string failure: %s\n", ts.c_str());
       return -1;
    }

    // with_feature
    t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41});
    t2 = t1.with_feature(Target::NoAsserts).with_feature(Target::NoBoundsQuery);
    ts = t2.to_string();
    if (ts != "x86-32-linux-no_asserts-no_bounds_query-sse41") {
       printf("to_string failure: %s\n", ts.c_str());
       return -1;
    }

    // without_feature
    t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41, Target::NoAsserts});
    // Note that NoBoundsQuery wasn't set here, so 'without' is a no-op
    t2 = t1.without_feature(Target::NoAsserts).without_feature(Target::NoBoundsQuery);
    ts = t2.to_string();
    if (ts != "x86-32-linux-sse41") {
       printf("to_string failure: %s\n", ts.c_str());
       return -1;
    }

    // natural_vector_size
    // SSE4.1 is 16 bytes wide
    t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41});
    if (t1.natural_vector_size<uint8_t>() != 16) {
       printf("natural_vector_size failure\n");
       return -1;
    }
    if (t1.natural_vector_size<int16_t>() != 8) {
       printf("natural_vector_size failure\n");
       return -1;
    }
    if (t1.natural_vector_size<uint32_t>() != 4) {
       printf("natural_vector_size failure\n");
       return -1;
    }
    if (t1.natural_vector_size<float>() != 4) {
       printf("natural_vector_size failure\n");
       return -1;
    }

    // AVX is 32 bytes wide for float, but we treat as only 16 for integral types,
    // due to suboptimal integer instructions
    t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41, Target::AVX});
    if (t1.natural_vector_size<uint8_t>() != 16) {
       printf("natural_vector_size failure\n");
       return -1;
    }
    if (t1.natural_vector_size<int16_t>() != 8) {
       printf("natural_vector_size failure\n");
       return -1;
    }
    if (t1.natural_vector_size<uint32_t>() != 4) {
       printf("natural_vector_size failure\n");
       return -1;
    }
    if (t1.natural_vector_size<float>() != 8) {
       printf("natural_vector_size failure\n");
       return -1;
    }

    // AVX2 is 32 bytes wide
    t1 = Target(Target::Linux, Target::X86, 32, {Target::SSE41, Target::AVX, Target::AVX2});
    if (t1.natural_vector_size<uint8_t>() != 32) {
       printf("natural_vector_size failure\n");
       return -1;
    }
    if (t1.natural_vector_size<int16_t>() != 16) {
       printf("natural_vector_size failure\n");
       return -1;
    }
    if (t1.natural_vector_size<uint32_t>() != 8) {
       printf("natural_vector_size failure\n");
       return -1;
    }
    if (t1.natural_vector_size<float>() != 8) {
       printf("natural_vector_size failure\n");
       return -1;
    }

    // NEON is 16 bytes wide
    t1 = Target(Target::Linux, Target::ARM, 32);
    if (t1.natural_vector_size<uint8_t>() != 16) {
       printf("natural_vector_size failure\n");
       return -1;
    }
    if (t1.natural_vector_size<int16_t>() != 8) {
       printf("natural_vector_size failure\n");
       return -1;
    }
    if (t1.natural_vector_size<uint32_t>() != 4) {
       printf("natural_vector_size failure\n");
       return -1;
    }
    if (t1.natural_vector_size<float>() != 4) {
       printf("natural_vector_size failure\n");
       return -1;
    }

    printf("Success!\n");
    return 0;
}
