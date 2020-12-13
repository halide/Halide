#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func producer1, producer2, consumer;
    Var x, y;

    producer1(x, y) = x + y;
    producer2(x, y) = 3 * x + 2 * y;
    consumer(x, y) = producer1(x, y - 1) + producer1(x, y + 1) + producer2(x, y - 1) + producer2(x, y + 1);
    consumer.compute_root();
    // Both functions should have been scheduled as async.
    producer1.compute_at(consumer, y).store_root();
    producer2.compute_at(consumer, y).store_root().compute_with(producer1, y).async();

    consumer.bound(x, 0, 16).bound(y, 0, 16);

    Buffer<int> out = consumer.realize(16, 16);

    printf("Success!\n");
    return 0;
}
