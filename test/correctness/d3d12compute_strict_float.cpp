// Tests for strict_float (HLSL 'precise' qualifier) on the D3D12 compute backend.
//
// Run with:
//   HL_JIT_TARGET=x86-64-windows-d3d12compute
//   HL_JIT_TARGET=x86-64-windows-d3d12compute-strict_float
//
// The test adds StrictFloat to the target itself, so either form works.

#include "Halide.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace Halide;

namespace {
uint32_t f32_bits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}
}  // namespace

// ---------------------------------------------------------------------------
// Correctness: each strict_float op produces the expected value.
// ---------------------------------------------------------------------------

int test_strict_arithmetic(const Target &t) {
    Var x("x"), xo("xo"), xi("xi");

    // strict_float(a + b), strict_float(a - b), strict_float(a * b), strict_float(a / b)
    Func f("f");
    f(x) = Tuple(
        strict_float(cast<float>(x) + 0.5f),
        strict_float(cast<float>(x) - 0.5f),
        strict_float(cast<float>(x) * 2.0f),
        strict_float(cast<float>(x) / 4.0f));
    f.gpu_tile(x, xo, xi, 32);

    Realization out = f.realize({128}, t);
    Buffer<float> add_out = out[0];
    Buffer<float> sub_out = out[1];
    Buffer<float> mul_out = out[2];
    Buffer<float> div_out = out[3];

    for (int i = 0; i < 128; i++) {
        if (add_out(i) != (float)i + 0.5f) {
            printf("  strict_add: mismatch at %d: got %g expected %g\n",
                   i, (double)add_out(i), (double)((float)i + 0.5f));
            return 1;
        }
        if (sub_out(i) != (float)i - 0.5f) {
            printf("  strict_sub: mismatch at %d: got %g expected %g\n",
                   i, (double)sub_out(i), (double)((float)i - 0.5f));
            return 1;
        }
        if (mul_out(i) != (float)i * 2.0f) {
            printf("  strict_mul: mismatch at %d: got %g expected %g\n",
                   i, (double)mul_out(i), (double)((float)i * 2.0f));
            return 1;
        }
        if (div_out(i) != (float)i / 4.0f) {
            printf("  strict_div: mismatch at %d: got %g expected %g\n",
                   i, (double)div_out(i), (double)((float)i / 4.0f));
            return 1;
        }
    }
    printf("  strict_float arithmetic: OK\n");
    return 0;
}

static int test_strict_minmax(const Target &t) {
    Var x("x"), xo("xo"), xi("xi");

    Func f("f");
    f(x) = Tuple(
        strict_float(min(cast<float>(x), cast<float>(128 - x))),
        strict_float(max(cast<float>(x), cast<float>(128 - x))));
    f.gpu_tile(x, xo, xi, 32);

    Realization out = f.realize({128}, t);
    Buffer<float> min_out = out[0];
    Buffer<float> max_out = out[1];

    for (int i = 0; i < 128; i++) {
        float a = (float)i, b = (float)(128 - i);
        if (min_out(i) != std::min(a, b)) {
            printf("  strict_min: mismatch at %d: got %g expected %g\n",
                   i, (double)min_out(i), (double)std::min(a, b));
            return 1;
        }
        if (max_out(i) != std::max(a, b)) {
            printf("  strict_max: mismatch at %d: got %g expected %g\n",
                   i, (double)max_out(i), (double)std::max(a, b));
            return 1;
        }
    }
    printf("  strict_float min/max: OK\n");
    return 0;
}

