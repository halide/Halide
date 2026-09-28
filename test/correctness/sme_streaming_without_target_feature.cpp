#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("sme_streaming_without_target_feature", "Schedule for Func f requires <SMEStreaming> but no compatible target feature is enabled in target", []() {
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
    });
}
