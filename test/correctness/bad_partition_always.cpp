#include "Halide.h"
#include "expect_user_error.h"
using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_partition_always", "Loop Partition Policy is set to Always for f.s0.x, but no loop partitioning was performed.", []() {
        Func f("f");
        Var x("x");

        f(x) = 0;
        f.partition(x, Partition::Always);

        f.realize({10});
    });
}
