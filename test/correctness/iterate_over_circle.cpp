#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
int count = 0;
int my_trace(JITUserContext *user_context, const halide_trace_event_t *ev) {
    if (ev->event == halide_trace_load) {
        count++;
    }
    return 0;
}
}  // namespace

TEST(IterateOverCircleTest, CircularIteration) {
    Func f;
    Var x, y;

    Func in;
    in(x, y) = x + y;
    in.compute_root();

    // Set f to zero
    f(x, y) = 0;

    // Then iterate over a circle, adding in(x, y) to f.
    Expr t = cast<int>(ceil(sqrt(max(0, 10 * 10 - y * y))));
    f(x, y) += select(x > -t && x < t, in(x, y), 0);

    in.trace_loads();
    f.jit_handlers().custom_trace = my_trace;
    f.realize({20, 20});

    int expected_count = 0;
    for (int y = 0; y < 20; y++) {
        for (int x = 0; x < 20; x++) {
            if (x * x + y * y < 10 * 10) {
                expected_count++;
            }
        }
    }

    EXPECT_EQ(count, expected_count)
        << "Func 'in' should only have been loaded from points "
           "within the circle x*x + y*y < 10*10.";
}
