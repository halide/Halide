#include "Halide.h"

using namespace Halide;

int num_stores = 0;

int my_trace(JITUserContext *user_context, const halide_trace_event_t *e) {
    if (e->event == halide_trace_store) {
        num_stores++;
    }
    return 0;
}

int main(int argc, char **argv) {
    // An iterated stencil in a single Func without using RDoms. Not a
    // useful way to do stencils, but it demonstrates that the region
    // computed of a Func can grow as a trapezoid as you walk back
    // through the update definitions. I.e. each update definition has
    // a distinct value for the bounds of the pure vars.

    Var x, y;

    // Input
    Func f;
    f(x) = sin(x);

    Func g;
    g(x, y) = undef<float>();
    // Using pure vars only, we can only do axis-aligned data
    // movement. So we'll lift the input onto the diagonal of a 2D
    // Func...
    g(x, x) = f(x);

    const int iters = 27;

    for (int i = 0; i < iters; i++) {
        // For each iteration, first copy the diagonal up and
        // down. Pure in x.
        g(x, x + 1) = g(x, x);
        g(x, x - 1) = g(x, x);
        // Then blur the diagonal horizontally. Pure in y.
        g(y, y) = (g(y, y) + g(y - 1, y) + g(y + 1, y)) / 3.0f;
    }

    g.compute_root();

    // Read out the output
    Func h;
    h(x) = g(x, x);

    // This has the right time complexity, and is parallelizable and
    // race-condition-free, but the space complexity is absurd. This
    // is not intended to be a good way to write iterated stencils, it
    // just looks for parts of the compiler that incorrectly assume
    // the pure bounds are fixed across all update definitions.

    // Figure out the number of values of g we expect to be
    // computed. The trapezoid expands by two for each iteration of
    // the stencil. So the extent of the first iteration should be the
    // extent of the last iteration + 2*iters.
    int output_extent = 19;

    int last_iteration_extent = output_extent;
    int first_iteration_extent = output_extent + 2 * iters;

    int expected = (first_iteration_extent +                                            // pure definition
                    iters * (last_iteration_extent + 2 + first_iteration_extent) / 2 +  // first update
                    iters * (last_iteration_extent + 2 + first_iteration_extent) / 2 +  // second update
                    iters * (last_iteration_extent + first_iteration_extent - 2) / 2);  // third update

    g.trace_stores();
    h.jit_handlers().custom_trace = &my_trace;
    h.realize({output_extent});

    if (num_stores != expected) {
        printf("Did not store to g the right numbers of times\n"
               " Expected: %d\n"
               " Actual: %d\n",
               expected, num_stores);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
