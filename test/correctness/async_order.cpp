#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly does not support async() yet.\n");
        return 0;
    }

    {
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

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(-1);
            }
        });
    }
    {
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

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(-1);
            }
        });
    }

    {
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

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(-1);
            }
        });
    }

    printf("Success!\n");
    return 0;
}
