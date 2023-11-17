#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly does not support async() yet.\n");
        return 0;
    }

    // Basic compute-root async producer
    {
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
            .double_buffer();

        Buffer<int> out = consumer.realize({128, 128});

        out.for_each_element([&](int x, int y) {
            int correct = 3 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    printf("Success!\n");
    return 0;
}