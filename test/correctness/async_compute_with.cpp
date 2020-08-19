#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // Two producers scheduled as async and two separate consumers.
    {
        Func producer1, producer2, consumer, consumer1, consumer2;
        Var x, y;

        producer1(x, y) = x + y;
        producer2(x, y) = 3 * x + 2 * y;
        consumer1(x, y) = producer2(x, y);
        consumer2(x, y) = producer1(x, y);
        consumer(x, y) = consumer1(x, y) + consumer2(x, y);

        consumer.compute_root();
        consumer1.compute_root();
        consumer2.compute_root();

        producer1.compute_root().async();
        producer2.compute_root().compute_with(producer1, Var::outermost()).async();

        consumer.bound(x, 0, 16).bound(y, 0, 16);

        Buffer<int> out = consumer.realize(16, 16);

        out.for_each_element([&](int x, int y) {
            int correct = 4 * x + 3 * y;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(-1);
            }
        });
    }

    // Two producers scheduled as async and one consumers.
    {
        Func producer1, producer2, producer3, consumer, consumer1, consumer2;
        Var x, y;

        producer1(x, y) = x + y;
        producer2(x, y) = 3 * x + 2 * y;
        consumer(x, y) = producer1(x, y - 3) + producer1(x, y + 3) + producer2(x, y - 1) + producer2(x, y + 1);
        consumer.compute_root();
        producer1.compute_at(consumer, y).store_root().async();
        producer2.compute_at(consumer, y).store_root().compute_with(producer1, y).async();

        consumer.bound(x, 0, 16).bound(y, 0, 16);

        Buffer<int> out = consumer.realize(16, 16);

        out.for_each_element([&](int x, int y) {
            int correct = 8 * x + 6 * y;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(-1);
            }
        });
    }
    // Two fused producers + one producer scheduled as async and one consumers.
    {
        Func producer1, producer2, producer3, consumer;
        Var x, y;

        producer1(x, y) = x + y;
        producer2(x, y) = 3 * x + 2 * y;
        producer3(x, y) = x + y;
        consumer(x, y) = producer1(x, y - 1) + producer1(x, y + 1) + producer2(x, y - 1) + producer2(x, y + 1) + producer3(x, y);
        consumer.compute_root();
        producer1.compute_at(consumer, y).store_root().async();
        producer2.compute_at(consumer, y).store_root().compute_with(producer1, y).async();
        producer3.compute_at(consumer, y).store_root().async();

        consumer.bound(x, 0, 16).bound(y, 0, 16);

        Buffer<int> out = consumer.realize(16, 16);

        out.for_each_element([&](int x, int y) {
            int correct = 9 * x + 7 * y;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(-1);
            }
        });
    }

    // Two producers scheduled as async + one producer and one consumer.
    {
        Func producer1, producer2, producer3, consumer;
        Var x, y;

        producer1(x, y) = x + y;
        producer2(x, y) = 3 * x + 2 * y;
        producer3(x, y) = x + y;
        consumer(x, y) = producer1(x, y - 1) + producer1(x, y + 1) + producer2(x, y - 1) + producer2(x, y + 1) + producer3(x, y);
        consumer.compute_root();
        producer1.compute_at(consumer, y).store_root().async();
        producer2.compute_at(consumer, y).store_root().compute_with(producer1, y).async();
        // producer3 is not async.
        producer3.compute_at(consumer, y).store_root();

        consumer.bound(x, 0, 16).bound(y, 0, 16);

        Buffer<int> out = consumer.realize(16, 16);

        out.for_each_element([&](int x, int y) {
            int correct = 9 * x + 7 * y;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(-1);
            }
        });
    }

    // Two producers scheduled as async and two separate consumers.
    {
        Func producer1, producer2, producer3, consumer, consumer1, consumer2;
        Var x, y;

        producer1(x, y) = x + y;
        producer2(x, y) = 3 * x + 2 * y;
        consumer1(x, y) = 2 * producer1(x, y) + producer2(x, y);
        consumer2(x, y) = producer1(x, y) + 2 * producer2(x, y);
        consumer(x, y) = consumer1(x, y) + consumer2(x, y);
        consumer.compute_root();
        consumer1.compute_root();
        consumer2.compute_root();
        producer1.compute_root().async();
        producer2.compute_root().compute_with(producer1, Var::outermost()).async();

        consumer.bound(x, 0, 16).bound(y, 0, 16);

        Buffer<int> out = consumer.realize(16, 16);

        out.for_each_element([&](int x, int y) {
            int correct = 12 * x + 9 * y;
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
