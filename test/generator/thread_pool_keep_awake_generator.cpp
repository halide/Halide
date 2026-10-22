#include "Halide.h"

namespace {

class ThreadPoolKeepAwake : public Halide::Generator<ThreadPoolKeepAwake> {
public:
    Input<int> offset{"offset"};
    Output<Buffer<int, 2>> output{"output"};

    void generate() {
        // A small pipeline with nested parallelism and an async producer, so
        // that it exercises do_par_for, do_parallel_tasks, and semaphores.
        Var x{"x"}, y{"y"}, xo{"xo"}, xi{"xi"};
        Func producer{"producer"}, consumer{"consumer"};

        producer(x, y) = x * y + offset;
        consumer(x, y) = producer(x - 1, y) + producer(x + 1, y);
        output(x, y) = consumer(x, y) + 1;

        output.parallel(y);
        consumer.compute_at(output, y).split(x, xo, xi, 4).parallel(xo);
        producer.compute_at(output, y).async();
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ThreadPoolKeepAwake, thread_pool_keep_awake)
