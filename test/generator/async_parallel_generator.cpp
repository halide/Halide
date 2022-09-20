#include "Halide.h"

// The convenience macros only go up to 5 arguments; we need 6 here,
// so we'll just extend it.
#define HalideExtern_6(rt, name, t0, t1, t2, t3, t4, t5)                                                                                                                \
    Halide::Expr name(const Halide::Expr &a0, const Halide::Expr &a1, const Halide::Expr &a2, const Halide::Expr &a3, const Halide::Expr &a4, const Halide::Expr &a5) { \
        _halide_check_arg_type(Halide::type_of<t0>(), name, a0, 1);                                                                                                     \
        _halide_check_arg_type(Halide::type_of<t1>(), name, a1, 2);                                                                                                     \
        _halide_check_arg_type(Halide::type_of<t2>(), name, a2, 3);                                                                                                     \
        _halide_check_arg_type(Halide::type_of<t3>(), name, a3, 4);                                                                                                     \
        _halide_check_arg_type(Halide::type_of<t4>(), name, a4, 5);                                                                                                     \
        _halide_check_arg_type(Halide::type_of<t5>(), name, a5, 6);                                                                                                     \
        return Halide::Internal::Call::make(Halide::type_of<rt>(), #name, {a0, a1, a2, a3, a4, a5}, Halide::Internal::Call::Extern);                                    \
    }

namespace Ext {
HalideExtern_6(int, sleeper, void *, int, int, int, int, int)
}

using namespace Halide;

class AsyncParallel : public Generator<AsyncParallel> {
public:
    // Define a pipeline that needs a mess of threads due to nested parallelism.

    Output<Func> output{"output", Int(32), 3};

    void generate() {
        Func consumer_2{"consumer_2"};
        Func producer_1{"producer_1"};
        Func consumer_1{"consumer_1"};
        Func producer_2{"producer_2"};

        Var x, y, z;

        Expr ucon = user_context_value();
        producer_1(x, y, z) = x + y + Ext::sleeper(ucon, 0, x, y, z, z);
        consumer_1(x, y, z) = Ext::sleeper(ucon, 1, x, y, z, producer_1(x - 1, y, z)) + Ext::sleeper(ucon, 2, x, y, z, producer_1(x + 1, y, z));
        producer_2(x, y, z) = Ext::sleeper(ucon, 3, x, y, z, consumer_1(x, y - 1, z)) + Ext::sleeper(ucon, 4, x, y, z, consumer_1(x, y + 1, z));
        consumer_2(x, y, z) = Ext::sleeper(ucon, 5, x, y, z, producer_2(x - 1, y, z)) + Ext::sleeper(ucon, 6, x, y, z, producer_2(x + 1, y, z));
        output(x, y, z) = Ext::sleeper(ucon, 7, x, y, z, consumer_2(x, y, z));

        consumer_2.compute_at(output, z);
        producer_2.store_at(consumer_2, y).compute_at(consumer_2, x).async();
        consumer_1.store_at(output, z).compute_at(consumer_2, y).async();
        producer_1.store_at(consumer_2, y).compute_at(consumer_1, x).async();
        output.parallel(z);
    }
};

HALIDE_REGISTER_GENERATOR(AsyncParallel, async_parallel)
