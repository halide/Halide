#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x("x");

    f(x) = x;
    f.compute_root().sme_streaming();

    Target target = get_jit_target_from_environment()
                        .without_feature(Target::SME2)
                        .without_feature(Target::SME_SVL128)
                        .without_feature(Target::SME_SVL256)
                        .without_feature(Target::SME_SVL512)
                        .without_feature(Target::SME_SVL1024)
                        .without_feature(Target::SME_SVL2048);

    f.compile_jit(target);

    printf("Success!\n");
    return 0;
}
