#include "halide_benchmark.h"
#include <Halide.h>

using namespace Halide;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    Var x("x"), y("y");

    for (int size = 1; size <= 128; size *= 2) {
        std::vector<ImageParam> inputs;
        Expr e = 0.0f;

        for (int i = 0; i < size; ++i) {
            inputs.emplace_back(Float(32), 2);
            e += inputs.back()(x, y);
        }

        double t_f = benchmark(1, 1, [&]() {
                Func f;
                f(x, y) = e;
                f.compile_jit();
            });

        printf("Compile time with %d inputs = %f s \n", size, t_f);
    }
    return 0;
}
