#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    auto target = get_host_target();
    if (!target.features_any_of({Target::SVE, Target::SVE2})) {
        printf("[SKIP] Scalable vector is not supported on this target.\n");
        _halide_user_assert(0);
        return 1;
    }

    Func f("f");
    Var x("x");

    f(x) = x;

    const int wrong_vector_bits = target.vector_bits == 128 ? 256 : 128;
    target.vector_bits = wrong_vector_bits;

    // Compile with wrong vscale and run on host, which should end up with assertion failure.
    Buffer<int> out = f.realize({100}, target);

    printf("Success!\n");
    return 0;
}
