#include "Halide.h"

using namespace Halide;

// Extern stages are supposed to obey the following nesting property
// on bounds queries: If some region of the output O requires some
// region of the input I, then requesting any subset of O should only
// require a subset of I.
//
// This extern stage violates that property. We're going to set up a
// schedule that does a bounds query to it for an entire image, and a
// bounds query for a single scanline. For the whole-image query it
// will claim to need a modest-sized input, but for the single
// scanline query it will claim to need a much wider input. The result
// is that the bounds query is not entirely respected. The actual input
// received in non-bounds-query-mode is the intersection of what it
// asked for for a single scanline and what it asked for for the whole
// image.
extern "C" HALIDE_EXPORT_SYMBOL int misbehaving_extern_stage(halide_buffer_t *in, int variant, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        // As a baseline, require the same amount of input as output, like a copy
        memcpy(in->dim, out->dim, out->dimensions * sizeof(halide_dimension_t));
        if (out->dim[1].extent == 1) {
            // This is the inner query, for a single scanline of
            // output.
            if (variant == 0) {
                // Require a wider input, violating the nesting
                // property. Shift it over a little too. Note that it
                // still overlaps.
                in->dim[0].min += 50;
                in->dim[0].extent += 100;
            } else if (variant == 1) {
                // Require an input somewhere off in the weeds.
                in->dim[0].min = 10000;
                in->dim[0].extent = 5;
            } else {
                abort();
            }
        }
    } else {
        // The inner bounds query was fine in the y dimension, which
        // correctly nested.
        assert(in->dim[1].min == out->dim[1].min);
        assert(in->dim[1].extent == 1);

        // But the inner (bad) bounds query should not have been
        // respected in the x dimension.
        if (variant == 0) {
            // For overlapping bounds, you get the intersection of the
            // inner query and the outer query.
            // Check the left edge
            // was indeed shifted inwards, as requested by the
            // per-scanline bounds query.
            assert(in->dim[0].min == out->dim[0].min + 50);

            // Check the right edge wasn't shifted over, but was instead
            // clamped to lie within the outer bounds query.
            assert(in->dim[0].extent == out->dim[0].extent - 50);
        } else if (variant == 1) {
            // For non-overlapping bounds, you just get squashed to
            // the nearest edge.
            int right_edge = out->dim[0].min + out->dim[0].extent - 1;
            assert(in->dim[0].min == right_edge);
            assert(in->dim[0].extent == 1);
        } else {
            abort();
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    Func f, g, h;
    Var x, y;
    Param<int> variant;
    f(x, y) = x + y;
    g.define_extern("misbehaving_extern_stage", {f, variant}, Int(32), 2);
    h(x, y) = g(x, y);

    g.compute_at(h, y);
    f.compute_at(h, y);

    variant.set(0);
    h.realize({200, 200});

    variant.set(1);
    h.realize({200, 200});

    printf("Success!\n");
}
