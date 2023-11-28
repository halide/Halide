#include "Halide.h"

using namespace Halide;

extern "C" HALIDE_EXPORT_SYMBOL int expensive(int x) {
    float f = 3.0f;
    for (int i = 0; i < (1 << 10); i++) {
        f = sqrtf(sinf(cosf(f)));
    }
    if (f < 0) return 3;
    return x;
}
HalideExtern_1(int, expensive, int);

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly does not support async() yet.\n");
        return 0;
    }

    // Basic compute-root async producer
    {
        Func producer, consumer;
        Var x, y;

        producer(x, y) = x + y;
        consumer(x, y) = expensive(producer(x - 1, y - 1) + producer(x + 1, y + 1));
        consumer.compute_root();
        producer.compute_root().async();

        Buffer<int> out = consumer.realize({16, 16});

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Sliding and folding over a single variable
    {
        Func producer, consumer;
        Var x, y;

        producer(x) = expensive(x);
        consumer(x) = expensive(producer(x) + producer(x - 1));
        consumer.compute_root();
        producer.store_root().fold_storage(x, 8).compute_at(consumer, x).async();

        Buffer<int> out = consumer.realize({16});

        out.for_each_element([&](int x) {
            int correct = 2 * x - 1;
            if (out(x) != correct) {
                printf("out(%d) = %d instead of %d\n",
                       x, out(x), correct);
                exit(1);
            }
        });
    }

    // Sliding and folding over a single variable, but flipped
    {
        Func producer, consumer;
        Var x, y;

        producer(x) = expensive(x);
        consumer(x) = expensive(producer(-x) + producer(-x + 1));
        consumer.compute_root();
        producer.store_root().fold_storage(x, 8, false).compute_at(consumer, x).async();

        Buffer<int> out = consumer.realize({16});

        out.for_each_element([&](int x) {
            int correct = -2 * x + 1;
            if (out(x) != correct) {
                printf("out(%d) = %d instead of %d\n",
                       x, out(x), correct);
                exit(1);
            }
        });
    }

    // Sliding and folding over y
    {
        Func producer, consumer;
        Var x, y;

        producer(x, y) = x + y;
        consumer(x, y) = expensive(producer(x - 1, y - 1) + producer(x + 1, y + 1));
        consumer.compute_root();
        // Producer can run 5 scanlines ahead
        producer.store_root().fold_storage(y, 8).compute_at(consumer, y).async();

        Buffer<int> out = consumer.realize({16, 16});

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Sliding over x and y, folding over y
    {
        Func producer, consumer;
        Var x, y;

        producer(x, y) = x + y;
        consumer(x, y) = expensive(producer(x - 1, y - 1) + producer(x + 1, y + 1));
        consumer.compute_root();
        // Producer can still run 5 scanlines ahead
        producer.store_root().fold_storage(y, 8).compute_at(consumer, x).async();

        Buffer<int> out = consumer.realize({16, 16});

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Sliding over x, folding over x and y. Folding over multiple
    // dimensions implies separate semaphores for each dimension
    // folded to prevent clobbering along each axis. The outer
    // semaphore never actually does anything, because the inner
    // semaphore stops it from getting that far ahead.
    {
        Func producer, consumer;
        Var x, y;

        producer(x, y) = x + y;
        // No longer a stencil in y, so that multiple dimensions can be folded
        consumer(x, y) = expensive(producer(x - 1, y) + producer(x + 1, y));
        consumer.compute_root();
        // Producer can run 5 pixels ahead within each scanline, also
        // give it some slop in y so it can run ahead to do the first
        // few pixels of the next scanline while the producer is still
        // chewing on the previous one.

        // The producer doesn't run into the new scanline as much as
        // it could, because we're sharing one semaphore for x in
        // between the two scanlines, so we're a little conservative.
        producer.store_root().fold_storage(x, 8).fold_storage(y, 2).compute_at(consumer, x).async();

        Buffer<int> out = consumer.realize({16, 16});

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Multiple async producers at root.
    {
        Func producer_1;
        Func producer_2;
        Func consumer;
        Var x, y;

        producer_1(x, y) = x;
        producer_2(x, y) = y;
        // Use different stencils to get different fold factors.
        consumer(x, y) = (producer_1(x - 1, y) + producer_1(x + 1, y) +
                          producer_2(x - 2, y) + producer_2(x + 2, y));

        producer_1.compute_root().async();
        producer_2.compute_root().async();

        Buffer<int> out = consumer.realize({16, 16});
        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Multiple async producers inside an outer parallel for loop
    {
        Func producer_1;
        Func producer_2;
        Func consumer;
        Var x, y;

        producer_1(x, y) = x;
        producer_2(x, y) = y;
        consumer(x, y) = (producer_1(x - 1, y) + producer_1(x + 1, y) +
                          producer_2(x - 2, y) + producer_2(x + 2, y));

        producer_1.compute_at(consumer, y).async();
        producer_2.compute_at(consumer, y).async();
        consumer.parallel(y);

        Buffer<int> out = consumer.realize({16, 16});
        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Multiple async producers inside an outer parallel for loop
    // with sliding within the inner serial loop
    {
        Func producer_1;
        Func producer_2;
        Func consumer;
        Var x, y;

        producer_1(x, y) = expensive(x);
        producer_2(x, y) = expensive(y);
        // Use different stencils to get different fold factors.
        consumer(x, y) = expensive((producer_1(x - 1, y) + producer_1(x + 1, y) +
                                    producer_2(x - 2, y) + producer_2(x + 2, y)));

        producer_1.compute_at(consumer, x).store_at(consumer, y).async();
        producer_2.compute_at(consumer, x).store_at(consumer, y).async();
        consumer.parallel(y);

        Buffer<int> out = consumer.realize({16, 16});
        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Nested asynchronous tasks.
    {
        Func f0, f1, f2;
        Var x, y;

        f0(x, y) = x + y;
        f1(x, y) = f0(x - 1, y - 1) + f0(x + 1, y + 1);
        f2(x, y) = f1(x - 1, y - 1) + f1(x + 1, y + 1);

        f2.compute_root();
        f1.compute_at(f2, y).async();
        f0.compute_at(f1, x).async();

        Buffer<int> out = f2.realize({16, 16});
        out.for_each_element([&](int x, int y) {
            int correct = 4 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Two async producer-consumer pairs over x in a producer-consumer
    // relationship over y.
    {
        Func producer_1;
        Func consumer_1;
        Func producer_2;
        Func consumer_2;

        Var x, y;

        producer_1(x, y) = x + y;
        consumer_1(x, y) = producer_1(x - 1, y) + producer_1(x + 1, y);
        producer_2(x, y) = consumer_1(x, y - 1) + consumer_1(x, y + 1);
        consumer_2(x, y) = producer_2(x - 1, y) + producer_2(x + 1, y);

        consumer_2.compute_root();
        producer_2.store_at(consumer_2, y).compute_at(consumer_2, x).async();
        consumer_1.store_root().compute_at(consumer_2, y).async();
        producer_1.store_at(consumer_2, y).compute_at(consumer_1, x).async();

        Buffer<int> out = consumer_2.realize({16, 16});
        out.for_each_element([&](int x, int y) {
            int correct = 8 * (x + y);
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Sliding and folding over y, with a non-constant amount of stuff
    // to acquire/release in the folding semaphore.
    {
        Func producer, consumer;
        Var x, y;

        producer(x, y) = x + y;
        consumer(x, y) = expensive(producer(x - 1, min(y - 1, 15)) + producer(x + 1, min(y + 1, 17)));
        consumer.compute_root();
        producer.store_root().fold_storage(y, 8).compute_at(consumer, y).async();

        Buffer<int> out = consumer.realize({128, 128});

        out.for_each_element([&](int x, int y) {
            int correct = (x - 1 + std::min(y - 1, 15)) + (x + 1 + std::min(y + 1, 17));
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Sliding and folding over y, with a non-constant amount of stuff
    // to acquire/release in the folding semaphore, and a flip in y
    // (the footprint marches monotonically up the image instead of
    // monotonically down the image).
    {
        Func producer, consumer;
        Var x, y;

        producer(x, y) = x + y;
        consumer(x, y) = expensive(producer(x - 1, -min(y - 1, 15)) + producer(x + 1, -min(y + 1, 17)));
        consumer.compute_root();
        producer.store_root().fold_storage(y, 8, false).compute_at(consumer, y).async();

        Buffer<int> out = consumer.realize({128, 128});

        out.for_each_element([&](int x, int y) {
            int correct = (x - 1 - std::min(y - 1, 15)) + (x + 1 - std::min(y + 1, 17));
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Downsample by 2x in y with sliding and folding over y
    {
        Func producer, consumer;
        Var x, y;

        producer(x, y) = x + y;
        // Use a lousy [1 1 1 1] downsampling kernel
        consumer(x, y) = producer(x, 2 * y - 1) + producer(x, 2 * y) + producer(x, 2 * y + 1) + producer(x, 2 * y + 2);
        consumer.compute_root();
        producer.store_root().fold_storage(y, 8).compute_at(consumer, y).async();

        Buffer<int> out = consumer.realize({16, 64});

        out.for_each_element([&](int x, int y) {
            int correct = 4 * x + 8 * y + 2;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Downsample by 1.5x in y with sliding and folding over y
    {
        Func producer, producer_up, consumer;
        Var x, y;

        producer(x, y) = x + y;
        // Use a dyadic filter equivalent to upsampling by 2x with
        // nearest neighbor then downsampling by 3x with a [1 2 3 2 1]
        // kernel.
        consumer(x, y) = select(y % 2 == 0,
                                (1 * producer(x, 3 * (y / 2) - 1) +
                                 5 * producer(x, 3 * (y / 2) + 0) +
                                 3 * producer(x, 3 * (y / 2) + 1)),
                                (3 * producer(x, 3 * (y / 2) + 1) +
                                 5 * producer(x, 3 * (y / 2) + 2) +
                                 1 * producer(x, 3 * (y / 2) + 3)));

        consumer.compute_root().align_bounds(y, 2).unroll(y, 2);
        producer.store_root().fold_storage(y, 8).compute_at(consumer, y).async();

        Buffer<int> out = consumer.realize({256, 256});

        out.for_each_element([&](int x, int y) {
            // Write it out as a 2x upsample followed by a [1 2 3
            // 2 1] downsample to check correctness and also my
            // math:
            int correct = (9 * x +
                           ((3 * y - 1) >> 1) +
                           2 * ((3 * y) >> 1) +
                           3 * ((3 * y + 1) >> 1) +
                           2 * ((3 * y + 2) >> 1) +
                           ((3 * y + 3) >> 1));
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                exit(1);
            }
        });
    }

    // Computing other stages at the outermost var of an async stage
    // should include it in the async block.
    {
        Func producer, producer_friend, consumer;
        Var x, y;

        producer_friend(x, y) = x + y;
        producer(x, y) = x + y + producer_friend(x, y);
        consumer(x, y) = producer(x, y);

        producer.compute_root().async();
        consumer.compute_root();
        producer_friend.compute_at(producer, Var::outermost());

        Buffer<int> out = consumer.realize({256, 256});

        out.for_each_element([&](int x, int y) {
            int correct = 2 * (x + y);
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
