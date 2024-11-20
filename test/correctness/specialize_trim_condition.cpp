#include "Halide.h"
#include "HalideRuntime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

using namespace Halide;

int load_count = 0;

// A trace that records the number of loads
int my_trace(JITUserContext *user_context, const halide_trace_event_t *ev) {

    if (ev->event == halide_trace_load) {
        load_count++;
    }
    return 0;
}

int main(int argc, char **argv) {
    Param<float> scale_factor_x, scale_factor_y;
    ImageParam input(UInt(8), 2);

    Var x, y;

    Func f;
    Expr upsample_x = scale_factor_x > cast<float>(1.0f);
    Expr upsample_y = scale_factor_y > cast<float>(1.0f);
    Expr upsample = upsample_x && upsample_y;
    Expr downsample = !upsample_x && !upsample_y;

    f(x, y) = select(upsample, input(cast<int>(x / 2), cast<int>(y / 2)),
                     select(downsample, input(x * 2, y * 2), 0));

    input.trace_loads();
    f.jit_handlers().custom_trace = &my_trace;

    // Impossible condition
    // f.specialize(upsample && downsample);
    f.specialize(upsample && !downsample);
    f.specialize(!upsample && downsample);
    f.specialize(!upsample && !downsample);
    f.specialize_fail("Unreachable condition");

    Buffer<uint8_t> img(16, 16);
    input.set(img);

    {
        // In this specialization, one of the select branches should be trimmed,
        // resulting in one load per output pixel
        load_count = 0;
        scale_factor_x.set(2.0f);
        scale_factor_y.set(2.0f);
        Buffer<uint8_t> out = f.realize({8, 8});
        assert(load_count == 64);
    }
    {
        // In this specialization, no select can be trimmed,
        // resulting in two loads per output pixel
        load_count = 0;
        scale_factor_x.set(0.5f);
        scale_factor_y.set(2.0f);
        Buffer<uint8_t> out = f.realize({8, 8});
        assert(load_count == 128);
    }
    {
        // In this specialization, one of the select branches should be trimmed,
        // resulting in one load per output pixel
        load_count = 0;
        scale_factor_x.set(0.5f);
        scale_factor_y.set(0.5f);
        Buffer<uint8_t> out = f.realize({8, 8});
        assert(load_count == 64);
    }

    printf("Success!\n");
    return 0;
}
