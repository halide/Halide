#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func producer1, producer2, consumer;
    Var x, y;

    producer1(x, y) = x + y;
    producer2(x, y) = producer1(x, y);
    consumer(x, y) = producer1(x, y - 1) + producer2(x, y + 1);

    consumer.compute_root();

    producer1.store_root().compute_at(consumer, y);
    producer2.store_root().compute_at(consumer, y).async();

    consumer.bound(x, 0, 16).bound(y, 0, 16);

    Buffer<int> out = consumer.realize(16, 16);

    return 0;
}
