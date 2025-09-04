#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
struct HoistStorageContext : JITUserContext {
    int malloc_count = 0;
    int malloc_total_size = 0;

    HoistStorageContext() {
        handlers.custom_malloc = custom_malloc;
        handlers.custom_free = custom_free;
    }

    static void *custom_malloc(JITUserContext *user_context, size_t x) {
        auto *context = static_cast<HoistStorageContext *>(user_context);
        context->malloc_count++;
        context->malloc_total_size += x;
        void *orig = malloc(x + 32);
        void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
        ((void **)ptr)[-1] = orig;
        return ptr;
    }

    static void custom_free(JITUserContext *, void *ptr) {
        free(((void **)ptr)[-1]);
    }
};

class HoistStorageTest : public ::testing::Test {
protected:
    HoistStorageContext context;

    void SetUp() override {
        if (get_jit_target_from_environment().arch == Target::WebAssembly) {
            GTEST_SKIP() << "WebAssembly JIT does not support custom allocators.";
        }
    }

    static void check_result(const Buffer<int> &out) {
        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            EXPECT_EQ(out(x, y), correct) << "out(" << x << ", " << y << ")";
        });
    }

    static void check_result_with_h(const Buffer<int> &out) {
        out.for_each_element([&](int x, int y) {
            int correct = 4 * x + 5 * y;
            EXPECT_EQ(out(x, y), correct) << "out(" << x << ", " << y << ")";
        });
    }
};
}  // namespace

TEST_F(HoistStorageTest, ConstantBoundAllocationExtents) {
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);

    g.compute_root();
    f.compute_at(g, x)
        .hoist_storage(g, Var::outermost())
        // Store in heap to make sure that custom malloc is called.
        .store_in(MemoryType::Heap);

    Buffer<int> out = g.realize(&context, {128, 128});

    const int expected_malloc_count = 1;
    EXPECT_EQ(context.malloc_count, expected_malloc_count);

    const int expected_malloc_total_size = 3 * 3 * sizeof(int32_t);
    EXPECT_EQ(context.malloc_total_size, expected_malloc_total_size);

    check_result(out);
}

TEST_F(HoistStorageTest, HoistStorageRoot) {
    // Same as above, but uses hoist_storage_root.
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);

    g.compute_root();
    f.compute_at(g, x)
        .hoist_storage_root()
        // Store in heap to make sure that custom malloc is called.
        .store_in(MemoryType::Heap);

    Buffer<int> out = g.realize(&context, {128, 128});

    const int expected_malloc_count = 1;
    EXPECT_EQ(context.malloc_count, expected_malloc_count);

    const int expected_malloc_total_size = 3 * 3 * sizeof(int32_t);
    EXPECT_EQ(context.malloc_total_size, expected_malloc_total_size);

    check_result(out);
}

TEST_F(HoistStorageTest, ConstantBoundWithTiling) {
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);
    g.compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);

    f.compute_at(g, xo)
        .hoist_storage(g, Var::outermost())
        // Store in heap to make sure that custom malloc is called.
        .store_in(MemoryType::Heap);

    Buffer<int> out = g.realize(&context, {128, 128});

    const int expected_malloc_count = 1;
    EXPECT_EQ(context.malloc_count, expected_malloc_count);

    const int expected_malloc_total_size = 18 * 18 * sizeof(int32_t);
    EXPECT_EQ(context.malloc_total_size, expected_malloc_total_size);

    check_result(out);
}

TEST_F(HoistStorageTest, VariableBoundsAnalysis) {
    // Allocation extents depend on the loop variables, so needs bounds analysis to lift the allocation out.
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);
    g.compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::GuardWithIf);

    f.compute_at(g, xo)
        .hoist_storage(g, Var::outermost())
        // Store in heap to make sure that custom malloc is called.
        .store_in(MemoryType::Heap);

    Buffer<int> out = g.realize(&context, {128, 128});

    const int expected_malloc_count = 1;
    EXPECT_EQ(context.malloc_count, expected_malloc_count);

    const int expected_malloc_total_size = 18 * 18 * sizeof(int32_t);
    EXPECT_EQ(context.malloc_total_size, expected_malloc_total_size);

    check_result(out);
}

