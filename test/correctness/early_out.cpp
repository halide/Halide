#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // This is a test case that performs an or reduction using a where clause to
    // get early-out behavior on the reduction loop. It triggered two bugs.
    //
    // First, there's a param that's only used in a specialization of a wrapper
    // func, and this wasn't picked up by InferArguments.
    //
    // Second, there's a variable-free condition
    // that feeds into bounds inference (test()), and bounds inference assumed
    // that being variable-free meant it only depended on params and could be
    // lifted out into a bounds expression.
    //
    // Both of these bugs caused compilation failures, so this test just
    // verifies that things compile.

    Param<int> height;

    Var y;

    Func test_rows("test_rows");
    test_rows(y) = y < 100;

    Func test("test");
    test() = cast<bool>(false);
    RDom ry(0, 1024);
    ry.where(!test());
    test() = test_rows(ry);

    Func output;
    output() = select(test(), cast<uint8_t>(0), cast<uint8_t>(1));

    Expr num_slices = (height + 255) / 256;
    Expr slice_size = (height + num_slices - 1) / num_slices;

    test_rows.in()
        .compute_root()
        .specialize(height > slice_size)
        .parallel(y, slice_size, TailStrategy::ShiftInwards);

    output.compile_jit();

    printf("Success!\n");

    return 0;
}
