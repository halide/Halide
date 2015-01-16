#include <Halide.h>
#include <stdio.h>

using namespace Halide;

bool vector_store;
bool scalar_store;

void reset_trace() {
    vector_store = scalar_store = false;
}

// A trace that checks for vector and scalar stores
int my_trace(void *user_context, const halide_trace_event *ev) {

    if (ev->event == halide_trace_store) {
        if (ev->vector_width > 1) {
            vector_store = true;
        } else {
            scalar_store = true;
        }
    }
    return 0;
}

// A custom allocator that counts how many allocations are for empty buffers.
int empty_allocs = 0, nonempty_allocs = 0, frees = 0;

void reset_alloc_counts() {
    empty_allocs = nonempty_allocs = frees = 0;
}

void *my_malloc(void *ctx, size_t sz) {
    // Don't worry about alignment because we'll just test this with scalar code
    if (sz == 0) {
        empty_allocs++;
    } else {
        nonempty_allocs++;
    }
    return malloc(sz);
}

void my_free(void *ctx, void *ptr) {
    frees++;
    free(ptr);
}

// Custom lowering pass to count the number of IfThenElse statements found inside
// pipelines.
int if_then_else_count = 0;
class CountIfThenElse : public Internal::IRMutator {
    int pipelines;

public:
    CountIfThenElse() : pipelines(0) {}

    void visit(const Internal::Pipeline *op) {
        // Only count ifs found inside a pipeline.
        pipelines++;
        IRMutator::visit(op);
        pipelines--;
    }
    void visit(const Internal::IfThenElse *op) {
        if (pipelines > 0) {
            if_then_else_count++;
        }
        Internal::IRMutator::visit(op);
    }
    using Internal::IRMutator::visit;
};

