#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    auto target = get_jit_target_from_environment();
    if (!target.has_feature(Target::SME2)) {
        printf("[SKIP] Streaming SVE is not supported on this target.\n");
        _halide_user_assert(0);
        return 1;
    }

    Func f("f");
    Var x("x");

    f(x) = x;

    const int correct_vector_bits = target.sme_streaming_vector_bits();
    const int wrong_vector_bits = correct_vector_bits == 512 ? 256 : 512;
    Target::Feature correct_sme_svl = Target::sme_svl_feature_from_bits(correct_vector_bits);
    Target::Feature wrong_sme_svl = Target::sme_svl_feature_from_bits(wrong_vector_bits);
    if (correct_sme_svl == Target::FeatureEnd || wrong_sme_svl == Target::FeatureEnd) {
        printf("Unexpected behavior in getting sme_vl feature!\n");
        return 0;  // Normal return is test failure
    }
    target = target.without_feature(correct_sme_svl).with_feature(wrong_sme_svl);

    // Compile with wrong vscale and run on host, which should end up with assertion failure.
    Buffer<int> out = f.realize({100}, target);

    printf("Success!\n");
    return 0;
}
