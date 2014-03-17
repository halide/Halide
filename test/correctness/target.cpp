#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target t1, t2;
    std::string ts;

    // parse_target_string("") should be exactly like get_host_target().
    t1 = get_host_target();
    t2 = parse_target_string("");
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
    if (t2.from_string(ts)) {
       printf("from_string failure: %s\n", ts.c_str());
       return -1;
    }
    if (t2 != t1) {
       printf("compare failure: %s %s\n", t1.to_string().c_str(), t2.to_string().c_str());
       return -1;
    }

    // Full specification round-trip:
    t1 = Target(Target::Linux, Target::X86, 32, Target::SSE41);
    ts = t1.to_string();
    if (ts != "x86-32-linux-sse41") {
       printf("to_string failure: %s\n", ts.c_str());
       return -1;
    }
    if (!t2.from_string(ts)) {
       printf("from_string failure: %s\n", ts.c_str());
       return -1;
    }
    if (t2 != t1) {
       printf("compare failure: %s %s\n", t1.to_string().c_str(), t2.to_string().c_str());
       return -1;
    }

    // Full specification round-trip, crazy features
    int64_t features = Target::JIT | Target::SSE41 | Target::AVX |
            Target::AVX2 | Target::CUDA | Target::OpenCL |
            Target::GPUDebug | Target::SPIR | Target::SPIR64;
    t1 = Target(Target::Android, Target::ARM, 32, features);
    ts = t1.to_string();
    if (ts != "arm-32-android-jit-sse41-avx-avx2-cuda-opencl-gpu_debug-spir-spir64") {
       printf("to_string failure: %s\n", ts.c_str());
       return -1;
    }
    if (!t2.from_string(ts)) {
       printf("from_string failure: %s\n", ts.c_str());
       return -1;
    }
    if (t2 != t1) {
       printf("compare failure: %s %s\n", t1.to_string().c_str(), t2.to_string().c_str());
       return -1;
    }

    // Partial specification merging: os,arch,bits get replaced; features get combined
    t2 = Target(Target::Linux, Target::X86, 64, Target::OpenCL);
    if (!t2.merge_string("x86-32-sse41")) {
       printf("merge_string failure: %s\n", ts.c_str());
       return -1;
    }
    // expect 32 (not 64), and both sse41 and opencl
    if (t2.to_string() != "x86-32-linux-sse41-opencl") {
       printf("merge_string: %s\n", t2.to_string().c_str());
       return -1;
    }

    // Expected failures:
    ts = "host-unknowntoken";
    if (t2.from_string(ts)) {
       printf("from_string failure: %s\n", ts.c_str());
       return -1;
    }

    ts = "x86-23";
    if (t2.from_string(ts)) {
       printf("from_string failure: %s\n", ts.c_str());
       return -1;
    }

    // "host" is only supported as the first token
    ts = "opencl-host";
    if (t2.from_string(ts)) {
       printf("from_string failure: %s\n", ts.c_str());
       return -1;
    }

    printf("Success!\n");
    return 0;
}
