#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    Var x("x"), y("y");

    Target t = get_jit_target_from_environment();

    // Make sure we don't have predicated instructions available
    if ((t.arch != Target::X86 && t.arch != Target::ARM) ||
        t.has_feature(Target::AVX512) ||
        t.has_feature(Target::SVE)) {
        printf("[SKIP] This is a test for architectures without predication. "
               "Currently we only test x86 before AVX-512 and ARM without SVE\n");
        return 0;
    }

    const int N = t.natural_vector_size<uint8_t>() * 2;
    const int reps = 1024 * 128;

    Buffer<uint8_t> output_buf(N - 1, N - 1);
    Buffer<uint8_t> correct_output;

    std::map<TailStrategy, double> times;
    for (auto ts : {TailStrategy::GuardWithIf,
                    TailStrategy::RoundUp,
                    TailStrategy::ShiftInwardsAndBlend,
                    TailStrategy::RoundUpAndBlend}) {
        Func f, g;
        f(x, y) = cast<uint8_t>(x + y);
        RDom r(0, reps);
        f(x, y) = f(x, y) * 3 + cast<uint8_t>(0 * r);
        g(x, y) = f(x, y);

        f.compute_root()
            .update()
            .reorder(x, y, r)
            .vectorize(x, N / 2, ts);

        if (ts == TailStrategy::ShiftInwardsAndBlend) {
            // Hide the stall from a load that overlaps the previous store by
            // doing multiple scanlines at once. We expect the tail in y might
            // be large, so force partitioning of x even in the loop tail in y.
            f.update()
                .reorder(y, x)
                .unroll(y, 8, TailStrategy::GuardWithIf)
                .reorder(x, y)
                .partition(x, Partition::Always);
        }

        g.compile_jit();
        // Uncomment to see the assembly
        // g.compile_to_assembly("/dev/stdout", {}, "f", t);
        double t = benchmark([&]() {
            g.realize(output_buf);
        });

        // Check correctness
        if (ts == TailStrategy::GuardWithIf) {
            correct_output = output_buf.copy();
        } else {
            for (int y = 0; y < output_buf.height(); y++) {
                for (int x = 0; x < output_buf.width(); x++) {
                    if (output_buf(x, y) != correct_output(x, y)) {
                        printf("output_buf(%d, %d) = %d instead of %d\n",
                               x, y, output_buf(x, y), correct_output(x, y));
                    }
                }
            }
        }
        times[ts] = t;
    }

    for (auto p : times) {
        std::cout << p.first << " " << p.second << "\n";
    }

    if (times[TailStrategy::GuardWithIf] < times[TailStrategy::ShiftInwardsAndBlend]) {
        printf("ShiftInwardsAndBlend is slower than it should be\n");
        return 1;
    }

    if (times[TailStrategy::GuardWithIf] < times[TailStrategy::RoundUpAndBlend]) {
        printf("RoundUpAndBlend is slower than it should be\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
