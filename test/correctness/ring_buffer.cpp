#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
class RingBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (get_jit_target_from_environment().arch == Target::WebAssembly) {
            GTEST_SKIP() << "WebAssembly does not support async() yet.";
        }
    }
};
}  // namespace

TEST_F(RingBufferTest, AsyncProducerDoubleBufferedHoistAtConsumerY) {
    Func producer("producer"), consumer("consumer");
    Var x, y, xo, yo, xi, yi;

    producer(x, y) = x + y;
    consumer(x, y) = producer(x - 1, y - 1) + producer(x, y) + producer(x + 1, y + 1);

    consumer
        .compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);
    producer
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(2)
        .async();

    Buffer<int> out = consumer.realize({128, 128});

    out.for_each_element([&](int xx, int yy) {
        int correct = 3 * (xx + yy);
        EXPECT_EQ(out(xx, yy), correct) << "at (" << xx << ", " << yy << ")";
    });
}

TEST_F(RingBufferTest, AsyncProducerDoubleBufferedHoistAtRoot) {
    Func producer("producer"), consumer("consumer");
    Var x, y, xo, yo, xi, yi;

    producer(x, y) = x + y;
    consumer(x, y) = producer(x - 1, y - 1) + producer(x, y) + producer(x + 1, y + 1);

    consumer
        .compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);
    producer
        .compute_at(consumer, xo)
        .hoist_storage_root()
        .ring_buffer(2)
        .async();

    Buffer<int> out = consumer.realize({128, 128});
    out.for_each_element([&](int xx, int yy) {
        int correct = 3 * (xx + yy);
        EXPECT_EQ(out(xx, yy), correct) << "at (" << xx << ", " << yy << ")";
    });
}

TEST_F(RingBufferTest, AsyncProducerWithMultipleIntermediates) {
    Func producer("producer"), consumer("consumer"), interm1("interm1"), interm2("interm2"), interm3("interm3");
    Var x, y, xo, yo, xi, yi;

    producer(x, y) = x + y;
    interm1(x, y) = producer(x - 1, y - 1);
    interm2(x, y) = producer(x, y);
    interm3(x, y) = producer(x + 1, y + 1);

    consumer(x, y) = interm1(x, y) + interm2(x, y) + interm3(x, y);

    consumer
        .compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);
    producer
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(2)
        .async();

    interm1.compute_at(consumer, xo);
    interm2.compute_at(consumer, xo);
    interm3.compute_at(consumer, xo);

    Buffer<int> out = consumer.realize({128, 128});
    out.for_each_element([&](int xx, int yy) {
        int correct = 3 * (xx + yy);
        EXPECT_EQ(out(xx, yy), correct) << "at (" << xx << ", " << yy << ")";
    });
}

TEST_F(RingBufferTest, AsyncProducerWithMultipleIntermediatesAndDirectUse) {
    Func producer("producer"), consumer("consumer"), interm1("interm1"), interm2("interm2"), interm3("interm3");
    Var x, y, xo, yo, xi, yi;

    producer(x, y) = x + y;
    interm1(x, y) = producer(x - 1, y - 1);
    interm2(x, y) = producer(x, y);
    interm3(x, y) = producer(x + 1, y + 1);

    consumer(x, y) = interm1(x, y) + interm2(x, y) + interm3(x, y) + producer(x, y + 2);

    consumer
        .compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);
    producer
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(2)
        .async();

    interm1.compute_at(consumer, xo);
    interm2.compute_at(consumer, xo);
    interm3.compute_at(consumer, xo);

    Buffer<int> out = consumer.realize({128, 128});

    out.for_each_element([&](int xx, int yy) {
        int correct = 3 * (xx + yy) + xx + yy + 2;
        EXPECT_EQ(out(xx, yy), correct) << "at (" << xx << ", " << yy << ")";
    });
}

TEST_F(RingBufferTest, TwoAsyncProducersOneConsumer) {
    Func producer1("producer1"), producer2("producer2"), consumer("consumer");
    Var x, y, xo, yo, xi, yi;

    producer1(x, y) = x + y;
    producer2(x, y) = x * y;
    consumer(x, y) = producer1(x - 1, y - 1) + producer2(x, y) + producer1(x + 1, y + 1);

    consumer
        .compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);
    producer1
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(2)
        .async();
    producer2
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(2)
        .async();

    Buffer<int> out = consumer.realize({128, 128});
    out.for_each_element([&](int xx, int yy) {
        int correct = 2 * (xx + yy) + xx * yy;
        EXPECT_EQ(out(xx, yy), correct) << "at (" << xx << ", " << yy << ")";
    });
}

TEST_F(RingBufferTest, TwoAsyncProducersDifferentStorageLevels) {
    Func producer1("producer1"), producer2("producer2"), consumer("consumer");
    Var x, y, xo, yo, xi, yi;

    producer1(x, y) = x + y;
    producer2(x, y) = x * y;
    consumer(x, y) = producer1(x - 1, y - 1) + producer2(x, y) + producer1(x + 1, y + 1);

    consumer
        .compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);

    producer1
        .compute_at(consumer, xo)
        .hoist_storage_root()
        .ring_buffer(2)
        .async();

    producer2
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(2)
        .async();

    Buffer<int> out = consumer.realize({128, 128});
    out.for_each_element([&](int xx, int yy) {
        int correct = 2 * (xx + yy) + xx * yy;
        EXPECT_EQ(out(xx, yy), correct) << "at (" << xx << ", " << yy << ")";
    });
}

