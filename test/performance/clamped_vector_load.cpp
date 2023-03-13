#include "Halide.h"
#include "halide_benchmark.h"
#include "halide_test_dirs.h"

#include <algorithm>
#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

Buffer<uint16_t> input;
Buffer<uint16_t> output;

#define MIN 1
#define MAX 1020

double test(Func f, bool test_correctness = true) {
    f.compile_to_assembly(Internal::get_test_tmp_dir() + f.name() + ".s", {input}, f.name());
    f.compile_jit();
    f.realize(output);

    if (test_correctness) {
        for (int y = 0; y < output.height(); y++) {
            for (int x = 0; x < output.width(); x++) {
                int ix1 = std::max(std::min(x, MAX), MIN);
                int ix2 = std::max(std::min(x + 1, MAX), MIN);
                uint16_t correct = input(ix1, y) * 3 + input(ix2, y);
                if (output(x, y) != correct) {
                    printf("output(%d, %d) = %d instead of %d\n",
                           x, y, output(x, y), correct);
                    exit(1);
                }
            }
        }
    }

    return benchmark([&]() { f.realize(output); });
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    // Try doing vector loads with a boundary condition in various
    // ways and compare the performance.

    input = Buffer<uint16_t>(1024 + 8, 320);

    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = rand() & 0xfff;
        }
    }

    output = Buffer<uint16_t>(1024, 320);

    Var x, y;

    double t_ref, t_clamped, t_scalar, t_pad;

    {
        // Do an unclamped load to get a reference number
        Func f;
        f(x, y) = input(x, y) * 3 + input(x + 1, y);

        f.vectorize(x, 8);

        t_ref = test(f, false);
    }

    {
        // Variant 1 - do the clamped vector load
        Func g;
        g(x, y) = input(clamp(x, MIN, MAX), y);

        Func f;
        f(x, y) = g(x, y) * 3 + g(x + 1, y);

        f.vectorize(x, 8);
        f.compile_to_lowered_stmt(Internal::get_test_tmp_dir() + "debug_clamped_vector_load.stmt", f.infer_arguments());

        t_clamped = test(f);
    }

    {
        // Variant 2 - do the load as a scalar op just before the vectorized stuff
        Func g;
        g(x, y) = input(clamp(x, MIN, MAX), y);

        Func f;
        f(x, y) = g(x, y) * 3 + g(x + 1, y);

        f.vectorize(x, 8);
        g.compute_at(f, x);

        t_scalar = test(f);
    }

    {
        // Variant 3 - pad each scanline using scalar code
        Func g;
        g(x, y) = input(clamp(x, MIN, MAX), y);

        Func f;
        f(x, y) = g(x, y) * 3 + g(x + 1, y);

        f.vectorize(x, 8);
        g.compute_at(f, y);

        t_pad = test(f);
    }

    // This constraint is pretty lax, because the op is so trivial
    // that the overhead of branching is large. For more complex ops,
    // the overhead should be smaller. We just make sure it's faster
    // than scalarizing or padding.
    if (t_clamped > t_scalar || t_clamped > t_pad) {
        printf("Clamped load timings suspicious:\n"
               "Unclamped: %f\n"
               "Clamped: %f\n"
               "Scalarize the load: %f\n"
               "Pad the input: %f\n",
               t_ref, t_clamped, t_scalar, t_pad);
        return 1;
    }

    printf("Success!\n");

    // Clean up our global images, otherwise you get destructor
    // order weirdness. The images hold onto the JIT-compiled module
    // that created them, and will delete it when they die. However,
    // it might not be possible to destroy the module cleanly after
    // main exits, because destroying the module touches globals
    // inside of llvm, and destructor order of globals is not
    // guaranteed.
    input = Buffer<uint16_t>();
    output = Buffer<uint16_t>();

    return 0;
}
