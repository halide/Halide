#include "Halide.h"

using namespace Halide;

template<typename T, typename Bits>
int test() {
    std::cout << "Testing " << type_of<T>() << "\n";
    Func f{"f"}, g{"g"};
    Param<T> b{"b"}, c{"c"};
    Var x{"x"};

    f(x) = fma(cast<T>(x), b, c);
    g(x) = strict_float(cast<T>(x) * b + c);

    Target t = get_jit_target_from_environment();
    if (std::is_same_v<T, float16_t> &&
        t.arch == Target::X86 &&
        t.os == Target::Windows &&
        t.bits == 32) {
        // Don't try to resolve float16 math library functions on win-32. In
        // theory LLVM is responsible for this, but at the time of writing
        // (12/16/2025) it doesn't seem to work.
        printf("Skipping float16 fma test on win-32\n");
        return 0;
    }

    if (std::is_same_v<T, float> &&
        t.has_gpu_feature() &&
        // Metal on x86 does not seem to respect strict float despite setting
        // the appropriate pragma.
        !(t.arch == Target::X86 && t.has_feature(Target::Metal)) &&
        // TODO: Vulkan does not respect strict_float yet:
        // https://github.com/halide/Halide/issues/7239
        !t.has_feature(Target::Vulkan)) {
        Var xo{"xo"}, xi{"xi"};
        f.gpu_tile(x, xo, xi, 32);
        g.gpu_tile(x, xo, xi, 32);
    } else {
        // Use a non-native vector width, to also test legalization
        f.vectorize(x, 5);
        g.vectorize(x, 5);
    }

    b.set((T)1.111111111);
    c.set((T)1.101010101);

    Buffer<T> with_fma = f.realize({1024});
    Buffer<T> without_fma = g.realize({1024});

    with_fma.copy_to_host();
    without_fma.copy_to_host();

    bool saw_error = false;
    for (int i = 0; i < with_fma.width(); i++) {

        Bits fma_bits = Internal::reinterpret_bits<Bits>(with_fma(i));
        Bits no_fma_bits = Internal::reinterpret_bits<Bits>(without_fma(i));

        if constexpr (sizeof(T) >= 4) {
            T correct_fma = std::fma((T)i, b.get(), c.get());

            if (with_fma(i) != correct_fma) {
                printf("fma result does not match std::fma:\n"
                       "  fma(%d, %10.10g, %10.10g) = %10.10g (0x%llx)\n"
                       "  but std::fma gives %10.10g (0x%llx)\n",
                       i,
                       (double)b.get(), (double)c.get(),
                       (double)with_fma(i),
                       (long long unsigned)fma_bits,
                       (double)correct_fma,
                       (long long unsigned)Internal::reinterpret_bits<Bits>(correct_fma));
                return -1;
            }
        }

        if (with_fma(i) == without_fma(i)) {
            continue;
        }

        saw_error = true;
        // For the specific positive numbers picked above, the rounding error is
        // at most 1 ULP. Note that it's possible to make much larger rounding
        // errors if you introduce some catastrophic cancellation.
        if (fma_bits + 1 != no_fma_bits &&
            fma_bits - 1 != no_fma_bits) {
            printf("Difference greater than 1 ULP: %10.10g (0x%llx) vs %10.10g (0x%llx)!\n",
                   (double)with_fma(i), (long long unsigned)fma_bits,
                   (double)without_fma(i), (long long unsigned)no_fma_bits);
            return -1;
        }
    }

    if (!saw_error) {
        printf("There should have occasionally been a 1 ULP difference between fma and non-fma results\n");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {

    if (test<double, uint64_t>() ||
        test<float, uint32_t>() ||
        test<float16_t, uint16_t>()) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
