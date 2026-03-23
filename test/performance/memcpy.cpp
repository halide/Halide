#include "Halide.h"
#include "halide_benchmark.h"
#include "halide_test_dirs.h"

#include <chrono>
#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    ImageParam src(UInt(8), 1);
    Func dst;
    Var x;
    dst(x) = src(x);

    dst.vectorize(x, 32, TailStrategy::GuardWithIf);

    dst.compile_to_assembly(Internal::get_test_tmp_dir() + "halide_memcpy.s", {src}, "halide_memcpy");
    dst.compile_jit();

    const int32_t buffer_size = 12345678;

    Buffer<uint8_t> input(buffer_size);
    Buffer<uint8_t> output(buffer_size);

    src.set(input);

    double t1 = benchmark([&]() {
        dst.realize(output);
    });

    double t2 = benchmark([&]() {
        memcpy(output.data(), input.data(), input.width());
    });

    printf("system memcpy: %.3e byte/s\n", buffer_size / t2);
    printf("halide memcpy: %.3e byte/s\n", buffer_size / t1);

    // memcpy will win by a little bit for large inputs because it uses streaming stores
    if (t1 > t2 * 3) {
        printf("Halide memcpy is slower than it should be.\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
