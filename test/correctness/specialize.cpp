#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
struct TraceContext : JITUserContext {
    bool vector_store{false};
    bool scalar_store{false};
    uint16_t vector_store_lanes{0};

    TraceContext() {
        handlers.custom_trace = custom_trace;
    }

    static int custom_trace(JITUserContext *ctx, const halide_trace_event_t *ev) {
        auto *self = static_cast<TraceContext *>(ctx);
        if (ev->event == halide_trace_store) {
            if (ev->type.lanes > 1) {
                self->vector_store = true;
                self->vector_store_lanes = ev->type.lanes;
            } else {
                self->scalar_store = true;
            }
        }
        return 0;
    }
};

// A custom allocator that counts how many allocations are for empty buffers.
struct AllocContext : JITUserContext {
    int empty_allocs = 0, nonempty_allocs = 0, frees = 0;

    AllocContext() {
        handlers.custom_malloc = custom_malloc;
        handlers.custom_free = custom_free;
    }

    static void *custom_malloc(JITUserContext *ctx, size_t sz) {
        // Don't worry about alignment because we'll just test this with scalar code
        auto *self = static_cast<AllocContext *>(ctx);
        if (sz == 0) {
            self->empty_allocs++;
        } else {
            self->nonempty_allocs++;
        }
        return malloc(sz);
    }

    static void custom_free(JITUserContext *ctx, void *ptr) {
        auto *self = static_cast<AllocContext *>(ctx);
        self->frees++;
        free(ptr);
    }
};

// Custom lowering pass to count the number of IfThenElse statements found inside
// ProducerConsumer nodes.
struct CountIfThenElse final : Internal::IRMutator {
    int count = 0;

    using IRMutator::visit;
    Internal::Stmt visit(const Internal::ProducerConsumer *op) override {
        // Only count ifs found inside a pipeline.
        producer_consumers++;
        Internal::Stmt stmt = IRMutator::visit(op);
        producer_consumers--;
        return stmt;
    }
    Internal::Stmt visit(const Internal::IfThenElse *op) override {
        if (producer_consumers > 0) {
            count++;
        }
        return IRMutator::visit(op);
    }

private:
    int producer_consumers = 0;
};

class SpecializeTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (get_jit_target_from_environment().arch == Target::WebAssembly) {
            GTEST_SKIP() << "WebAssembly JIT does not support custom allocators.";
        }
    }
};
}  // namespace

TEST_F(SpecializeTest, NestedSpecializationWithVectorization) {
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

    f.trace_stores();

    // Just check that all the specialization didn't change the output.
    {
        param.set(true);
        TraceContext ctx;
        Buffer<int> out = f.realize(&ctx, {100});
        for (int i = 0; i < out.width(); i++) {
            EXPECT_EQ(out(i), i * 3) << "i = " << i;
        }
        param.set(false);
        ASSERT_NO_THROW(f.realize(&ctx, out));
        for (int i = 0; i < out.width(); i++) {
            EXPECT_EQ(out(i), i * 17) << "i = " << i;
        }

        ASSERT_FALSE(ctx.scalar_store) << "This was not supposed to use vector stores";
        ASSERT_TRUE(ctx.vector_store) << "This was supposed to use vector stores";
    }

    // Now try a smaller input
    {
        param.set(true);
        TraceContext ctx;
        Buffer<int> out = f.realize(&ctx, {3});
        for (int i = 0; i < out.width(); i++) {
            EXPECT_EQ(out(i), i * 3) << "i = " << i;
        }
        param.set(false);
        ASSERT_NO_THROW(f.realize(&ctx, out));
        for (int i = 0; i < out.width(); i++) {
            EXPECT_EQ(out(i), i * 17) << "i = " << i;
        }

        ASSERT_TRUE(ctx.scalar_store) << "This was supposed to use scalar stores";
        ASSERT_FALSE(ctx.vector_store) << "This was not supposed to use vector stores";
    }
}

TEST_F(SpecializeTest, SkipStagesBasedOnParameter) {
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

    {
        param.set(true);
        AllocContext ctx;
        ASSERT_NO_THROW(out.realize(&ctx, {100}));

        EXPECT_EQ(ctx.empty_allocs, 1);
        EXPECT_EQ(ctx.nonempty_allocs, 2);
        EXPECT_EQ(ctx.frees, 3);
    }

    {
        param.set(false);
        AllocContext ctx;
        ASSERT_NO_THROW(out.realize(&ctx, {100}));
        EXPECT_EQ(ctx.empty_allocs, 2);
        EXPECT_EQ(ctx.nonempty_allocs, 1);
        EXPECT_EQ(ctx.frees, 3);
    }
}

