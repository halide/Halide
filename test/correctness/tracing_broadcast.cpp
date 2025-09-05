#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(TracingBroadcastTest, Basic) {
    Func f("f");
    Var x("x"), y("y");
    f(x, y) = 1234567890;
    f.vectorize(x, 8);

    f.trace_stores();
    f.jit_handlers().custom_trace = [](JITUserContext *, const halide_trace_event_t *e) {
        if (e->event == halide_trace_store) {
            for (int i = 0; i < e->type.lanes; ++i) {
                int val = static_cast<const int *>(e->value)[i];
                if (val != 1234567890) {
                    ADD_FAILURE() << "All values stored should have been 1234567890, instead they are: " << val;
                    return 1;
                }
            }
        }
        return 0;
    };
    ASSERT_NO_THROW(f.realize({8, 8}));
}
