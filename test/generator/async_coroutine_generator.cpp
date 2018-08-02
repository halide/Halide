#include "Halide.h"

using namespace Halide;

class AsyncCoroutine : public Generator<AsyncCoroutine> {
public:
    // Define a pipeline that needs a mess of threads due to nested parallelism.

    Output<Func> output {"output", Int(32), 3};

    void generate() {
        Func consumer_2 {"consumer_2"};
        Func producer_1 {"producer_1"};
        Func consumer_1 {"consumer_1"};
        Func producer_2 {"producer_2"};

        Var x, y, z;

        producer_1(x, y, z) = x + y + z;
        consumer_1(x, y, z) = producer_1(x-1, y, z) + producer_1(x+1, y, z);
        producer_2(x, y, z) = consumer_1(x, y-1, z) + consumer_1(x, y+1, z);
        consumer_2(x, y, z) = producer_2(x-1, y, z) + producer_2(x+1, y, z);
        output(x, y, z) = consumer_2(x, y, z);

        consumer_2.compute_at(output, z);
        producer_2.store_at(consumer_2, y).compute_at(consumer_2, x).async();
        consumer_1.store_at(output, z).compute_at(consumer_2, y).async();
        producer_1.store_at(consumer_2, y).compute_at(consumer_1, x).async();
        output.parallel(z);
    }
};

HALIDE_REGISTER_GENERATOR(AsyncCoroutine, async_coroutine)
