#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {

class AsyncOrderTest : public ::testing::Test {
protected:
    const Target target{get_jit_target_from_environment()};

    void SetUp() override {
        if (target.arch == Target::WebAssembly) {
            GTEST_SKIP() << "WebAssembly does not support async() yet.";
        }
    }

    static void check_result(const Buffer<int> &out) {
        out.for_each_element([&](int x, int y) {
            const int correct = 2 * (x + y);
            ASSERT_EQ(out(x, y), correct) << "out(" << x << ", " << y << ") = " << out(x, y) << " instead of " << correct;
        });
    }
};

}  // namespace

TEST_F(AsyncOrderTest, AsyncProducerWithComputeAt) {
    Func producer1, producer2, consumer;
    Var x, y;

    producer1(x, y) = x + y;
    producer2(x, y) = producer1(x, y);
    consumer(x, y) = producer1(x, y - 1) + producer2(x, y + 1);

    consumer.compute_root();

    producer1.compute_at(consumer, y);
    producer2.compute_at(consumer, y).async();

    consumer.bound(x, 0, 16).bound(y, 0, 16);

    Buffer<int> out = consumer.realize({16, 16});
    check_result(out);
}

TEST_F(AsyncOrderTest, AsyncProducerWithStoreRoot) {
    Func producer1, producer2, consumer;
    Var x, y;

    producer1(x, y) = x + y;
    producer2(x, y) = producer1(x, y);
    consumer(x, y) = producer1(x, y - 1) + producer2(x, y + 1);

    consumer.compute_root();

    producer1.compute_root();
    producer2.store_root().compute_at(consumer, y).async();

    consumer.bound(x, 0, 16).bound(y, 0, 16);

    Buffer<int> out = consumer.realize({16, 16});
    check_result(out);
}

TEST_F(AsyncOrderTest, BothProducersAsync) {
    Func producer1, producer2, consumer;
    Var x, y;

    producer1(x, y) = x + y;
    producer2(x, y) = producer1(x, y);
    consumer(x, y) = producer1(x, y - 1) + producer2(x, y + 1);

    consumer.compute_root();

    producer1.store_root().compute_at(consumer, y).async();
    producer2.store_root().compute_at(consumer, y).async();

    consumer.bound(x, 0, 16).bound(y, 0, 16);

    Buffer<int> out = consumer.realize({16, 16});
    check_result(out);
}
