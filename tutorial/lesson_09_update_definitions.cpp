// Halide tutorial lesson 9: Multi-pass Funcs, update definitions, and reductions

// On linux, you can compile and run it like so:
// g++ lesson_09*.cpp -g -std=c++11 -I ../include -I ../tools -L ../bin -lHalide `libpng-config --cflags --ldflags` -ljpeg -lpthread -ldl -fopenmp -o lesson_09
// LD_LIBRARY_PATH=../bin ./lesson_09

// On os x (will only work if you actually have g++, not Apple's pretend g++ which is actually clang):
// g++ lesson_09*.cpp -g -std=c++11 -I ../include -I ../tools -L ../bin -lHalide `libpng-config --cflags --ldflags` -ljpeg -fopenmp -o lesson_09
// DYLD_LIBRARY_PATH=../bin ./lesson_09

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_09_update_definitions
// in a shell with the current directory at the top of the halide
// source tree.

#include "Halide.h"
#include <stdio.h>

// We're going to be using x86 SSE intrinsics later on in this lesson.
#ifdef __SSE2__
#include <emmintrin.h>
#endif

// We'll also need a clock to do performance testing at the end.
#include "clock.h"

using namespace Halide;

// Support code for loading pngs.
#include "halide_image_io.h"
using namespace Halide::Tools;

