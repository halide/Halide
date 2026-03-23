#include "Halide.h"
using namespace Halide;

int main(int argc, char **argv) {
#ifndef HALIDE_WITH_EXCEPTIONS
    printf("[SKIP] bad_partition_always_throws requires exceptions\n");
    return 0;
#else
    try {
        Func f("f");
        Var x("x");
        f(x) = 0;
        f.partition(x, Partition::Always);
        f.realize({10});
    } catch (const CompileError &e) {
        const std::string_view msg = e.what();
        constexpr std::string_view expected_msg =
            "Loop Partition Policy is set to Always for f.s0.x, "
            "but no loop partitioning was performed.";
        if (msg.find(expected_msg) == std::string_view::npos) {
            std::cerr << "Expected error containing (" << expected_msg << "), but got (" << msg << ")\n";
            return 1;
        }
        printf("Success!\n");
        return 0;
    }

    printf("Did not see any exception!\n");
    return 1;
#endif
}
