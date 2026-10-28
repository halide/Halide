#include "Halide.h"
#include "expect_user_error.h"
using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_eager_inline", "eager_inline() cannot inline reduced: it must be a pure Func with no update or extern definition and no specializations.", []() {
        Var x{"x"};
        RDom r(0, 4);
        Func reduced{"reduced"}, consumer{"consumer"};
        reduced(x) = 0;
        reduced(x) += r;  // update definition -> not pure
        consumer(x) = reduced(x);

        // A Func with an update definition is not inlinable, so eager_inline() rejects it.
        consumer.eager_inline({reduced});
    });
}
