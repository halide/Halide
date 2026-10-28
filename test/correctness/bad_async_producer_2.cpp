#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_async_producer_2", "The Func f0 is consumed by async Func f1 and has a compute_at location in between the store_at location and the compute_at location of f1. This is only legal when f0 is both async and has a store_at location outside the store_at location of the consumer.", []() {
        Func producer1, producer2, consumer;
        Var x, y;

        producer1(x, y) = x + y;
        producer2(x, y) = producer1(x, y);
        consumer(x, y) = producer2(x, y - 1) + producer2(x, y + 1);

        consumer.compute_root();

        producer1.compute_at(consumer, y).async();
        producer2.store_root().compute_at(consumer, y).async();

        consumer.bound(x, 0, 16).bound(y, 0, 16);

        Buffer<int> out = consumer.realize({16, 16});
        (void)out;
    });
}