int main(int argc, char **argv) {
    {
        Param<bool> param;

        Func f;
        Var x;
        f(x) = select(param, x*3, x*17);

        // Vectorize when the output is large enough
        Expr cond = (f.output_buffer().width() >= 4);
        f.specialize(cond).vectorize(x, 4);

        // This has created a specialization of f that is
        // vectorized. Now we want to further specialize both the
        // default case and the special case based on param. We can
        // retrieve a reference to the specialization using the same
        // condition again:
        f.specialize(cond).specialize(param);

        // Now specialize the narrow case on param as well
        f.specialize(param);

        f.set_custom_trace(&my_trace);
        f.trace_stores();

        Image<int> out(100);

        // Just check that all the specialization didn't change the output.
        param.set(true);
        reset_trace();
        f.realize(out);
        for (int i = 0; i < out.width(); i++) {
            int correct = i*3;
            if (out(i) != correct) {
                printf("out(%d) was %d instead of %d\n",
                       i, out(i), correct);
            }
        }
        param.set(false);
        f.realize(out);
        for (int i = 0; i < out.width(); i++) {
            int correct = i*17;
            if (out(i) != correct) {
                printf("out(%d) was %d instead of %d\n",
                       i, out(i), correct);
            }
        }

        // Should have used vector stores
        if (!vector_store  || scalar_store) {
            printf("This was supposed to use vector stores\n");
            return -1;
        }

        // Now try a smaller input
        out = Image<int>(3);
        param.set(true);
        reset_trace();
        f.realize(out);
        for (int i = 0; i < out.width(); i++) {
            int correct = i*3;
            if (out(i) != correct) {
                printf("out(%d) was %d instead of %d\n",
                       i, out(i), correct);
            }
        }
        param.set(false);
        f.realize(out);
        for (int i = 0; i < out.width(); i++) {
            int correct = i*17;
            if (out(i) != correct) {
                printf("out(%d) was %d instead of %d\n",
                       i, out(i), correct);
            }
        }

        // Should have used scalar stores
        if (vector_store || !scalar_store) {
            printf("This was supposed to use scalar stores\n");
            return -1;
        }

    }

    {
        Func f1, f2, g1, g2;
        Var x;

        // Define pipeline A
        f1(x) = x + 7;
        g1(x) = f1(x) + f1(x + 1);

        // Define pipeline B
        f2(x) = x * 34;
        g2(x) = f2(x) + f2(x - 1);

        // Switch between them based on a boolean param
        Param<bool> param;
        Func out;
        out(x) = select(param, g1(x), g2(x));

        // These will be outside the condition that specializes out,
        // but skip stages will nuke their allocation and computation
        // for us.
        f1.compute_root();
        g1.compute_root();
        f2.compute_root();

        out.specialize(param);

        // Count allocations.
        out.set_custom_allocator(&my_malloc, &my_free);

        reset_alloc_counts();
        param.set(true);
        out.realize(100);

        if (empty_allocs != 1 || nonempty_allocs != 2 || frees != 3) {
            printf("There were supposed to be 1 empty alloc, 2 nonempty allocs, and 3 frees.\n"
                   "Instead we got %d empty allocs, %d nonempty allocs, and %d frees.\n",
                   empty_allocs, nonempty_allocs, frees);
            return -1;
        }

        reset_alloc_counts();
        param.set(false);
        out.realize(100);

        if (empty_allocs != 2 || nonempty_allocs != 1 || frees != 3) {
            printf("There were supposed to be 2 empty allocs, 1 nonempty alloc, and 3 frees.\n"
                   "Instead we got %d empty allocs, %d nonempty allocs, and %d frees.\n",
                   empty_allocs, nonempty_allocs, frees);
            return -1;
        }
    }

    {
        // Specialize for interleaved vs planar inputs
        ImageParam im(Float(32), 1);
        im.set_stride(0, Expr()); // unconstrain the stride

        Func f;
        Var x;

        f(x) = im(x);

        // If we have a stride of 1 it's worth vectorizing, but only if the width is also > 8.
        f.specialize(im.stride(0) == 1 && im.width() >= 8).vectorize(x, 8);

        f.trace_stores();
        f.set_custom_trace(&my_trace);

        // Check bounds inference is still cool with widths < 8
        f.infer_input_bounds(5);
        int m = im.get().min(0), e = im.get().extent(0);
        if (m != 0 || e != 5) {
            printf("min, extent = %d, %d instead of 0, 5\n", m, e);
            return -1;
        }

        // Check we don't crash with the small input, and that it uses scalar stores
        reset_trace();
        f.realize(5);
        if (!scalar_store || vector_store) {
            printf("These stores were supposed to be scalar.\n");
            return -1;
        }

        // Check we don't crash with a larger input, and that it uses vector stores
        Image<float> image(100);
        im.set(image);

        reset_trace();
        f.realize(100);
        if (scalar_store || !vector_store) {
            printf("These stores were supposed to be vector.\n");
            return -1;
        }

    }

    {
        // Bounds required of the input change depending on the param
        ImageParam im(Float(32), 1);
        Param<bool> param;

        Func f;
        Var x;
        f(x) = select(param, im(x + 10), im(x - 10));
        f.specialize(param);

        param.set(true);
        f.infer_input_bounds(100);
        int m = im.get().min(0);
        if (m != 10) {
            printf("min %d instead of 10\n", m);
            return -1;
        }
        param.set(false);
        im.set(Buffer());
        f.infer_input_bounds(100);
        m = im.get().min(0);
        if (m != -10) {
            printf("min %d instead of -10\n", m);
            return -1;
        }

    }

    {
        // Specialize an update definition
        Func f;
        Var x;
        Param<int> start, size;
        RDom r(start, size);


        f(x) = x;
        f(r) = 10 - r;

        // Special-case for when we only update one element of f
        f.update().specialize(size == 1);

        // Also special-case updating no elements of f
        f.update().specialize(size == 0);

        start.set(0);
        size.set(1);

        // Not crashing is enough
        f.realize(100);
    }

    {
        // What happens to bounds inference if an input is not used at
        // all for a given specialization?
        ImageParam im(Float(32), 1);
        Param<bool> param;
        Func f;
        Var x;

        f(x) = select(param, im(x), 0.0f);

        f.specialize(param);

        param.set(false);
        Image<float> image(10);
        im.set(image);
        // The image is too small, but that should be OK, because the
        // param is false so the image will never be used.
        f.realize(100);

    }

    {
        // Specialization inherits the scheduling directives done so far:

        ImageParam im(Int(32), 2);
        Func f;
        Var x, y;
        f(x, y) = im(x, y);

        Expr cond = f.output_buffer().width() >= 4;

        // Unroll y by two innermost.
        f.reorder(y, x).unroll(y, 2).reorder(x, y);

        // Vectorize if the output is at least 4-wide. Inherits the
        // unrolling already done.
        f.specialize(cond).vectorize(x, 4);

        // Confirm that the unrolling applies to both cases using bounds inference:
        f.infer_input_bounds(3, 1);

        if (im.get().extent(0) != 3) {
            printf("extent(0) was supposed to be 3.\n");
            return -1;
        }

        if (im.get().extent(1) != 2) {
            // Height is 2, because the unrolling also happens in the
            // specialized case.
            printf("extent(1) was supposed to be 2.\n");
            return -1;
        }

    }

    {
        // Check we don't need to specialize intermediate stages.
        ImageParam im(Int(32), 1);
        Func f, g, h, out;
        Var x;
        f(x) = im(x);
        g(x) = f(x);
        h(x) = g(x);
        out(x) = h(x);

        Expr w = out.output_buffer().extent(0);
        out.output_buffer().set_min(0, 0);

        f.compute_root().specialize(w >= 4).vectorize(x, 4);
        g.compute_root().vectorize(x, 4);
        h.compute_root().vectorize(x, 4);
        out.specialize(w >= 4).vectorize(x, 4);

        Image<int> input(3), output(3);
        // Shouldn't throw a bounds error:
        im.set(input);
        out.realize(output);
    }

    {
        // Check specializations of stages nested in other stages simplify appropriately.
        ImageParam im(Int(32), 2);
        Param<bool> cond1, cond2;
        Func f, out;
        Var x, y;
        f(x, y) = im(x, y);
        out(x, y) = f(x, y);

        f.compute_at(out, x).specialize(cond1 && cond2).vectorize(x, 4);
        out.compute_root().specialize(cond1 && cond2).vectorize(x, 4);

        if_then_else_count = 0;
        out.add_custom_lowering_pass(new CountIfThenElse());

        Image<int> input(3, 3), output(3, 3);
        // Shouldn't throw a bounds error:
        im.set(input);
        out.realize(output);

        if (if_then_else_count != 1) {
            printf("Found other than 1 IfThenElse stmts.\n");
            return -1;
        }
    }

    {
        // Check specializations of stages nested in other stages simplify appropriately.
        ImageParam im(Int(32), 2);
        Param<bool> cond1, cond2;
        Func f, out;
        Var x, y;
        f(x, y) = im(x, y);
        out(x, y) = f(x, y);

        f.compute_at(out, x).specialize(cond1).vectorize(x, 4);
        out.compute_root().specialize(cond1 && cond2).vectorize(x, 4);

        if_then_else_count = 0;
        out.add_custom_lowering_pass(new CountIfThenElse());

        Image<int> input(3, 3), output(3, 3);
        // Shouldn't throw a bounds error:
        im.set(input);
        out.realize(output);

        // There should have been 2 Ifs: The outer cond1 && cond2, and
        // the condition in the true case should have been simplified
        // away. The If in the false branch cannot be simplified.
        if (if_then_else_count != 2) {
            printf("Found other than 2 IfThenElse stmts.\n");
            return -1;
        }
    }

    printf("Success!\n");
    return 0;

}
