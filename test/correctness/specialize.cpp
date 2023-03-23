#include "Halide.h"
#include <stdio.h>

using namespace Halide;

bool vector_store;
bool scalar_store;
uint16_t vector_store_lanes;

void reset_trace() {
    vector_store_lanes = 0;
    vector_store = scalar_store = false;
}

// A trace that checks for vector and scalar stores
int my_trace(JITUserContext *user_context, const halide_trace_event_t *ev) {

    if (ev->event == halide_trace_store) {
        if (ev->type.lanes > 1) {
            vector_store = true;
            vector_store_lanes = ev->type.lanes;
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

void *my_malloc(JITUserContext *ctx, size_t sz) {
    // Don't worry about alignment because we'll just test this with scalar code
    if (sz == 0) {
        empty_allocs++;
    } else {
        nonempty_allocs++;
    }
    return malloc(sz);
}

void my_free(JITUserContext *ctx, void *ptr) {
    frees++;
    free(ptr);
}

// Custom lowering pass to count the number of IfThenElse statements found inside
// ProducerConsumer nodes.
int if_then_else_count = 0;
class CountIfThenElse : public Internal::IRMutator {
    int producer_consumers;

public:
    CountIfThenElse()
        : producer_consumers(0) {
    }

    Internal::Stmt visit(const Internal::ProducerConsumer *op) override {
        // Only count ifs found inside a pipeline.
        producer_consumers++;
        Internal::Stmt stmt = IRMutator::visit(op);
        producer_consumers--;
        return stmt;
    }
    Internal::Stmt visit(const Internal::IfThenElse *op) override {
        if (producer_consumers > 0) {
            if_then_else_count++;
        }
        return Internal::IRMutator::visit(op);
    }
    using Internal::IRMutator::visit;
};

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support custom allocators.\n");
        return 0;
    }

    {
        Param<bool> param;

        Func f;
        Var x;
        f(x) = select(param, x * 3, x * 17);

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

        f.jit_handlers().custom_trace = my_trace;
        f.trace_stores();

        Buffer<int> out(100);

        // Just check that all the specialization didn't change the output.
        param.set(true);
        reset_trace();
        f.realize(out);
        for (int i = 0; i < out.width(); i++) {
            int correct = i * 3;
            if (out(i) != correct) {
                printf("out(%d) was %d instead of %d\n",
                       i, out(i), correct);
            }
        }
        param.set(false);
        f.realize(out);
        for (int i = 0; i < out.width(); i++) {
            int correct = i * 17;
            if (out(i) != correct) {
                printf("out(%d) was %d instead of %d\n",
                       i, out(i), correct);
            }
        }

        // Should have used vector stores
        if (!vector_store || scalar_store) {
            printf("This was supposed to use vector stores\n");
            return 1;
        }

        // Now try a smaller input
        out = Buffer<int>(3);
        param.set(true);
        reset_trace();
        f.realize(out);
        for (int i = 0; i < out.width(); i++) {
            int correct = i * 3;
            if (out(i) != correct) {
                printf("out(%d) was %d instead of %d\n",
                       i, out(i), correct);
            }
        }
        param.set(false);
        f.realize(out);
        for (int i = 0; i < out.width(); i++) {
            int correct = i * 17;
            if (out(i) != correct) {
                printf("out(%d) was %d instead of %d\n",
                       i, out(i), correct);
            }
        }

        // Should have used scalar stores
        if (vector_store || !scalar_store) {
            printf("This was supposed to use scalar stores\n");
            return 1;
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
        out.jit_handlers().custom_malloc = my_malloc;
        out.jit_handlers().custom_free = my_free;

        reset_alloc_counts();
        param.set(true);
        out.realize({100});

        if (empty_allocs != 1 || nonempty_allocs != 2 || frees != 3) {
            printf("There were supposed to be 1 empty alloc, 2 nonempty allocs, and 3 frees.\n"
                   "Instead we got %d empty allocs, %d nonempty allocs, and %d frees.\n",
                   empty_allocs, nonempty_allocs, frees);
            return 1;
        }

        reset_alloc_counts();
        param.set(false);
        out.realize({100});

        if (empty_allocs != 2 || nonempty_allocs != 1 || frees != 3) {
            printf("There were supposed to be 2 empty allocs, 1 nonempty alloc, and 3 frees.\n"
                   "Instead we got %d empty allocs, %d nonempty allocs, and %d frees.\n",
                   empty_allocs, nonempty_allocs, frees);
            return 1;
        }
    }

    {
        // Specialize for interleaved vs planar inputs
        ImageParam im(Int(32), 1);
        im.dim(0).set_stride(Expr());  // unconstrain the stride

        Func f;
        Var x;

        f(x) = im(x);

        // If we have a stride of 1 it's worth vectorizing, but only if the width is also > 8.
        f.specialize(im.dim(0).stride() == 1 && im.width() >= 8).vectorize(x, 8);

        f.trace_stores();
        f.jit_handlers().custom_trace = &my_trace;

        // Check bounds inference is still cool with widths < 8
        f.infer_input_bounds({5});
        int m = im.get().min(0), e = im.get().extent(0);
        if (m != 0 || e != 5) {
            printf("min, extent = %d, %d instead of 0, 5\n", m, e);
            return 1;
        }

        // Check we don't crash with the small input, and that it uses scalar stores
        reset_trace();
        f.realize({5});
        if (!scalar_store || vector_store) {
            printf("These stores were supposed to be scalar.\n");
            return 1;
        }

        // Check we don't crash with a larger input, and that it uses vector stores
        Buffer<int> image(100);
        im.set(image);

        reset_trace();
        f.realize({100});
        if (scalar_store || !vector_store) {
            printf("These stores were supposed to be vector.\n");
            return 1;
        }
    }

    {
        // Specialize a copy for dense vs. non-dense inputs.
        ImageParam im(Int(32), 1);
        im.dim(0).set_stride(Expr());  // unconstrain the stride

        Func f;
        Var x;

        f(x) = im(x);

        f.specialize(im.dim(0).stride() == 1).vectorize(x, 8);

        f.trace_stores();
        f.jit_handlers().custom_trace = &my_trace;

        Buffer<int> strided_image(4, 100);
        strided_image.slice(0, 0);
        im.set(strided_image);

        // Check we used scalar stores for a strided input.
        reset_trace();
        f.realize({100});
        if (!scalar_store || vector_store) {
            printf("These stores were supposed to be scalar.\n");
            return 1;
        }

        // Check that we used vector stores for a dense input.
        Buffer<int> image(100);
        im.set(image);

        reset_trace();
        f.realize({100});
        if (scalar_store || !vector_store) {
            printf("These stores were supposed to be vector.\n");
            return 1;
        }
    }

    {
        // Bounds required of the input change depending on the param
        ImageParam im(Int(32), 1);
        Param<bool> param;

        Func f;
        Var x;
        f(x) = select(param, im(x + 10), im(x - 10));
        f.specialize(param);

        param.set(true);
        f.infer_input_bounds({100});
        int m = im.get().min(0);
        if (m != 10) {
            printf("min %d instead of 10\n", m);
            return 1;
        }
        param.set(false);
        im.reset();
        f.infer_input_bounds({100});
        m = im.get().min(0);
        if (m != -10) {
            printf("min %d instead of -10\n", m);
            return 1;
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
        f.realize({100});
    }

    {
        // What happens to bounds inference if an input is not used at
        // all for a given specialization?
        ImageParam im(Int(32), 1);
        Param<bool> param;
        Func f;
        Var x;

        f(x) = select(param, im(x), 0);

        f.specialize(param);

        param.set(false);
        Buffer<int> image(10);
        im.set(image);
        // The image is too small, but that should be OK, because the
        // param is false so the image will never be used.
        f.realize({100});
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
        f.infer_input_bounds({3, 1});

        if (im.get().extent(0) != 3) {
            printf("extent(0) was supposed to be 3.\n");
            return 1;
        }

        if (im.get().extent(1) != 2) {
            // Height is 2, because the unrolling also happens in the
            // specialized case.
            printf("extent(1) was supposed to be 2.\n");
            return 1;
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

        Expr w = out.output_buffer().dim(0).extent();
        out.output_buffer().dim(0).set_min(0);

        f.compute_root().specialize(w >= 4).vectorize(x, 4);
        g.compute_root().vectorize(x, 4);
        h.compute_root().vectorize(x, 4);
        out.specialize(w >= 4).vectorize(x, 4);

        Buffer<int> input(3), output(3);
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

        f.compute_at(out, x).specialize(cond1 && cond2).vectorize(x, 4, TailStrategy::RoundUp);
        out.compute_root().specialize(cond1 && cond2).vectorize(x, 4, TailStrategy::RoundUp);

        if_then_else_count = 0;
        CountIfThenElse pass1;
        for (auto ff : out.compile_to_module(out.infer_arguments()).functions()) {
            pass1.mutate(ff.body);
        }

        Buffer<int> input(3, 3), output(3, 3);
        // Shouldn't throw a bounds error:
        im.set(input);
        out.realize(output);

        if (if_then_else_count != 1) {
            printf("Expected 1 IfThenElse stmts. Found %d.\n", if_then_else_count);
            return 1;
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

        f.compute_at(out, x).specialize(cond1).vectorize(x, 4, TailStrategy::RoundUp);
        out.compute_root().specialize(cond1 && cond2).vectorize(x, 4, TailStrategy::RoundUp);

        if_then_else_count = 0;
        CountIfThenElse pass2;
        for (auto ff : out.compile_to_module(out.infer_arguments()).functions()) {
            pass2.mutate(ff.body);
        }

        Buffer<int> input(3, 3), output(3, 3);
        // Shouldn't throw a bounds error:
        im.set(input);
        out.realize(output);

        // There should have been 2 Ifs total: They are the
        // outer cond1 && cond2, and the condition in the true case
        // should have been simplified away. The If in the false
        // branch cannot be simplified.
        if (if_then_else_count != 2) {
            printf("Expected 2 IfThenElse stmts. Found %d.\n", if_then_else_count);
            return 1;
        }
    }

    {
        // Check specialization on a more complex expression used in a select.
        ImageParam im(Int(32), 2);
        Param<int> p;
        Expr test = (p > 73) || (p * p + p + 1 == 0);

        Func f;
        Var x;
        f(x) = select(test, im(x, 0), im(0, x));
        f.specialize(test);

        // Selects evaluate both sides, so evaluating ten values of
        // this Func (ignoring the specialization) requires a 10x10
        // box of the input (The union of a 10x1 box and a 1x10
        // box). The specialization means that instead of depending on
        // the union, we either depend on a wide or a tall box,
        // depending on the param.

        p.set(100);
        f.infer_input_bounds({10});
        int w = im.get().width();
        int h = im.get().height();
        if (w != 10 || h != 1) {
            printf("Incorrect inferred size: %d %d\n", w, h);
            return 1;
        }
        im.reset();

        p.set(-100);
        f.infer_input_bounds({10});
        w = im.get().width();
        h = im.get().height();
        if (w != 1 || h != 10) {
            printf("Incorrect inferred size: %d %d\n", w, h);
            return 1;
        }
    }

    {
        // Check specialization of an implied condition
        ImageParam im(Int(32), 2);
        Param<int> p;
        Expr test = (p > 73);

        Func f;
        Var x;
        f(x) = select(p > 50, im(x, 0), im(0, x));
        f.specialize(test);

        // (p > 73) implies (p > 50), so if the condition holds (as it
        // does when p is 100), we only access the first row of the
        // input, and bounds inference should recognize this.
        p.set(100);
        f.infer_input_bounds({10});
        int w = im.get().width();
        int h = im.get().height();
        if (w != 10 || h != 1) {
            printf("Incorrect inferred size: %d %d\n", w, h);
            return 1;
        }
        im.reset();

        // (p <= 73) doesn't tell us anything about (p > 50), so when
        // the condition doesn't hold, we can make no useful
        // simplifications. The select remains, so both sides of it
        // are evaluated, so the image must be loaded over the full
        // square.
        p.set(-100);
        f.infer_input_bounds({10});
        w = im.get().width();
        h = im.get().height();
        if (w != 10 || h != 10) {
            printf("Incorrect inferred size: %d %d\n", w, h);
            return 1;
        }
    }

    {
        Var x, y;
        Param<int> p;
        Expr const_false = Expr(0) == Expr(1);
        Expr const_true = Expr(0) == Expr(0);
        Expr different_const_true = Expr(1) == Expr(1);

        // Check that we aggressively prune specialize(const-false)
        Func f;
        f(x) = x;
        f.specialize(p == 0).vectorize(x, 32);      // will *not* be pruned
        f.specialize(const_false).vectorize(x, 8);  // will be pruned
        f.vectorize(x, 4);                          // default case, not a specialization

        _halide_user_assert(f.function().definition().specializations().size() == 2);

        std::map<std::string, Internal::Function> env;
        env.insert({f.function().name(), f.function()});
        simplify_specializations(env);

        const auto &s = f.function().definition().specializations();
        _halide_user_assert(s.size() == 1);
        // should be (something) == 0
        _halide_user_assert(s[0].condition.as<Internal::EQ>() && is_const_zero(s[0].condition.as<Internal::EQ>()->b));

        f.jit_handlers().custom_trace = &my_trace;
        f.trace_stores();

        vector_store_lanes = 0;
        p.set(0);
        f.realize({100});
        _halide_user_assert(vector_store_lanes == 32);

        vector_store_lanes = 0;
        p.set(42);  // just a nonzero value
        f.realize({100});
        _halide_user_assert(vector_store_lanes == 4);
    }

    {
        Var x;
        Param<int> p;
        Expr const_false = Expr(0) == Expr(1);
        Expr const_true = Expr(0) == Expr(0);
        Expr different_const_true = Expr(1) == Expr(1);

        // Check that we aggressively prune all specializations after specialize(const-true)
        Func f;
        f(x) = x;
        f.specialize(p == 0).vectorize(x, 32);      // will *not* be pruned
        f.specialize(const_true).vectorize(x, 16);  // will *not* be pruned
        f.specialize(const_false).vectorize(x, 4);  // will be pruned
        f.specialize(p == 42).vectorize(x, 8);      // will be pruned
        f.specialize(const_true);                   // dupe of call above, won't add new specialization
        // Note that specialize() will return the same schedule for subsequent
        // calls with the same Expr, but doesn't guarantee that all Exprs
        // that evaluate to the same value collapse. Use a deliberately-
        // different Expr here to check that we do elide these.
        f.specialize(different_const_true);  // will be pruned

        _halide_user_assert(f.function().definition().specializations().size() == 5);

        std::map<std::string, Internal::Function> env;
        env.insert({f.function().name(), f.function()});
        simplify_specializations(env);

        const auto &s = f.function().definition().specializations();
        // Note that this is 1 (rather than 2) because the final const-true
        // Specialization will be hoisted into the main Schedule.
        _halide_user_assert(s.size() == 1);
        // should be (something) == 0
        _halide_user_assert(s[0].condition.as<Internal::EQ>() && is_const_zero(s[0].condition.as<Internal::EQ>()->b));

        f.jit_handlers().custom_trace = &my_trace;
        f.trace_stores();

        vector_store_lanes = 0;
        p.set(42);  // Chosen to ensure pruned branch is pruned
        f.realize({100});
        _halide_user_assert(vector_store_lanes == 16);

        vector_store_lanes = 0;
        p.set(0);
        f.realize({100});
        _halide_user_assert(vector_store_lanes == 32);
    }

    {
        Var x;
        Param<int> p;
        Expr const_true = Expr(0) == Expr(0);
        Expr different_const_true = Expr(1) == Expr(1);

        // Check that if we promote a final const-true specialize, we keep the
        // implicit compute/store_root required for outputs.
        Func f("foof");
        f(x) = x;
        f.specialize(p == 0).vectorize(x, 32);  // will *not* be pruned
        f.specialize(const_true).vectorize(x, 16);

        f.jit_handlers().custom_trace = &my_trace;
        f.trace_stores();

        vector_store_lanes = 0;
        p.set(42);  // arbitrary nonzero value
        f.realize({100});
        _halide_user_assert(vector_store_lanes == 16);

        vector_store_lanes = 0;
        p.set(0);
        f.realize({100});
        _halide_user_assert(vector_store_lanes == 32);
    }

    {
        Var x;
        Param<int> p;

        // Check that specialize_fail() is correctly skipped.
        Func f;
        f(x) = x;
        f.specialize(p == 0);
        f.specialize_fail("Unhandled Param value encountered.");
        // It's OK to retrieve an existing specialization after specialize_fail()...
        f.specialize(p == 0).vectorize(x, 32);
        // ...but it's *not* ok to create a new specialization after specialize_fail()
        // f.specialize(p == 1);  -- would fail
        // Also not ok to have duplicate specialize_fail() calls.
        // f.specialize_fail("This is bad.");  -- would fail

        f.jit_handlers().custom_trace = &my_trace;
        f.trace_stores();

        vector_store_lanes = 0;
        p.set(0);
        f.realize({100});
        _halide_user_assert(vector_store_lanes == 32);
    }

    printf("Success!\n");
    return 0;
}
