#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly does not support async() yet.\n");
        return 0;
    }

    // Double-buffer a tile of producer computed as async.
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
            .ring_buffer(2)
            .async();

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

    // Double-buffer a tile of producer computed as async, but the storage moved to the outside.
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
            .hoist_storage_root()
            .ring_buffer(2)
            .async();

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

    // Double-buffer a tile of producer computed as async with multiple intermediate consumers.
    {
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

        interm1
            .compute_at(consumer, xo);
        interm2
            .compute_at(consumer, xo);
        interm3
            .compute_at(consumer, xo);

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

    // Double-buffer a tile of producer computed as async with multiple intermediate consumers and output consumer.
    {
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

        interm1
            .compute_at(consumer, xo);
        interm2
            .compute_at(consumer, xo);
        interm3
            .compute_at(consumer, xo);

        Buffer<int> out = consumer.realize({128, 128});

        out.for_each_element([&](int x, int y) {
            int correct = 3 * (x + y) + x + y + 2;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Two async producers with double buffering and one consumer.
    {
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

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y) + x * y;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Two async producers with double buffering at different storage levels and one consumer.
    {
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

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y) + x * y;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Two async producers with ring buffers and two consumers.
    {
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

        interm1
            .compute_at(consumer, xo);

        interm2
            .compute_at(consumer, xo);

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

        out.for_each_element([&](int x, int y) {
            int correct = 4 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Three async producers with ring buffers and two consumers.
    {
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

        out.for_each_element([&](int x, int y) {
            int correct = 4 * (x + y) + ((x - 1) * (y - 1)) + ((x + 1) * (y + 1));
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Two non-async ring-buffered producers and two consumers.
    {
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

        out.for_each_element([&](int x, int y) {
            int correct = 4 * (x + y) + ((x - 1) * (y - 1)) + ((x + 1) * (y + 1));
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Chain of two async double-buffered producers and consumer.
    {
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

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y + x * y);
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