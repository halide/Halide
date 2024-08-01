#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // For simplicity, only run this test on hosts that we can predict.
    Target t = get_host_target();
    if (t.arch != Target::X86 || t.bits != 64 || t.os != Target::OSX) {
        printf("[SKIP] This test only runs on x86-64-osx.\n");
        return 0;
    }

    t = t.with_feature(Target::Debug);

    // Full specification round-trip, crazy features
    Target t1 = Target(Target::OSX, Target::X86, 64,
                       {Target::CUDA, Target::Debug});

    Expr is_arm = target_arch_is(Target::ARM);
    Expr is_x86 = target_arch_is(Target::X86);
    Expr bits = target_bits();
    Expr is_android = target_os_is(Target::Android);
    Expr is_osx = target_os_is(Target::OSX);
    Expr vec = target_natural_vector_size<float>();
    Expr has_cuda = target_has_feature(Target::CUDA);
    Expr has_vulkan = target_has_feature(Target::Vulkan);

    Func f;
    Var x;

    f(x) = select(is_arm, 1, 0) +
           select(is_x86, 2, 0) +
           select(vec == 4, 4, 0) +
           select(is_android, 8, 0) +
           select(is_osx, 16, 0) +
           select(bits == 32, 32, 0) +
           select(bits == 64, 64, 0) +
           select(has_cuda, 128, 0) +
           select(has_vulkan, 256, 0);

    Buffer<int> result = f.realize({1}, t1);

    assert(result(0) == 2 + 4 + 16 + 64 + 128);

    printf("Success!\n");
    return 0;
}
