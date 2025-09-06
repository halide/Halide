#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {

int num_vector_stores = 0;
int num_scalar_stores = 0;
int my_trace(JITUserContext *user_context, const halide_trace_event_t *e) {
    if (e->event == halide_trace_store) {
        if (e->type.lanes > 1) {
            num_vector_stores++;
        } else {
            num_scalar_stores++;
        }
    }
    return 0;
}

}  // namespace

class VectorizeGuardWithIfTest : public ::testing::TestWithParam<TailStrategy> {
};

TEST_P(VectorizeGuardWithIfTest, BasicVectorization) {
    TailStrategy tail_strategy = GetParam();
    
    Func f;
    Var x;

    f(x) = x;

    const int w = 100, v = 8;
    f.vectorize(x, v, tail_strategy);
    const int expected_vector_stores = w / v;
    const int expected_scalar_stores = w % v;

    f.jit_handlers().custom_trace = &my_trace;
    f.trace_stores();

    num_vector_stores = 0;
    num_scalar_stores = 0;
    Buffer<int> result = f.realize({w});

    ASSERT_EQ(num_vector_stores, expected_vector_stores) << "There were " << num_vector_stores << " vector stores instead of " << expected_vector_stores;
    ASSERT_EQ(num_scalar_stores, expected_scalar_stores) << "There were " << num_scalar_stores << " scalar stores instead of " << expected_scalar_stores;

    for (int i = 0; i < w; i++) {
        ASSERT_EQ(result(i), i) << "result(" << i << ") == " << result(i) << " instead of " << i;
    }
}

TEST_P(VectorizeGuardWithIfTest, IndirectAccess) {
    TailStrategy tail_strategy = GetParam();
    
    const int w = 98, v = 8;

    Buffer<int> b(w / 2);
    for (int i = 0; i < w / 2; i++) {
        b(i) = i;
    }
    Func f;
    Var x;

    f(x) = b(x / 2) + x / 2;

    f.output_buffer().dim(0).set_min(0).set_extent(w);

    f.vectorize(x, v, tail_strategy);

    Buffer<int> result = f.realize({w});

    for (int i = 0; i < w; i++) {
        int expected = i / 2 + i / 2;
        ASSERT_EQ(result(i), expected) << "result(" << i << ") == " << result(i) << " instead of " << expected;
    }
}

TEST(VectorizeGuardWithIf, PredicateLoadsAndStores) {
    Var x;
    Func f, g;

    ImageParam in(Int(32), 1);

    Expr index = clamp(x * x - 2, 0, x);

    f(x) = index + in(index);
    g(x) = f(x);

    f.compute_root().vectorize(x, 8, TailStrategy::PredicateLoads);
    g.compute_root().vectorize(x, 8, TailStrategy::PredicateStores);

    const int w = 100;
    Buffer<int> buf(w);
    buf.fill(0);
    in.set(buf);
    Buffer<int> result = g.realize({w});

    for (int i = 0; i < w; i++) {
        int correct = std::max(std::min(i * i - 2, i), 0);
        ASSERT_EQ(result(i), correct) << "result(" << i << ") == " << result(i) << " instead of " << correct;
    }
}

INSTANTIATE_TEST_SUITE_P(
    TailStrategies,
    VectorizeGuardWithIfTest,
    ::testing::Values(TailStrategy::GuardWithIf, TailStrategy::Predicate)
);