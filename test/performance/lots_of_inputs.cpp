#include "halide_benchmark.h"
#include <Halide.h>

using namespace Halide;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    // TODO(ataei): Increase the size once we have a reasonable compilation time. 
    // This takes several minutes for bigger sizes now.
    int size = 64;
    int buffer_size = 4096;
    Var x("x"), y("y");
    std::vector<ImageParam> inputs;
    std::vector<Buffer<float>> input_data;
    std::vector<Argument> args;
    std::vector<float> ones(buffer_size * buffer_size, 1.0f);
    Expr e = 0.0f;

    for (int i = 0; i < size; ++i) {
        inputs.emplace_back(Float(32), 2);
        input_data.emplace_back(ones.data(), buffer_size, buffer_size);
        inputs.back().set(input_data.back());
        args.emplace_back(inputs.back());
        e += inputs.back()(x, y);
    }

    Func f("halide_func");
    f(x, y) = e;
    Buffer<float> result = f.realize(buffer_size, buffer_size);
    for (int i = 0; i < buffer_size; ++i) {
        for (int j = 0; j < buffer_size; ++j) {
            if (result(i, j) != size) {
                printf("Incorrect results(%d, %d) = %f \n", i, j, result(i, j));
                return 1;
            }
        }
    }

    double t_f = benchmark([&]() {
        f.realize(result);
    });
    printf("Successful, Total time = %f ms \n", t_f);
    return 0;
}
