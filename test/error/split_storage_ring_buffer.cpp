#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func producer("producer"), consumer("consumer");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");
    Var so("so"), si("si");

    producer(x, y) = x + y;
    consumer(x, y) = producer(x, y);
    consumer.compute_root().tile(x, y, xo, yo, xi, yi, 8, 8);
    producer.compute_at(consumer, xo)
        .hoist_storage(consumer, yo)
        .ring_buffer(2)
        .split_storage(x, so, si, 4);

    consumer.realize({16, 16});

    return 0;
}
