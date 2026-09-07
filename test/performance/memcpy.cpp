#include "Halide.h"
#include "halide_benchmark.h"
#include "halide_test_dirs.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>

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

    // LLVM requires vector-width alignment to select the native streaming
    // instructions on some targets. Halide::Buffer allocations satisfy this.
    src.set_host_alignment(32);
    dst.output_buffer().set_host_alignment(32);

    dst.vectorize(x, 32, TailStrategy::GuardWithIf)
        .stream_loads({src})
        .stream_stores();

    dst.compile_to_assembly(Internal::get_test_tmp_dir() + "halide_memcpy.s", {src}, "halide_memcpy");
    dst.compile_jit();

    const int32_t buffer_size = 12345678;

    Buffer<uint8_t> input(buffer_size);
    Buffer<uint8_t> output(buffer_size);

    src.set(input);

    auto halide_copy = [&]() {
        dst.realize(output);
    };
    auto system_copy = [&]() {
        memcpy(output.data(), input.data(), input.width());
    };

    // Warm up, then alternate the benchmarks to reduce the effect of thermal
    // and frequency drift.
    halide_copy();
    system_copy();

    double t_halide = std::numeric_limits<double>::infinity();
    double t_memcpy = std::numeric_limits<double>::infinity();
    for (int i = 0; i < 5; i++) {
        t_halide = std::min(t_halide, (double)benchmark(halide_copy));
        t_memcpy = std::min(t_memcpy, (double)benchmark(system_copy));
    }

    printf("system memcpy: %.2f GB/s\n", (buffer_size / t_memcpy) / 1e9);
    printf("halide memcpy: %.2f GB/s\n", (buffer_size / t_halide) / 1e9);

    // Streaming directives should bring the generated copy close to the
    // platform memcpy implementation for a large contiguous transfer.
    if (t_halide > t_memcpy * 1.2) {
        printf("Halide memcpy is slower than it should be.\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