// ---------------------------------------------------------------------------
// FMA separation: fma(x, b, c) vs strict_float(x * b + c).
//
// On hardware with FMA, the two paths may differ by 1 ULP because fma() uses
// a fused multiply-add while strict_float forces separate mul then add via the
// HLSL 'precise' qualifier.  We verify:
//   - no result differs by more than 1 ULP, and
//   - the test is noted as meaningful or trivial for informational purposes.
// ---------------------------------------------------------------------------
static int test_fma_separation(const Target &t) {
    Var x("x"), xo("xo"), xi("xi");
    Func f_fma("f_fma"), f_strict("f_strict");
    Param<float> b("b"), c("c");

    f_fma(x) = fma(cast<float>(x), b, c);
    f_strict(x) = strict_float(cast<float>(x) * b + c);
    f_fma.gpu_tile(x, xo, xi, 32);
    f_strict.gpu_tile(x, xo, xi, 32);

    b.set(1.111111111f);
    c.set(1.0101010101f);

    Buffer<float> out_fma = f_fma.realize({1024}, t);
    Buffer<float> out_strict = f_strict.realize({1024}, t);
    out_fma.copy_to_host();
    out_strict.copy_to_host();

    bool saw_difference = false;
    for (int i = 0; i < 1024; i++) {
        uint32_t bits_fma = ::f32_bits(out_fma(i));
        uint32_t bits_strict = ::f32_bits(out_strict(i));
        if (bits_fma == bits_strict) {
            continue;
        }
        saw_difference = true;
        if (bits_fma + 1 != bits_strict && bits_fma - 1 != bits_strict) {
            printf("  fma_separation: difference > 1 ULP at %d: "
                   "fma=0x%08x strict=0x%08x\n",
                   i, bits_fma, bits_strict);
            return 1;
        }
    }

    if (saw_difference) {
        printf("  fma_separation: fma and strict_float differ by 1 ULP (precise is working)\n");
    } else {
        printf("  fma_separation: fma and strict_float identical (GPU may not use FMA, OK)\n");
    }
    return 0;
}

// ---------------------------------------------------------------------------
// SM 6.0+: strict_float on float64
// ---------------------------------------------------------------------------
static int test_strict_float64(const Target &t) {
    int sm = t.get_d3d12compute_capability_lower_bound();
    if (sm < 60) {
        printf("  strict_float64: skipped (SM %d < 6.0)\n", sm);
        return 0;
    }

    Var x("x"), xo("xo"), xi("xi");
    Func f("f");
    // Compute in float64 with strict ops, store as float32.
    // Use integer literals (3, 1) so Halide folds them to FloatImm(Float(64))
    // rather than Cast(Float(64), IntImm) nodes, matching the pattern of the
    // working test_float64 in d3d12compute_sm6x.cpp.
    f(x) = cast<float>(strict_float(cast<double>(x) * 3 + 1));
    f.gpu_tile(x, xo, xi, 32);

    Buffer<float> out = f.realize({128}, t);
    for (int i = 0; i < 128; i++) {
        float expected = (float)((double)i * 3.0 + 1.0);
        if (out(i) != expected) {
            printf("  strict_float64: mismatch at %d: got %g expected %g\n",
                   i, (double)out(i), (double)expected);
            return 1;
        }
    }
    printf("  strict_float64: OK\n");
    return 0;
}

// ---------------------------------------------------------------------------
// SM 6.2+: strict_float on float16
// ---------------------------------------------------------------------------
static int test_strict_float16(const Target &t) {
    int sm = t.get_d3d12compute_capability_lower_bound();
    if (sm < 62) {
        printf("  strict_float16: skipped (SM %d < 6.2)\n", sm);
        return 0;
    }

    Var x("x"), xo("xo"), xi("xi");
    Func f("f");
    // Small integers are exactly representable in float16.
    f(x) = strict_float(cast(Float(16), cast<int32_t>(x)) +
                        cast(Float(16), cast<int32_t>(x)));
    f.gpu_tile(x, xo, xi, 32);

    Buffer<float16_t> out = f.realize({64}, t);
    for (int i = 0; i < 64; i++) {
        float expected = (float)i * 2.0f;
        if ((float)out(i) != expected) {
            printf("  strict_float16: mismatch at %d: got %g expected %g\n",
                   i, (float)out(i), expected);
            return 1;
        }
    }
    printf("  strict_float16: OK\n");
    return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();

    if (!t.has_feature(Target::D3D12Compute)) {
        printf("[SKIP] D3D12Compute not in target.\n");
        printf("       Set HL_JIT_TARGET=x86-64-windows-d3d12compute\n");
        return 0;
    }

    // Ensure StrictFloat is enabled for all sub-tests.
    t = t.with_feature(Target::StrictFloat);

    int sm = t.get_d3d12compute_capability_lower_bound();
    printf("D3D12Compute strict_float (SM %d)\n\n", sm);

    int failures = 0;
    failures += test_strict_arithmetic(t);
    failures += test_strict_minmax(t);
    failures += test_fma_separation(t);
    failures += test_strict_float64(t);
    failures += test_strict_float16(t);

    if (failures != 0) {
        printf("\n%d sub-test(s) failed.\n", failures);
        return 1;
    }

    printf("\nSuccess!\n");
    return 0;
}
