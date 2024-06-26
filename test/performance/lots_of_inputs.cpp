#include "halide_benchmark.h"
#include <Halide.h>

using namespace Halide;
using namespace Halide::Tools;

Func make_pipeline(int size) {
    std::vector<ImageParam> inputs;
    Expr e = 0.0f;

    for (int i = 0; i < size; i++) {
        inputs.emplace_back(Float(32), 2);
        e += inputs.back()(_);
    }

    return lambda(e);
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    for (int size = 1; size <= 128; size *= 2) {
        Func f = make_pipeline(size);
        double t_f = benchmark(1, 1, [&]() {
            f.compile_jit();
        });

        printf("Total compile time with %d inputs = %f s \n", size, t_f);

        // We may or may not notice if the build bots start taking longer than 15 minutes on one test
        if (t_f > 15 * 60) {
            printf("Took too long\n");
            return 1;
        }
    }

    for (int size = 1; size <= 128; size *= 2) {
        Func f = make_pipeline(size);

        double t_f = benchmark(1, 1, [&]() {
            f.compile_to_module(f.infer_arguments());
        });

        printf("Lowering time with %d inputs = %f s \n", size, t_f);

        if (t_f > 15 * 60) {
            printf("Took too long\n");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
