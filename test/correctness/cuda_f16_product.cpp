#include "Halide.h"
#include <cstdio>
#include <regex>

using namespace Halide;

// A float16 product of float16s computed in float32 (whose product of two
// float16s is exact) is float16's correctly rounded multiply, which CUDA
// has natively. One operand is a converted integer (a weight's code times
// its scale), which llvm alone doesn't narrow back to float16.
int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA) || t.get_cuda_capability_lower_bound() < 61) {
        printf("[SKIP] Cuda (with compute capability 6.1) is not enabled in target: %s\n",
               t.to_string().c_str());
        return 0;
    }

    for (bool strict : {false, true}) {
        Buffer<int8_t> a(4096);
        Buffer<float16_t> b(4096);
        for (int i = 0; i < a.width(); i++) {
            // scales across float16's range, subnormals included
            a(i) = (int8_t)(i * 7919);
            b(i) = float16_t::make_from_bits((uint16_t)((i * 104729 + 12345) & 0xffff));
        }
        Func f;
        Var x, xo, xi;
        Expr p = cast<float16_t>(cast<float>(cast<float16_t>(a(x))) * cast<float>(b(x)));
        f(x) = strict ? strict_float(p) : p;
        f.gpu_tile(x, xo, xi, 64).vectorize(xi, 8);
        Target tt = strict ? t.with_feature(Target::StrictFloat) : t;
        Buffer<float16_t> out = f.realize({a.width()}, tt);
        out.copy_to_host();
        for (int i = 0; i < a.width(); i++) {
            float16_t correct = float16_t((float)a(i) * (float)b(i));
            bool both_nan = correct.is_nan() && out(i).is_nan();
            if (!both_nan && out(i).to_bits() != correct.to_bits()) {
                printf("strict = %d: %d * %g = %g (0x%04x) instead of %g (0x%04x)\n", strict,
                       a(i), (float)b(i), (float)out(i), out(i).to_bits(), (float)correct, correct.to_bits());
                return 1;
            }
        }
        Buffer<uint8_t> buf = f.compile_to_module(f.infer_arguments(), "f", tt).compile_to_buffer();
        std::regex re("mul(\\.rn)?\\.f16");
        if (!std::regex_search((const char *)buf.begin(), (const char *)buf.end(), re)) {
            printf("strict = %d: no float16 multiply in the PTX. Rerun with HL_DEBUG_CODEGEN=1 to debug\n", strict);
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}