TEST_F(SpecializeTest, SpecializeOnInputStrideAndWidth) {
    // Specialize for interleaved vs planar inputs
    ImageParam im(Int(32), 1);
    im.dim(0).set_stride(Expr());  // unconstrain the stride

    Func f;
    Var x;

    f(x) = im(x);

    // If we have a stride of 1 it's worth vectorizing, but only if the width is also > 8.
    f.specialize(im.dim(0).stride() == 1 && im.width() >= 8).vectorize(x, 8);

    f.trace_stores();

    // Check bounds inference is still cool with widths < 8
    f.infer_input_bounds({5});
    int m = im.get().min(0), e = im.get().extent(0);
    EXPECT_EQ(m, 0);
    EXPECT_EQ(e, 5);

    // Check we don't crash with the small input, and that it uses scalar stores
    {
        TraceContext ctx;
        ASSERT_NO_THROW(f.realize(&ctx, {5}));
        EXPECT_TRUE(ctx.scalar_store) << "These stores were supposed to be scalar.";
        EXPECT_FALSE(ctx.vector_store) << "These stores were supposed to be scalar.";
    }

    // Check we don't crash with a larger input, and that it uses vector stores
    {
        Buffer<int> image(100);
        im.set(image);

        TraceContext ctx;
        ASSERT_NO_THROW(f.realize(&ctx, {100}));
        EXPECT_TRUE(ctx.vector_store) << "These stores were supposed to be vector.";
        EXPECT_FALSE(ctx.scalar_store) << "These stores were supposed to be vector.";
    }
}

TEST_F(SpecializeTest, DenseVsStridedInputSpecialization) {
    // Specialize a copy for dense vs. non-dense inputs.
    ImageParam im(Int(32), 1);
    im.dim(0).set_stride(Expr());  // unconstrain the stride

    Func f;
    Var x;

    f(x) = im(x);

    f.specialize(im.dim(0).stride() == 1).vectorize(x, 8);

    f.trace_stores();

    Buffer<int> strided_image(4, 100);
    strided_image.slice(0, 0);
    im.set(strided_image);

    // Check we used scalar stores for a strided input.
    {
        TraceContext ctx;
        ASSERT_NO_THROW(f.realize(&ctx, {100}));
        EXPECT_TRUE(ctx.scalar_store) << "These stores were supposed to be scalar.";
        EXPECT_FALSE(ctx.vector_store) << "These stores were supposed to be scalar.";
    }

    // Check that we used vector stores for a dense input.
    {
        Buffer<int> image(100);
        im.set(image);

        TraceContext ctx;
        ASSERT_NO_THROW(f.realize(&ctx, {100}));
        EXPECT_TRUE(ctx.vector_store) << "These stores were supposed to be vector.";
        EXPECT_FALSE(ctx.scalar_store) << "These stores were supposed to be vector.";
    }
}

TEST_F(SpecializeTest, BoundsInferenceChangesWithParameter) {
    // Bounds required of the input change depending on the param
    ImageParam im(Int(32), 1);
    Param<bool> param;

    Func f;
    Var x;
    f(x) = select(param, im(x + 10), im(x - 10));
    f.specialize(param);

    param.set(true);
    f.infer_input_bounds({100});
    EXPECT_EQ(im.get().min(0), 10);

    param.set(false);
    im.reset();
    f.infer_input_bounds({100});
    EXPECT_EQ(im.get().min(0), -10);
}

TEST_F(SpecializeTest, SpecializeUpdateDefinition) {
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
    ASSERT_NO_THROW(f.realize({100}));
}

TEST_F(SpecializeTest, UnusedInputInSpecialization) {
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
    ASSERT_NO_THROW(f.realize({100}));
}

TEST_F(SpecializeTest, SpecializationInheritsScheduling) {
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
    EXPECT_EQ(im.get().extent(0), 3);
    EXPECT_EQ(im.get().extent(1), 2)
        << "Height should be 2 because the unrolling also happens in the specialized case.";
}

TEST_F(SpecializeTest, IntermediateStagesNotSpecialized) {
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
    im.set(input);
    ASSERT_NO_THROW(out.realize(output)) << "Shouldn't throw a bounds error";
}

TEST_F(SpecializeTest, NestedStageSpecializationSimplification) {
    // Check specializations of stages nested in other stages simplify appropriately.
    ImageParam im(Int(32), 2);
    Param<bool> cond1, cond2;
    Func f, out;
    Var x, y;
    f(x, y) = im(x, y);
    out(x, y) = f(x, y);

    f.compute_at(out, x).specialize(cond1 && cond2).vectorize(x, 4, TailStrategy::RoundUp);
    out.compute_root().specialize(cond1 && cond2).vectorize(x, 4, TailStrategy::RoundUp);

    CountIfThenElse if_then_else;
    out.add_custom_lowering_pass(&if_then_else, nullptr);

    Buffer<int> input(3, 3), output(3, 3);
    im.set(input);
    cond1.set(false);
    cond2.set(false);
    ASSERT_NO_THROW(out.realize(output)) << "Shouldn't throw a bounds error";
    EXPECT_EQ(if_then_else.count, 1);
}

