#include "halide_benchmark.h"
#include <Halide.h>

using namespace Halide;
using namespace Halide::Tools;

Func make_pipeline(int size) {
    std::vector<ImageParam> inputs;
    Expr e = 0.0f;
    Var x, y;

    for (int i = 0; i < size; ++i) {
        inputs.emplace_back(Float(32), 2);
        e += inputs.back()(x, y);
    }
    Func f;
    f(x, y) = e;
    return f;
}

int main(int argc, char **argv) {
    /*
    for (int size = 1; size <= 128; size *= 2) {
        Func f = make_pipeline(size);
        double t_f = benchmark(1, 1, [&]() {
                f.compile_jit();
            });

        printf("Total compile time with %d inputs = %f s \n", size, t_f);
    }
    */

    for (int size = 4096; size <= 4096; size *= 2) {
        Func f = make_pipeline(size);

        double t_f = benchmark(1, 1, [&]() {
                f.compile_to_module(f.infer_arguments());
            });

        printf("Lowering time with %d inputs = %f s \n", size, t_f);
    }
    return 0;
}
