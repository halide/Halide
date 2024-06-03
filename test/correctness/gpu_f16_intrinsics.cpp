#include "Halide.h"

using namespace Halide;
int main(int argc, char *argv[]) {

    auto target = get_jit_target_from_environment();
    if (!target.has_feature(Target::Metal)) {
        printf("[SKIP] No metal target enabled.\n");
        return 0;
    }

    Func output, output_cpu;
    Var x, y;
    Expr val = cast(Float(16), cast(Float(16), x + y) + 1.f);
    Expr clamp_val = clamp(cast(Float(16), 0.1f) * val, cast(Float(16), 0), cast(Float(16), 1));

    output(x, y) = cast(Float(16), select(clamp_val > 1, cast<float>(abs(clamp_val)), cast<float>(fast_pow(clamp_val, cast(Float(16), 1.f / 2.2f)))));
    output_cpu(x, y) = cast(Float(16), select(clamp_val > 1, cast<float>(abs(clamp_val)), cast<float>(fast_pow(clamp_val, cast(Float(16), 1.f / 2.2f)))));

    Var xi, xo, yi, yo;
    output.gpu_tile(x, y, xo, yo, xi, yi, 8, 8);

    Buffer<float16_t> out = output.realize({64, 64});
    Buffer<float16_t> out2 = output_cpu.realize({64, 64});
    out.copy_to_host();

    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            if (fabs(float(out2(i, j)) - float(out(i, j))) > 0.01) {
                fprintf(stderr, "Failed: Incorrect value at %d,%d: %f vs %f\n", i, j, float(out(i, j)), float(out2(i, j)));
                return 1;
            }
        }
    }

    Func f, g, h;
    auto inf16 = float16_t::make_infinity();
    auto neginf16 = float16_t::make_negative_infinity();
    auto nan16 = float16_t::make_nan();

    f(x) = inf16;
    g(x) = neginf16;
    h(x) = nan16;

    f.gpu_tile(x, xo, xi, 8);
    g.gpu_tile(x, xo, xi, 8);
    h.gpu_tile(x, xo, xi, 8);

    Buffer<float16_t> fout = f.realize({8});
    Buffer<float16_t> gout = g.realize({8});
    Buffer<float16_t> hout = h.realize({8});
    fout.copy_to_host();
    gout.copy_to_host();
    hout.copy_to_host();

    for (int i = 0; i < 8; i++) {
        if (!fout(i).is_infinity()) {
            fprintf(stderr, "Failed: did not get infinity at %d (got: %u, expected: %u)\n", i, fout(i).to_bits(), float16_t::make_infinity().to_bits());
            return 1;
        }
        if (!(gout(i).is_infinity() && gout(i).is_negative())) {
            fprintf(stderr, "Failed: did not get negative infinity at %d (got: %u, expected: %u)\n", i, gout(i).to_bits(), float16_t::make_negative_infinity().to_bits());
            return 1;
        }
        if (!hout(i).is_nan()) {
            fprintf(stderr, "Failed: did not get nan at %d (got: %u, expected: %u)\n", i, hout(i).to_bits(), float16_t::make_nan().to_bits());
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