TEST_F(SpecializeTest, DifferentSpecializationConditionsNested) {
    // Check specializations of stages nested in other stages simplify appropriately.
    ImageParam im(Int(32), 2);
    Param<bool> cond1, cond2;
    Func f, out;
    Var x, y;
    f(x, y) = im(x, y);
    out(x, y) = f(x, y);

    f.compute_at(out, x).specialize(cond1).vectorize(x, 4, TailStrategy::RoundUp);
    out.compute_root().specialize(cond1 && cond2).vectorize(x, 4, TailStrategy::RoundUp);

    CountIfThenElse if_then_else;
    out.add_custom_lowering_pass(&if_then_else, nullptr);

    Buffer<int> input(3, 3), output(3, 3);
    // Shouldn't throw a bounds error:
    im.set(input);
    cond1.set(false);
    cond2.set(false);
    ASSERT_NO_THROW(out.realize(output));
    EXPECT_EQ(if_then_else.count, 2)
        << "There should have been 2 Ifs total: They are the"
           "outer cond1 && cond2, and the condition in the true case"
           "should have been simplified away. The If in the false"
           "branch cannot be simplified.";
}

TEST_F(SpecializeTest, ComplexExpressionSpecialization) {
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
    EXPECT_EQ(w, 10);
    EXPECT_EQ(h, 1);
    im.reset();

    p.set(-100);
    f.infer_input_bounds({10});
    w = im.get().width();
    h = im.get().height();
    EXPECT_EQ(w, 1);
    EXPECT_EQ(h, 10);
}

TEST_F(SpecializeTest, ImpliedConditionSpecialization) {
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
    EXPECT_EQ(w, 10);
    EXPECT_EQ(h, 1);
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
    EXPECT_EQ(w, 10);
    EXPECT_EQ(h, 10);
}

TEST_F(SpecializeTest, ConstantFalseSpecializationPruning) {
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

    ASSERT_EQ(f.function().definition().specializations().size(), 2);

    std::map<std::string, Internal::Function> env;
    env.insert({f.function().name(), f.function()});
    simplify_specializations(env);

    const auto &s = f.function().definition().specializations();
    ASSERT_EQ(s.size(), 1);
    // should be (something) == 0
    ASSERT_TRUE(s[0].condition.as<Internal::EQ>() && is_const_zero(s[0].condition.as<Internal::EQ>()->b));

    f.trace_stores();

    {
        p.set(0);
        TraceContext ctx;
        ASSERT_NO_THROW(f.realize(&ctx, {100}));
        ASSERT_EQ(ctx.vector_store_lanes, 32);
    }

    {
        p.set(42);  // just a nonzero value
        TraceContext ctx;
        ASSERT_NO_THROW(f.realize(&ctx, {100}));
        ASSERT_EQ(ctx.vector_store_lanes, 4);
    }
}

TEST_F(SpecializeTest, ConstantTrueSpecializationPruning) {
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

    ASSERT_EQ(f.function().definition().specializations().size(), 5);

    std::map<std::string, Internal::Function> env;
    env.insert({f.function().name(), f.function()});
    simplify_specializations(env);

    const auto &s = f.function().definition().specializations();
    // Note that this is 1 (rather than 2) because the final const-true
    // Specialization will be hoisted into the main Schedule.
    ASSERT_EQ(s.size(), 1);
    // should be (something) == 0
    ASSERT_TRUE(s[0].condition.as<Internal::EQ>() && is_const_zero(s[0].condition.as<Internal::EQ>()->b));

    f.trace_stores();

    {
        p.set(42);  // Chosen to ensure pruned branch is pruned
        TraceContext ctx;
        ASSERT_NO_THROW(f.realize(&ctx, {100}));
        ASSERT_EQ(ctx.vector_store_lanes, 16);
    }

    {
        p.set(0);
        TraceContext ctx;
        ASSERT_NO_THROW(f.realize(&ctx, {100}));
        ASSERT_EQ(ctx.vector_store_lanes, 32);
    }
}

TEST_F(SpecializeTest, ConstantTrueSpecializationPromotion) {
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

    f.trace_stores();

    {
        p.set(42);  // arbitrary nonzero value
        TraceContext ctx;
        ASSERT_NO_THROW(f.realize(&ctx, {100}));
        ASSERT_TRUE(ctx.vector_store_lanes == 16);
    }

    {
        p.set(0);
        TraceContext ctx;
        ASSERT_NO_THROW(f.realize(&ctx, {100}));
        ASSERT_TRUE(ctx.vector_store_lanes == 32);
    }
}

TEST_F(SpecializeTest, SpecializeFailHandling) {
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

    f.trace_stores();

    p.set(0);
    TraceContext ctx;
    ASSERT_NO_THROW(f.realize(&ctx, {100}));
    ASSERT_EQ(ctx.vector_store_lanes, 32);
}