TEST_F(HoistStorageTest, PartialHoisting) {
    // Allocation extents depend on the loop variables, so needs bounds analysis to lift the allocation out.
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);
    g.compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::GuardWithIf);

    f.compute_at(g, xo)
        .hoist_storage(g, yo)
        // Store in heap to make sure that custom malloc is called.
        .store_in(MemoryType::Heap);

    Buffer<int> out = g.realize(&context, {128, 128});

    const int expected_malloc_count = 8;
    EXPECT_EQ(context.malloc_count, expected_malloc_count);

    const int expected_malloc_total_size = expected_malloc_count * 18 * 18 * sizeof(int32_t);
    EXPECT_EQ(context.malloc_total_size, expected_malloc_total_size);

    check_result(out);
}

TEST_F(HoistStorageTest, TwoFunctionsSameLevel) {
    // Two functions are hoisted at the same level.
    Func f("f"), h("h"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    h(x, y) = 2 * x + 3 * y;
    g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1) + h(x, y);

    g.compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::GuardWithIf);

    f.compute_at(g, xo)
        .hoist_storage(g, Var::outermost())
        // Store in heap to make sure that custom malloc is called.
        .store_in(MemoryType::Heap);
    h.compute_at(g, xo)
        .hoist_storage(g, Var::outermost())
        // Store in heap to make sure that custom malloc is called.
        .store_in(MemoryType::Heap);

    Buffer<int> out = g.realize(&context, {128, 128});

    const int expected_malloc_count = 2;
    EXPECT_EQ(context.malloc_count, expected_malloc_count);

    const int expected_malloc_total_size = 16 * 16 * sizeof(int32_t) + 18 * 18 * sizeof(int32_t);
    EXPECT_EQ(context.malloc_total_size, expected_malloc_total_size);

    check_result_with_h(out);
}

TEST_F(HoistStorageTest, TwoFunctionsDifferentLevels) {
    // Two functions are hoisted, but at different loop levels.
    Func f("f"), h("h"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    h(x, y) = 2 * x + 3 * y;
    g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1) + h(x, y);

    g.compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::GuardWithIf);

    f.compute_at(g, xo)
        .hoist_storage(g, Var::outermost())
        // Store in heap to make sure that custom malloc is called.
        .store_in(MemoryType::Heap);
    h.compute_at(g, xo)
        .hoist_storage(g, yo)
        // Store in heap to make sure that custom malloc is called.
        .store_in(MemoryType::Heap);

    Buffer<int> out = g.realize(&context, {128, 128});

    const int expected_malloc_count = 1 + 8;
    EXPECT_EQ(context.malloc_count, expected_malloc_count);

    const int expected_malloc_total_size = 8 * 16 * 16 * sizeof(int32_t) + 18 * 18 * sizeof(int32_t);
    EXPECT_EQ(context.malloc_total_size, expected_malloc_total_size);

    check_result_with_h(out);
}

TEST_F(HoistStorageTest, OneFunctionHoisted) {
    // There are two functions, but only one is hoisted.
    Func f("f"), h("h"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    h(x, y) = 2 * x + 3 * y;
    g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1) + h(x, y);

    g.compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::GuardWithIf);

    f.compute_at(g, xo)
        // Store in heap to make sure that custom malloc is called.
        .store_in(MemoryType::Heap);
    h.compute_at(g, xo)
        .hoist_storage(g, yo)
        // Store in heap to make sure that custom malloc is called.
        .store_in(MemoryType::Heap);

    Buffer<int> out = g.realize(&context, {128, 128});

    const int expected_malloc_count = 64 + 8;
    EXPECT_EQ(context.malloc_count, expected_malloc_count);

    const int expected_malloc_total_size = 8 * 16 * 16 * sizeof(int32_t) + 64 * 18 * 18 * sizeof(int32_t);
    EXPECT_EQ(context.malloc_total_size, expected_malloc_total_size);

    check_result_with_h(out);
}

