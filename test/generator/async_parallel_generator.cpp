#include "Halide.h"

namespace Ext {
HalideExtern_5(int, sleeper, int, int, int, int, int)
}

#if HALIDE_PREFER_G2_GENERATORS

namespace {

using namespace Halide;

Func AsyncParallel() {
    Func consumer_2{"consumer_2"};
    Func producer_1{"producer_1"};
    Func consumer_1{"consumer_1"};
    Func producer_2{"producer_2"};
    Func output{"output"};

    Var x, y, z;

    producer_1(x, y, z) = x + y + Ext::sleeper(0, x, y, z, z);
    consumer_1(x, y, z) = Ext::sleeper(1, x, y, z, producer_1(x - 1, y, z)) + Ext::sleeper(2, x, y, z, producer_1(x + 1, y, z));
    producer_2(x, y, z) = Ext::sleeper(3, x, y, z, consumer_1(x, y - 1, z)) + Ext::sleeper(4, x, y, z, consumer_1(x, y + 1, z));
    consumer_2(x, y, z) = Ext::sleeper(5, x, y, z, producer_2(x - 1, y, z)) + Ext::sleeper(6, x, y, z, producer_2(x + 1, y, z));
    output(x, y, z) = Ext::sleeper(7, x, y, z, consumer_2(x, y, z));

    consumer_2.compute_at(output, z);
    producer_2.store_at(consumer_2, y).compute_at(consumer_2, x).async();
    consumer_1.store_at(output, z).compute_at(consumer_2, y).async();
    producer_1.store_at(consumer_2, y).compute_at(consumer_1, x).async();
    output.parallel(z);

    return output;
}

}  // namespace

HALIDE_REGISTER_G2(
    AsyncParallel,   // actual C++ fn
    async_parallel,  // build-system name
    Output("output", Int(32), 3))

#else

class AsyncParallel : public Halide::Generator<AsyncParallel> {
public:
    // Define a pipeline that needs a mess of threads due to nested parallelism.

    Output<Func> output{"output", Int(32), 3};

    void generate() {
        Func consumer_2{"consumer_2"};
        Func producer_1{"producer_1"};
        Func consumer_1{"consumer_1"};
        Func producer_2{"producer_2"};

        Var x, y, z;

        producer_1(x, y, z) = x + y + Ext::sleeper(0, x, y, z, z);
        consumer_1(x, y, z) = Ext::sleeper(1, x, y, z, producer_1(x - 1, y, z)) + Ext::sleeper(2, x, y, z, producer_1(x + 1, y, z));
        producer_2(x, y, z) = Ext::sleeper(3, x, y, z, consumer_1(x, y - 1, z)) + Ext::sleeper(4, x, y, z, consumer_1(x, y + 1, z));
        consumer_2(x, y, z) = Ext::sleeper(5, x, y, z, producer_2(x - 1, y, z)) + Ext::sleeper(6, x, y, z, producer_2(x + 1, y, z));
        output(x, y, z) = Ext::sleeper(7, x, y, z, consumer_2(x, y, z));

        consumer_2.compute_at(output, z);
        producer_2.store_at(consumer_2, y).compute_at(consumer_2, x).async();
        consumer_1.store_at(output, z).compute_at(consumer_2, y).async();
        producer_1.store_at(consumer_2, y).compute_at(consumer_1, x).async();
        output.parallel(z);
    }
};

HALIDE_REGISTER_GENERATOR(AsyncParallel, async_parallel)

#endif