TEST_F(RingBufferTest, TwoAsyncProducersTwoConsumers) {
    Func producer1("producer1"), producer2("producer2"), interm1("interm1"), interm2("interm2"), consumer("consumer");
    Var x, y, xo, yo, xi, yi;

    producer1(x, y) = x + y;
    producer2(x, y) = x + y;
    interm1(x, y) = producer1(x - 1, y + 1) + producer2(x, y);
    interm2(x, y) = producer1(x, y) + producer2(x + 1, y - 1);
    consumer(x, y) = interm1(x, y) + interm2(x, y);

    consumer
        .compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);

    interm1.compute_at(consumer, xo);
    interm2.compute_at(consumer, xo);

    // Extents for ring_buffer() below are random to test various cases.
    producer1
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(5)
        .async();

    producer2
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(2)
        .async();

    Buffer<int> out = consumer.realize({128, 128});

    out.for_each_element([&](int xx, int yy) {
        int correct = 4 * (xx + yy);
        EXPECT_EQ(out(xx, yy), correct) << "at (" << xx << ", " << yy << ")";
    });
}

TEST_F(RingBufferTest, ThreeAsyncProducersTwoConsumers) {
    Func producer1("producer1"), producer2("producer2"), producer3("producer3");
    Func interm1("interm1"), interm2("interm2"), consumer("consumer");
    Var x, y, xo, yo, xi, yi;

    producer1(x, y) = x + y;
    producer2(x, y) = x + y;
    producer3(x, y) = x * y;
    interm1(x, y) = producer1(x - 1, y + 1) + producer2(x, y) + producer3(x - 1, y - 1);
    interm2(x, y) = producer1(x, y) + producer2(x + 1, y - 1) + producer3(x + 1, y + 1);
    consumer(x, y) = interm1(x, y) + interm2(x, y);

    consumer
        .compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);

    interm1
        .compute_at(consumer, xo);

    interm2
        .compute_at(consumer, xo)
        // Let's hoist storage of this consumer to make it more complicated.
        .hoist_storage(consumer, yo);

    // Extents for ring_buffer are random to test various cases.
    producer1
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(2)
        .async();

    producer2
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(3)
        .async();

    producer3
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(4)
        .async();

    Buffer<int> out = consumer.realize({128, 128});

    out.for_each_element([&](int xx, int yy) {
        int correct = 4 * (xx + yy) + ((xx - 1) * (yy - 1)) + ((xx + 1) * (yy + 1));
        EXPECT_EQ(out(xx, yy), correct) << "at (" << xx << ", " << yy << ")";
    });
}

TEST_F(RingBufferTest, TwoNonAsyncProducersTwoConsumers) {
    Func producer1("producer1"), producer2("producer2"), producer3("producer3");
    Func interm1("interm1"), interm2("interm2"), consumer("consumer");
    Var x, y, xo, yo, xi, yi;

    producer1(x, y) = x + y;
    producer2(x, y) = x + y;
    producer3(x, y) = x * y;
    interm1(x, y) = producer1(x - 1, y + 1) + producer2(x, y) + producer3(x - 1, y - 1);
    interm2(x, y) = producer1(x, y) + producer2(x + 1, y - 1) + producer3(x + 1, y + 1);
    consumer(x, y) = interm1(x, y) + interm2(x, y);

    consumer
        .compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);

    interm1
        .compute_at(consumer, xo);

    interm2
        .compute_at(consumer, xo)
        // Let's hoist storage of this consumer to make it more complicated.
        .hoist_storage(consumer, yo);

    // Extents for ring_buffer() below are random to test various cases.
    producer1
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(3);

    producer2
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(2);

    producer3
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(4);

    Buffer<int> out = consumer.realize({128, 128});

    out.for_each_element([&](int xx, int yy) {
        int correct = 4 * (xx + yy) + ((xx - 1) * (yy - 1)) + ((xx + 1) * (yy + 1));
        EXPECT_EQ(out(xx, yy), correct) << "at (" << xx << ", " << yy << ")";
    });
}

TEST_F(RingBufferTest, ChainOfTwoAsyncProducers) {
    Func producer1("producer1"), producer2("producer2"), consumer("consumer");
    Var x, y, xo, yo, xi, yi;

    producer1(x, y) = x + y;
    producer2(x, y) = producer1(x, y) + x * y;
    consumer(x, y) = producer2(x, y) * 2;

    consumer
        .compute_root()
        .tile(x, y, xo, yo, xi, yi, 16, 16, TailStrategy::RoundUp);
    producer1
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(2)
        .async();

    producer2
        .compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(2)
        .async();

    Buffer<int> out = consumer.realize({128, 128});

    out.for_each_element([&](int xx, int yy) {
        int correct = 2 * (xx + yy + xx * yy);
        EXPECT_EQ(out(xx, yy), correct) << "at (" << xx << ", " << yy << ")";
    });
}
