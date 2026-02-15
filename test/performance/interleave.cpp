#include "Halide.h"
#include "halide_benchmark.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

struct Result {
    int type_size, factor;
    double bandwidth;
};

template<typename T>
Result test_interleave(int factor, const Target &t) {
    const int N = 8192;
    Buffer<T> in(N, factor), out(N * factor);

    for (int y = 0; y < factor; y++) {
        for (int x = 0; x < N; x++) {
            in(x, y) = (T)(x * factor + y);
        }
    }

    Func output;
    Var x, y;

    output(x) = in(x / factor, x % factor);

    Var xi, yi;
    output.unroll(x, factor, TailStrategy::RoundUp).vectorize(x, t.natural_vector_size<T>(), TailStrategy::RoundUp);
    output.output_buffer().dim(0).set_min(0);

    output.compile_jit();

    output.realize(out);

    double time = benchmark(20, 20, [&]() {
        output.realize(out);
    });

    for (int y = 0; y < factor; y++) {
        for (int x = 0; x < N; x++) {
            uint64_t actual = out(x * factor + y), correct = in(x, y);
            if (actual != correct) {
                std::cerr << "For factor " << factor
                          << "out(" << x << " * " << factor << " + " << y << ") = "
                          << actual << " instead of " << correct << "\n";
                exit(1);
            }
        }
    }

    // Uncomment to dump asm for inspection
    // output.compile_to_assembly("/dev/stdout",
    // std::vector<Argument>{in}, "interleave", t);

    return Result{(int)sizeof(T), factor, out.size_in_bytes() / (1.0e9 * time)};
}

template<typename T>
Result test_deinterleave(int factor, const Target &t) {
    const int N = 8192;
    Buffer<T> in(N * factor), out(N, factor);

    for (int x = 0; x < N; x++) {
        for (int y = 0; y < factor; y++) {
            in(x * factor + y) = (T)(x + y * N);
        }
    }

    Func output;
    Var x, y;

    output(x, y) = in(x * factor + y);

    Var xi, yi;
    output.reorder(y, x).bound(y, 0, factor).unroll(y).vectorize(x, t.natural_vector_size<T>(), TailStrategy::RoundUp);
    // output.output_buffer().dim(0).set_min(0);

    output.compile_jit();

    output.realize(out);

    double time = benchmark(20, 20, [&]() {
        output.realize(out);
    });

    for (int y = 0; y < factor; y++) {
        for (int x = 0; x < N; x++) {
            uint64_t actual = out(x, y), correct = in(x * factor + y);
            if (actual != correct) {
                std::cerr << "For factor " << factor
                          << "out(" << x << ", " << y << ") = "
                          << actual << " instead of " << correct << "\n";
                exit(1);
            }
        }
    }

    // Uncomment to dump asm for inspection
    output.compile_to_assembly("/dev/stdout",
    std::vector<Argument>{in}, "interleave", t);

    return Result{(int)sizeof(T), factor, out.size_in_bytes() / (1.0e9 * time)};
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    // Set the target features to use for dumping to assembly
    target.set_features({Target::NoRuntime, Target::NoAsserts, Target::NoBoundsQuery});

    std::cout << "\nbytes, interleave factor, interleave bandwidth (GB/s), deinterleave bandwidth (GB/s):\n";
#if 0
    for (int t : {1, 2, 4, 8}) {
        for (int f = 2; f < 16; f++) {
#else
     {
         {
            int t = 1, f = 4;
#endif
            Result r1, r2;
            switch (t) {
            case 1:
                r1 = test_interleave<uint8_t>(f, target);
                r2 = test_deinterleave<uint8_t>(f, target);
                break;
            case 2:
                r1 = test_interleave<uint16_t>(f, target);
                r2 = test_deinterleave<uint16_t>(f, target);
                break;
            case 4:
                r1 = test_interleave<uint32_t>(f, target);
                r2 = test_deinterleave<uint32_t>(f, target);
                break;
            case 8:
                r1 = test_interleave<uint64_t>(f, target);
                r2 = test_deinterleave<uint64_t>(f, target);
                break;
            default:
                break;
            }
            std::cout << r1.type_size << " "
                      << r1.factor << " "
                      << r1.bandwidth << " "
                      << r2.bandwidth << "\n";

        }
    }

    printf("Success!\n");
    return 0;
}