int main(int argc, char **argv) {
    // Declare some Vars to use below.
    Var x("x"), y("y");

    // Load a grayscale image to use as an input.
    Buffer<uint8_t> input = load_image("images/gray.png");

    // You can define a Func in multiple passes. Let's see a toy
    // example first.
    {
        // The first definition must be one like we have seen already
        // - a mapping from Vars to an Expr:
        Func f;
        f(x, y) = x + y;
        // We call this first definition the "pure" definition.

        // But the later definitions can include computed expressions on
        // both sides. The simplest example is modifying a single point:
        f(3, 7) = 42;

        // We call these extra definitions "update" definitions, or
        // "reduction" definitions. A reduction definition is an
        // update definition that recursively refers back to the
        // function's current value at the same site:
        f(x, y) = f(x, y) + 17;

        // If we confine our update to a single row, we can
        // recursively refer to values in the same column:
        f(x, 3) = f(x, 0) * f(x, 10);

        // Similarly, if we confine our update to a single column, we
        // can recursively refer to other values in the same row.
        f(0, y) = f(0, y) / f(3, y);

        // The general rule is: Each Var used in an update definition
        // must appear unadorned in the same position as in the pure
        // definition in all references to the function on the left-
        // and right-hand sides. So the following definitions are
        // legal updates:
        f(x, 17) = x + 8;
        f(0, y) = y * 8;
        f(x, x + 1) = x + 8;
        f(y/2, y) = f(0, y) * 17;

        // But these ones would cause an error:

        // f(x, 0) = f(x + 1, 0);
        // First argument to f on the right-hand-side must be 'x', not 'x + 1'.

        // f(y, y + 1) = y + 8;
        // Second argument to f on the left-hand-side must be 'y', not 'y + 1'.

        // f(y, x) = y - x;
        // Arguments to f on the left-hand-side are in the wrong places.

        // f(3, 4) = x + y;
        // Free variables appear on the right-hand-side but not the left-hand-side.

        // We'll realize this one just to make sure it compiles. The
        // second-to-last definition forces us to realize over a
        // domain that is taller than it is wide.
        f.realize(100, 101);

        // For each realization of f, each step runs in its entirety
        // before the next one begins. Let's trace the loads and
        // stores for a simpler example:
        Func g("g");
        g(x, y) = x + y;   // Pure definition
        g(2, 1) = 42;      // First update definition
        g(x, 0) = g(x, 1); // Second update definition

        g.trace_loads();
        g.trace_stores();

        g.realize(4, 4);

        // See figures/lesson_09_update.gif for a visualization.

        // Reading the log, we see that each pass is applied in
        // turn. The equivalent C is:
        int result[4][4];
        // Pure definition
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                result[y][x] = x + y;
            }
        }
        // First update definition
        result[1][2] = 42;
        // Second update definition
        for (int x = 0; x < 4; x++) {
            result[0][x] = result[1][x];
        }
    }

    // Putting update passes inside loops.
    {
        // Starting with this pure definition:
        Func f;
        f(x, y) = (x + y)/100.0f;

        // Say we want an update that squares the first fifty rows. We
        // could do this by adding 50 update definitions:

        // f(x, 0) = f(x, 0) * f(x, 0);
        // f(x, 1) = f(x, 1) * f(x, 1);
        // f(x, 2) = f(x, 2) * f(x, 2);
        // ...
        // f(x, 49) = f(x, 49) * f(x, 49);

        // Or equivalently using a compile-time loop in our C++:
        // for (int i = 0; i < 50; i++) {
        //   f(x, i) = f(x, i) * f(x, i);
        // }

        // But it's more manageable and more flexible to put the loop
        // in the generated code. We do this by defining a "reduction
        // domain" and using it inside an update definition:
        RDom r(0, 50);
        f(x, r) = f(x, r) * f(x, r);
        Buffer<float> halide_result = f.realize(100, 100);

        // See figures/lesson_09_update_rdom.mp4 for a visualization.

        // The equivalent C is:
        float c_result[100][100];
        for (int y = 0; y < 100; y++) {
            for (int x = 0; x < 100; x++) {
                c_result[y][x] = (x + y)/100.0f;
            }
        }
        for (int x = 0; x < 100; x++) {
            for (int r = 0; r < 50; r++) {
                // The loop over the reduction domain occurs inside of
                // the loop over any pure variables used in the update
                // step:
                c_result[r][x] = c_result[r][x] * c_result[r][x];
            }
        }

        // Check the results match:
        for (int y = 0; y < 100; y++) {
            for (int x = 0; x < 100; x++) {
                if (fabs(halide_result(x, y) - c_result[y][x]) > 0.01f) {
                    printf("halide_result(%d, %d) = %f instead of %f\n",
                           x, y, halide_result(x, y), c_result[y][x]);
                    return -1;
                }
            }
        }
    }

    // Now we'll examine a real-world use for an update definition:
    // computing a histogram.
    {

        // Some operations on images can't be cleanly expressed as a pure
        // function from the output coordinates to the value stored
        // there. The classic example is computing a histogram. The
        // natural way to do it is to iterate over the input image,
        // updating histogram buckets. Here's how you do that in Halide:
        Func histogram("histogram");

        // Histogram buckets start as zero.
        histogram(x) = 0;

        // Define a multi-dimensional reduction domain over the input image:
        RDom r(0, input.width(), 0, input.height());

        // For every point in the reduction domain, increment the
        // histogram bucket corresponding to the intensity of the
        // input image at that point.
        histogram(input(r.x, r.y)) += 1;

        Buffer<int> halide_result = histogram.realize(256);

        // The equivalent C is:
        int c_result[256];
        for (int x = 0; x < 256; x++) {
            c_result[x] = 0;
        }
        for (int r_y = 0; r_y < input.height(); r_y++) {
            for (int r_x = 0; r_x < input.width(); r_x++) {
                c_result[input(r_x, r_y)] += 1;
            }
        }

        // Check the answers agree:
        for (int x = 0; x < 256; x++) {
            if (c_result[x] != halide_result(x)) {
                printf("halide_result(%d) = %d instead of %d\n",
                       x, halide_result(x), c_result[x]);
                return -1;
            }
        }
    }

    // Scheduling update steps
    {
        // The pure variables in an update step and can be
        // parallelized, vectorized, split, etc as usual.

        // Vectorizing, splitting, or parallelize the variables that
        // are part of the reduction domain is trickier. We'll cover
        // that in a later lesson.

        // Consider the definition:
        Func f;
        f(x, y) = x * y;
        // Set row zero to each row 8
        f(x, 0) = f(x, 8);
        // Set column zero equal to column 8 plus 2
        f(0, y) = f(8, y) + 2;

        // The pure variables in each stage can be scheduled
        // independently. To control the pure definition, we schedule
        // as we have done in the past. The following code vectorizes
        // and parallelizes the pure definition only.
        f.vectorize(x, 4).parallel(y);

        // We use Func::update(int) to get a handle to an update step
        // for the purposes of scheduling. The following line
        // vectorizes the first update step across x. We can't do
        // anything with y for this update step, because it doesn't
        // use y.
        f.update(0).vectorize(x, 4);

        // Now we parallelize the second update step in chunks of size
        // 4.
        Var yo, yi;
        f.update(1).split(y, yo, yi, 4).parallel(yo);

        Buffer<int> halide_result = f.realize(16, 16);

        // See figures/lesson_09_update_schedule.mp4 for a visualization.

        // Here's the equivalent (serial) C:
        int c_result[16][16];

        // Pure step. Vectorized in x and parallelized in y.
        for (int y = 0; y < 16; y++) { // Should be a parallel for loop
            for (int x_vec = 0; x_vec < 4; x_vec++) {
                int x[] = {x_vec*4, x_vec*4+1, x_vec*4+2, x_vec*4+3};
                c_result[y][x[0]] = x[0] * y;
                c_result[y][x[1]] = x[1] * y;
                c_result[y][x[2]] = x[2] * y;
                c_result[y][x[3]] = x[3] * y;
            }
        }

        // First update. Vectorized in x.
        for (int x_vec = 0; x_vec < 4; x_vec++) {
            int x[] = {x_vec*4, x_vec*4+1, x_vec*4+2, x_vec*4+3};
            c_result[0][x[0]] = c_result[8][x[0]];
            c_result[0][x[1]] = c_result[8][x[1]];
            c_result[0][x[2]] = c_result[8][x[2]];
            c_result[0][x[3]] = c_result[8][x[3]];
        }

        // Second update. Parallelized in chunks of size 4 in y.
        for (int yo = 0; yo < 4; yo++) { // Should be a parallel for loop
            for (int yi = 0; yi < 4; yi++) {
                int y = yo*4 + yi;
                c_result[y][0] = c_result[y][8] + 2;
            }
        }

        // Check the C and Halide results match:
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                if (halide_result(x, y) != c_result[y][x]) {
                    printf("halide_result(%d, %d) = %d instead of %d\n",
                           x, y, halide_result(x, y), c_result[y][x]);
                    return -1;
                }
            }
        }
    }

    // That covers how to schedule the variables within a Func that
    // uses update steps, but what about producer-consumer
    // relationships that involve compute_at and store_at? Let's
    // examine a reduction as a producer, in a producer-consumer pair.
    {
        // Because an update does multiple passes over a stored array,
        // it's not meaningful to inline them. So the default schedule
        // for them does the closest thing possible. It computes them
        // in the innermost loop of their consumer. Consider this
        // trivial example:
        Func producer, consumer;
        producer(x) = x*2;
        producer(x) += 10;
        consumer(x) = 2 * producer(x);
        Buffer<int> halide_result = consumer.realize(10);

        // See figures/lesson_09_inline_reduction.gif for a visualization.

        // The equivalent C is:
        int c_result[10];
        for (int x = 0; x < 10; x++)  {
            int producer_storage[1];
            // Pure step for producer
            producer_storage[0] = x * 2;
            // Update step for producer
            producer_storage[0] = producer_storage[0] + 10;
            // Pure step for consumer
            c_result[x] = 2 * producer_storage[0];
        }

        // Check the results match
        for (int x = 0; x < 10; x++) {
            if (halide_result(x) != c_result[x]) {
                printf("halide_result(%d) = %d instead of %d\n",
                       x, halide_result(x), c_result[x]);
                return -1;
            }
        }

        // For all other compute_at/store_at options, the reduction
        // gets placed where you would expect, somewhere in the loop
        // nest of the consumer.
    }

    // Now let's consider a reduction as a consumer in a
    // producer-consumer pair. This is a little more involved.
    {
        {
            // Case 1: The consumer references the producer in the pure step only.
            Func producer, consumer;
            // The producer is pure.
            producer(x) = x*17;
            consumer(x) = 2 * producer(x);
            consumer(x) += 50;

            // The valid schedules for the producer in this case are
            // the default schedule - inlined, and also:
            //
            // 1) producer.compute_at(x), which places the computation of
            // the producer inside the loop over x in the pure step of the
            // consumer.
            //
            // 2) producer.compute_root(), which computes all of the
            // producer ahead of time.
            //
            // 3) producer.store_root().compute_at(x), which allocates
            // space for the consumer outside the loop over x, but fills
            // it in as needed inside the loop.
            //
            // Let's use option 1.

            producer.compute_at(consumer, x);

            Buffer<int> halide_result = consumer.realize(10);

            // See figures/lesson_09_compute_at_pure.gif for a visualization.

            // The equivalent C is:
            int c_result[10];
            // Pure step for the consumer
            for (int x = 0; x < 10; x++)  {
                // Pure step for producer
                int producer_storage[1];
                producer_storage[0] = x * 17;
                c_result[x] = 2 * producer_storage[0];
            }
            // Update step for the consumer
            for (int x = 0; x < 10; x++) {
                c_result[x] += 50;
            }

            // All of the pure step is evaluated before any of the
            // update step, so there are two separate loops over x.

            // Check the results match
            for (int x = 0; x < 10; x++) {
                if (halide_result(x) != c_result[x]) {
                    printf("halide_result(%d) = %d instead of %d\n",
                           x, halide_result(x), c_result[x]);
                    return -1;
                }
            }
        }

        {
            // Case 2: The consumer references the producer in the update step only
            Func producer, consumer;
            producer(x) = x * 17;
            consumer(x) = 100 - x * 10;
            consumer(x) += producer(x);

            // Again we compute the producer per x coordinate of the
            // consumer. This places producer code inside the update
            // step of the consumer, because that's the only step that
            // uses the producer.
            producer.compute_at(consumer, x);

            // Note however, that we didn't say:
            //
            // producer.compute_at(consumer.update(0), x).
            //
            // Scheduling is done with respect to Vars of a Func, and
            // the Vars of a Func are shared across the pure and
            // update steps.

            Buffer<int> halide_result = consumer.realize(10);

            // See figures/lesson_09_compute_at_update.gif for a visualization.

            // The equivalent C is:
            int c_result[10];
            // Pure step for the consumer
            for (int x = 0; x < 10; x++)  {
                c_result[x] = 100 - x * 10;
            }
            // Update step for the consumer
            for (int x = 0; x < 10; x++) {
                // Pure step for producer
                int producer_storage[1];
                producer_storage[0] = x * 17;
                c_result[x] += producer_storage[0];
            }


            // Check the results match
            for (int x = 0; x < 10; x++) {
                if (halide_result(x) != c_result[x]) {
                    printf("halide_result(%d) = %d instead of %d\n",
                           x, halide_result(x), c_result[x]);
                    return -1;
                }
            }
        }

        {
            // Case 3: The consumer references the producer in
            // multiple steps that share common variables
            Func producer, consumer;
            producer(x) = x * 17;
            consumer(x) = 170 - producer(x);
            consumer(x) += producer(x)/2;

            // Again we compute the producer per x coordinate of the
            // consumer. This places producer code inside both the
            // pure and the update step of the consumer. So there end
            // up being two separate realizations of the producer, and
            // redundant work occurs.
            producer.compute_at(consumer, x);

            Buffer<int> halide_result = consumer.realize(10);

            // See figures/lesson_09_compute_at_pure_and_update.gif for a visualization.

            // The equivalent C is:
            int c_result[10];
            // Pure step for the consumer
            for (int x = 0; x < 10; x++)  {
                // Pure step for producer
                int producer_storage[1];
                producer_storage[0] = x * 17;
                c_result[x] = 170 - producer_storage[0];
            }
            // Update step for the consumer
            for (int x = 0; x < 10; x++) {
                // Another copy of the pure step for producer
                int producer_storage[1];
                producer_storage[0] = x * 17;
                c_result[x] += producer_storage[0]/2;
            }

            // Check the results match
            for (int x = 0; x < 10; x++) {
                if (halide_result(x) != c_result[x]) {
                    printf("halide_result(%d) = %d instead of %d\n",
                           x, halide_result(x), c_result[x]);
                    return -1;
                }
            }
        }

        {
            // Case 4: The consumer references the producer in
            // multiple steps that do not share common variables
            Func producer, consumer;
            producer(x, y) = (x * y) / 10 + 8;
            consumer(x, y) = x + y;
            consumer(x, 0) = producer(x, x);
            consumer(0, y) = producer(y, 9-y);

            // In this case neither producer.compute_at(consumer, x)
            // nor producer.compute_at(consumer, y) will work, because
            // either one fails to cover one of the uses of the
            // producer. So we'd have to inline producer, or use
            // producer.compute_root().

            // Let's say we really really want producer to be
            // compute_at the inner loops of both consumer update
            // steps. Halide doesn't allow multiple different
            // schedules for a single Func, but we can work around it
            // by making two wrappers around producer, and scheduling
            // those instead:

            // Attempt 2:
            Func producer_1, producer_2, consumer_2;
            producer_1(x, y) = producer(x, y);
            producer_2(x, y) = producer(x, y);

            consumer_2(x, y) = x + y;
            consumer_2(x, 0) += producer_1(x, x);
            consumer_2(0, y) += producer_2(y, 9-y);

            // The wrapper functions give us two separate handles on
            // the producer, so we can schedule them differently.
            producer_1.compute_at(consumer_2, x);
            producer_2.compute_at(consumer_2, y);

            Buffer<int> halide_result = consumer_2.realize(10, 10);

            // See figures/lesson_09_compute_at_multiple_updates.mp4 for a visualization.

            // The equivalent C is:
            int c_result[10][10];
            // Pure step for the consumer
            for (int y = 0; y < 10; y++) {
                for (int x = 0; x < 10; x++) {
                    c_result[y][x] = x + y;
                }
            }
            // First update step for consumer
            for (int x = 0; x < 10; x++) {
                int producer_1_storage[1];
                producer_1_storage[0] = (x * x) / 10 + 8;
                c_result[0][x] += producer_1_storage[0];
            }
            // Second update step for consumer
            for (int y = 0; y < 10; y++) {
                int producer_2_storage[1];
                producer_2_storage[0] = (y * (9-y)) / 10 + 8;
                c_result[y][0] += producer_2_storage[0];
            }

            // Check the results match
            for (int y = 0; y < 10; y++) {
                for (int x = 0; x < 10; x++) {
                    if (halide_result(x, y) != c_result[y][x]) {
                        printf("halide_result(%d, %d) = %d instead of %d\n",
                               x, y, halide_result(x, y), c_result[y][x]);
                        return -1;
                    }
                }
            }
        }

        {
            // Case 5: Scheduling a producer under a reduction domain
            // variable of the consumer.

            // We are not just restricted to scheduling producers at
            // the loops over the pure variables of the consumer. If a
            // producer is only used within a loop over a reduction
            // domain (RDom) variable, we can also schedule the
            // producer there.

            Func producer, consumer;

            RDom r(0, 5);
            producer(x) = x % 8;
            consumer(x) = x + 10;
            consumer(x) += r + producer(x + r);

            producer.compute_at(consumer, r);

            Buffer<int> halide_result = consumer.realize(10);

            // See figures/lesson_09_compute_at_rvar.gif for a visualization.

            // The equivalent C is:
            int c_result[10];
            // Pure step for the consumer.
            for (int x = 0; x < 10; x++)  {
                c_result[x] = x + 10;
            }
            // Update step for the consumer.
            for (int x = 0; x < 10; x++) {
                // The loop over the reduction domain is always the inner loop.
                for (int r = 0; r < 5; r++) {
                    // We've schedule the storage and computation of
                    // the producer here. We just need a single value.
                    int producer_storage[1];
                    // Pure step of the producer.
                    producer_storage[0] = (x + r) % 8;

                    // Now use it in the update step of the consumer.
                    c_result[x] += r + producer_storage[0];
                }
            }

            // Check the results match
            for (int x = 0; x < 10; x++) {
                if (halide_result(x) != c_result[x]) {
                    printf("halide_result(%d) = %d instead of %d\n",
                           x, halide_result(x), c_result[x]);
                    return -1;
                }
            }


        }
    }

    // A real-world example of a reduction inside a producer-consumer chain.
    {
        // The default schedule for a reduction is a good one for
        // convolution-like operations. For example, the following
        // computes a 5x5 box-blur of our grayscale test image with a
        // clamp-to-edge boundary condition:

        // First add the boundary condition.
        Func clamped = BoundaryConditions::repeat_edge(input);

        // Define a 5x5 box that starts at (-2, -2)
        RDom r(-2, 5, -2, 5);

        // Compute the 5x5 sum around each pixel.
        Func local_sum;
        local_sum(x, y) = 0; // Compute the sum as a 32-bit integer
        local_sum(x, y) += clamped(x + r.x, y + r.y);

        // Divide the sum by 25 to make it an average
        Func blurry;
        blurry(x, y) = cast<uint8_t>(local_sum(x, y) / 25);

        Buffer<uint8_t> halide_result = blurry.realize(input.width(), input.height());

        // The default schedule will inline 'clamped' into the update
        // step of 'local_sum', because clamped only has a pure
        // definition, and so its default schedule is fully-inlined.
        // We will then compute local_sum per x coordinate of blurry,
        // because the default schedule for reductions is
        // compute-innermost. Here's the equivalent C:

        Buffer<uint8_t> c_result(input.width(), input.height());
        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                int local_sum[1];
                // Pure step of local_sum
                local_sum[0] = 0;
                // Update step of local_sum
                for (int r_y = -2; r_y <= 2; r_y++) {
                    for (int r_x = -2; r_x <= 2; r_x++) {
                        // The clamping has been inlined into the update step.
                        int clamped_x = std::min(std::max(x + r_x, 0), input.width()-1);
                        int clamped_y = std::min(std::max(y + r_y, 0), input.height()-1);
                        local_sum[0] += input(clamped_x, clamped_y);
                    }
                }
                // Pure step of blurry
                c_result(x, y) = (uint8_t)(local_sum[0] / 25);
            }
        }

        // Check the results match
        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                if (halide_result(x, y) != c_result(x, y)) {
                    printf("halide_result(%d, %d) = %d instead of %d\n",
                           x, y, halide_result(x, y), c_result(x, y));
                    return -1;
                }
            }
        }
    }

    // Reduction helpers.
    {
        // There are several reduction helper functions provided in
        // Halide.h, which compute small reductions and schedule them
        // innermost into their consumer. The most useful one is
        // "sum".
        Func f1;
        RDom r(0, 100);
        f1(x) = sum(r + x) * 7;

        // Sum creates a small anonymous Func to do the reduction. It's equivalent to:
        Func f2;
        Func anon;
        anon(x) = 0;
        anon(x) += r + x;
        f2(x) = anon(x) * 7;

        // So even though f1 references a reduction domain, it is a
        // pure function. The reduction domain has been swallowed to
        // define the inner anonymous reduction.

        Buffer<int> halide_result_1 = f1.realize(10);
        Buffer<int> halide_result_2 = f2.realize(10);

        // The equivalent C is:
        int c_result[10];
        for (int x = 0; x < 10; x++) {
            int anon[1];
            anon[0] = 0;
            for (int r = 0; r < 100; r++) {
                anon[0] += r + x;
            }
            c_result[x] = anon[0] * 7;
        }

        // Check they all match.
        for (int x = 0; x < 10; x++) {
            if (halide_result_1(x) != c_result[x]) {
                printf("halide_result_1(%d) = %d instead of %d\n",
                       x, halide_result_1(x), c_result[x]);
                return -1;
            }
            if (halide_result_2(x) != c_result[x]) {
                printf("halide_result_2(%d) = %d instead of %d\n",
                       x, halide_result_2(x), c_result[x]);
                return -1;
            }
        }
    }

    // A complex example that uses reduction helpers.
    {
        // Other reduction helpers include "product", "minimum",
        // "maximum", "argmin", and "argmax". Using argmin and argmax
        // requires understanding tuples, which come in a later
        // lesson. Let's use minimum and maximum to compute the local
        // spread of our grayscale image.

        // First, add a boundary condition to the input.
        Func clamped;
        Expr x_clamped = clamp(x, 0, input.width()-1);
        Expr y_clamped = clamp(y, 0, input.height()-1);
        clamped(x, y) = input(x_clamped, y_clamped);

        RDom box(-2, 5, -2, 5);
        // Compute the local maximum minus the local minimum:
        Func spread;
        spread(x, y) = (maximum(clamped(x + box.x, y + box.y)) -
                        minimum(clamped(x + box.x, y + box.y)));

        // Compute the result in strips of 32 scanlines
        Var yo, yi;
        spread.split(y, yo, yi, 32).parallel(yo);

        // Vectorize across x within the strips. This implicitly
        // vectorizes stuff that is computed within the loop over x in
        // spread, which includes our minimum and maximum helpers, so
        // they get vectorized too.
        spread.vectorize(x, 16);

        // We'll apply the boundary condition by padding each scanline
        // as we need it in a circular buffer (see lesson 08).
        clamped.store_at(spread, yo).compute_at(spread, yi);

        Buffer<uint8_t> halide_result = spread.realize(input.width(), input.height());

        // The C equivalent is almost too horrible to contemplate (and
        // took me a long time to debug). This time I want to time
        // both the Halide version and the C version, so I'll use sse
        // intrinsics for the vectorization, and openmp to do the
        // parallel for loop (you'll need to compile with -fopenmp or
        // similar to get correct timing).
        #ifdef __SSE2__

        // Don't include the time required to allocate the output buffer.
        Buffer<uint8_t> c_result(input.width(), input.height());

        #ifdef _OPENMP
        double t1 = current_time();
        #endif

        // Run this one hundred times so we can average the timing results.
        for (int iters = 0; iters < 100; iters++) {

            #pragma omp parallel for
            for (int yo = 0; yo < (input.height() + 31)/32; yo++) {
                int y_base = std::min(yo * 32, input.height() - 32);

                // Compute clamped in a circular buffer of size 8
                // (smallest power of two greater than 5). Each thread
                // needs its own allocation, so it must occur here.

                int clamped_width = input.width() + 4;
                uint8_t *clamped_storage = (uint8_t *)malloc(clamped_width * 8);

                for (int yi = 0; yi < 32; yi++) {
                    int y = y_base + yi;

                    uint8_t *output_row = &c_result(0, y);

                    // Compute clamped for this scanline, skipping rows
                    // already computed within this slice.
                    int min_y_clamped = (yi == 0) ? (y - 2) : (y + 2);
                    int max_y_clamped = (y + 2);
                    for (int cy = min_y_clamped; cy <= max_y_clamped; cy++) {
                        // Figure out which row of the circular buffer
                        // we're filling in using bitmasking:
                        uint8_t *clamped_row =
                            clamped_storage + (cy & 7) * clamped_width;

                        // Figure out which row of the input we're reading
                        // from by clamping the y coordinate:
                        int clamped_y = std::min(std::max(cy, 0), input.height()-1);
                        uint8_t *input_row = &input(0, clamped_y);

                        // Fill it in with the padding.
                        for (int x = -2; x < input.width() + 2; x++) {
                            int clamped_x = std::min(std::max(x, 0), input.width()-1);
                            *clamped_row++ = input_row[clamped_x];
                        }
                    }

                    // Now iterate over vectors of x for the pure step of the output.
                    for (int x_vec = 0; x_vec < (input.width() + 15)/16; x_vec++) {
                        int x_base = std::min(x_vec * 16, input.width() - 16);

                        // Allocate storage for the minimum and maximum
                        // helpers. One vector is enough.
                        __m128i minimum_storage, maximum_storage;

                        // The pure step for the maximum is a vector of zeros
                        maximum_storage = _mm_setzero_si128();

                        // The update step for maximum
                        for (int max_y = y - 2; max_y <= y + 2; max_y++) {
                            uint8_t *clamped_row =
                                clamped_storage + (max_y & 7) * clamped_width;
                            for (int max_x = x_base - 2; max_x <= x_base + 2; max_x++) {
                                __m128i v = _mm_loadu_si128(
                                    (__m128i const *)(clamped_row + max_x + 2));
                                maximum_storage = _mm_max_epu8(maximum_storage, v);
                            }
                        }

                        // The pure step for the minimum is a vector of
                        // ones. Create it by comparing something to
                        // itself.
                        minimum_storage = _mm_cmpeq_epi32(_mm_setzero_si128(),
                                                          _mm_setzero_si128());

                        // The update step for minimum.
                        for (int min_y = y - 2; min_y <= y + 2; min_y++) {
                            uint8_t *clamped_row =
                                clamped_storage + (min_y & 7) * clamped_width;
                            for (int min_x = x_base - 2; min_x <= x_base + 2; min_x++) {
                                __m128i v = _mm_loadu_si128(
                                    (__m128i const *)(clamped_row + min_x + 2));
                                minimum_storage = _mm_min_epu8(minimum_storage, v);
                            }
                        }

                        // Now compute the spread.
                        __m128i spread = _mm_sub_epi8(maximum_storage, minimum_storage);

                        // Store it.
                        _mm_storeu_si128((__m128i *)(output_row + x_base), spread);

                    }
                }

                free(clamped_storage);
            }
        }

        // Skip the timing comparison if we don't have openmp
        // enabled. Otherwise it's unfair to C.
        #ifdef _OPENMP
        double t2 = current_time();

        // Now run the Halide version again without the
        // jit-compilation overhead. Also run it one hundred times.
        for (int iters = 0; iters < 100; iters++) {
            spread.realize(halide_result);
        }

        double t3 = current_time();

        // Report the timings. On my machine they both take about 3ms
        // for the 4-megapixel input (fast!), which makes sense,
        // because they're using the same vectorization and
        // parallelization strategy. However I find the Halide easier
        // to read, write, debug, modify, and port.
        printf("Halide spread took %f ms. C equivalent took %f ms\n",
               (t3 - t2)/100, (t2 - t1)/100);

        #endif // _OPENMP

        // Check the results match:
        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                if (halide_result(x, y) != c_result(x, y)) {
                    printf("halide_result(%d, %d) = %d instead of %d\n",
                           x, y, halide_result(x, y), c_result(x, y));
                    return -1;
                }
            }
        }

        #endif // __SSE2__

    }

    printf("Success!\n");
    return 0;
}