TEST_F(HoistStorageTest, WithSpecialize) {
    // Test with specialize.
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);

    g.compute_root();
    g.specialize(g.output_buffer().width() > 64).vectorize(x, 4);
    f.compute_at(g, x)
        .hoist_storage(g, Var::outermost())
        // Store in heap to make sure that custom malloc is called.
        .store_in(MemoryType::Heap);

    Buffer<int> out(128, 128);
    g.realize(&context, out);

    const int expected_malloc_count = 1;
    EXPECT_EQ(context.malloc_count, expected_malloc_count);

    const int expected_malloc_total_size = (4 + 3 - 1) * 3 * sizeof(int32_t);
    EXPECT_EQ(context.malloc_total_size, expected_malloc_total_size);

    check_result(out);
}

TEST_F(HoistStorageTest, LiftAfterSlidingWindow) {
    // Also, check that we can lift after sliding window.
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);

    g.compute_root();
    f.compute_at(g, x)
        .store_at(g, y)
        .hoist_storage(g, Var::outermost())
        // Store in heap to make sure that custom malloc is called.
        .store_in(MemoryType::Heap);

    Buffer<int> out = g.realize(&context, {128, 128});

    const int expected_malloc_count = 1;
    EXPECT_EQ(context.malloc_count, expected_malloc_count);

    const int expected_malloc_total_size = 4 * 3 * sizeof(int32_t);
    EXPECT_EQ(context.malloc_total_size, expected_malloc_total_size);

    check_result(out);
}

TEST_F(HoistStorageTest, HoistedTupleStorage) {
    // Hoisted Tuple storage
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = {x + y, x + y};
    g(x, y) = f(x - 1, y - 1)[0] + f(x + 1, y + 1)[1];

    g.compute_root();
    f.compute_at(g, x)
        .hoist_storage(LoopLevel::root())
        // Store in heap to make sure that custom malloc is called.
        .store_in(MemoryType::Heap);

    Buffer<int> out(128, 128);
    g.realize(&context, out);

    const int expected_malloc_count = 2;
    EXPECT_EQ(context.malloc_count, expected_malloc_count);

    const int expected_malloc_total_size = 2 * 3 * 3 * sizeof(int32_t);
    EXPECT_EQ(context.malloc_total_size, expected_malloc_total_size);

    check_result(out);
}

TEST_F(HoistStorageTest, LoopLevelRoot) {
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    g(x, y) = f(x - 1, y - 1) + f(x + 1, y + 1);

    g.compute_root();
    g.specialize(g.output_buffer().width() > 64).vectorize(x, 4);
    f.compute_at(g, x)
        .hoist_storage(LoopLevel::root())
        // Store in heap to make sure that custom malloc is called.
        .store_in(MemoryType::Heap);

    Buffer<int> out(128, 128);
    g.realize(&context, out);

    const int expected_malloc_count = 1;
    EXPECT_EQ(context.malloc_count, expected_malloc_count);

    const int expected_malloc_total_size = (4 + 3 - 1) * 3 * sizeof(int32_t);
    EXPECT_EQ(context.malloc_total_size, expected_malloc_total_size);

    check_result(out);
}

TEST_F(HoistStorageTest, BoundaryConditions) {
    ImageParam input(UInt(8), 2);
    Var x{"x"}, y{"y"}, yo{"yo"}, yi{"yi"};
    Func f[3];
    f[0] = BoundaryConditions::repeat_edge(input);
    f[1](x, y) = ((f[0]((x / 2) + 2, (y / 2) + 2)) + (f[0](x + 1, y)));
    f[2](x, y) = ((f[1](x * 2, (y * 2) + -2)) + (f[1](x + -1, y + -1)));
    f[2].split(y, yo, yi, 16);
    f[0].hoist_storage(f[2], yo).compute_at(f[1], x);
    f[1].hoist_storage_root().compute_at(f[2], yi);

    ASSERT_NO_THROW(f[2].compile_jit());
}
