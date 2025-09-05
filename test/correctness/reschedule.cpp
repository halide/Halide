#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
bool vector_store = false, scalar_store = false;

int my_trace(JITUserContext *user_context, const halide_trace_event_t *ev) {
    if (ev->event == halide_trace_store) {
        if (ev->type.lanes > 1) {
            vector_store = true;
        } else {
            scalar_store = true;
        }
    }
    return 0;
}
}

TEST(RescheduleTest, Reschedule) {
    vector_store = false;
    scalar_store = false;
    
    Func f;
    Var x;

    f(x) = x;
    f.jit_handlers().custom_trace = &my_trace;
    f.trace_stores();

    Buffer<int> result_1;
    ASSERT_NO_THROW(result_1 = f.realize({10}));

    f.vectorize(x, 4);

    Buffer<int> result_2;
    ASSERT_NO_THROW(result_2 = f.realize({10}));

    EXPECT_TRUE(vector_store) << "There should have been vector stores";
    EXPECT_TRUE(scalar_store) << "There should have been scalar stores";
}
