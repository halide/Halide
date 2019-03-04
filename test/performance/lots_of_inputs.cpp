#include "halide_benchmark.h"
#include <Halide.h>

using namespace Halide;
using namespace Halide::Tools;

Func make_pipeline(int size) {
    // Make a pipeline that's pathological in several ways. The number
    // of inputs, the number of Funcs, and the maximum nesting depth
    // of for loops, and the maximum size of a RHS, will all grow
    // linearly with the size arg.
    std::vector<ImageParam> inputs;
    std::vector<Func> funcs;
    Expr e = 0.0f;
    Var x, y, yo;

    for (int i = 0; i < size; i++) {
        inputs.emplace_back(Float(32), 2);
        e += inputs.back()(x, y);
    }

    return lambda(x, y, e);
}

int main(int argc, char **argv) {
    for (int size = 1; size <= 128; size *= 2) {
        Func f = make_pipeline(size);
        double t_f = benchmark(1, 1, [&]() {
                f.compile_jit();
            });

        printf("Total compile time with %d inputs = %f s \n", size, t_f);

        // We may or may not notice if the build bots start taking longer than 15 minutes on one test
        if (t_f > 15 * 60) {
            printf("Took too long\n");
            return -1;
        }
    }

    for (int size = 1; size <= 4096; size *= 2) {
        Func f = make_pipeline(size);

        double t_f = benchmark(1, 1, [&]() {
                f.compile_to_module(f.infer_arguments());
            });

        printf("Lowering time with %d inputs = %f s \n", size, t_f);

        if (t_f > 15 * 60) {
            printf("Took too long\n");
            return -1;
        }
    }
    return 0;
}
