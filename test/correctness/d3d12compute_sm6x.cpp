// Tests for HLSL Shader Model 6.x features via DXC compilation.
//
// Each sub-test is guarded by the minimum SM version it requires.
// Run with, e.g.:
//   HL_JIT_TARGET=x86-64-windows-d3d12compute_sm60   (64-bit types)
//   HL_JIT_TARGET=x86-64-windows-d3d12compute_sm62   (+ 16-bit types)
//   HL_JIT_TARGET=x86-64-windows-d3d12compute_sm66   (+ float atomics)

#include "Halide.h"
#include <cstdio>

using namespace Halide;

// ---------------------------------------------------------------------------
// SM 6.0: 64-bit integer and float arithmetic
//
// D3D12 has no 64-bit typed buffer format (DXGI_FORMAT_UNKNOWN for 64-bit),
// so RWBuffer<double> / RWBuffer<int64_t> are invalid resource types.
// Instead we compute in 64-bit and cast the result back to 32-bit for storage.
// This exercises 64-bit ALU without requiring unsupported buffer formats.
// ---------------------------------------------------------------------------

static int test_float64(const Target &t) {
    Var x("x"), xo("xo"), xi("xi");
    Func f("f");
    // Compute in float64, store as float32.
    f(x) = cast<float>(cast<double>(x) * 2 + 1);
    f.gpu_tile(x, xo, xi, 32);

    Buffer<float> out = f.realize({128}, t);
    for (int i = 0; i < 128; i++) {
        float expected = (float)((double)i * 2 + 1);
        if (out(i) != expected) {
            printf("  float64: mismatch at %d: got %g expected %g\n",
                   i, (double)out(i), (double)expected);
            return 1;
        }
    }
    printf("  float64 arithmetic: OK\n");
    return 0;
}

static int test_int64(const Target &t) {
    Var x("x"), xo("xo"), xi("xi");
    Func f("f");
    // Compute in int64, store as int32.
    f(x) = cast<int32_t>(cast<int64_t>(x) * cast<int64_t>(3) + cast<int64_t>(7));
    f.gpu_tile(x, xo, xi, 32);

    Buffer<int32_t> out = f.realize({128}, t);
    for (int i = 0; i < 128; i++) {
        int32_t expected = (int32_t)((int64_t)i * 3 + 7);
        if (out(i) != expected) {
            printf("  int64: mismatch at %d: got %d expected %d\n",
                   i, out(i), expected);
            return 1;
        }
    }
    printf("  int64 arithmetic: OK\n");
    return 0;
}

// ---------------------------------------------------------------------------
// SM 6.2: Native 16-bit types
// ---------------------------------------------------------------------------

static int test_float16(const Target &t) {
    Var x("x"), xo("xo"), xi("xi");
    Func f("f");
    // Add two float16 values -- exercises native float16 ALU in DXC / SM 6.2+.
    // Values are small integers so float16 precision is exact.
    f(x) = cast(Float(16), cast<int32_t>(x)) + cast(Float(16), cast<int32_t>(x));
    f.gpu_tile(x, xo, xi, 32);

    Buffer<float16_t> out = f.realize({64}, t);
    for (int i = 0; i < 64; i++) {
        float expected = (float)i * 2.0f;
        if ((float)out(i) != expected) {
            printf("  float16: mismatch at %d: got %g expected %g\n",
                   i, (float)out(i), expected);
            return 1;
        }
    }
    printf("  float16 arithmetic: OK\n");
    return 0;
}

static int test_int16(const Target &t) {
    Var x("x"), xo("xo"), xi("xi");
    Func f("f");
    f(x) = cast<int16_t>(x) * cast<int16_t>(3);
    f.gpu_tile(x, xo, xi, 32);

    Buffer<int16_t> out = f.realize({32}, t);
    for (int i = 0; i < 32; i++) {
        int16_t expected = (int16_t)(i * 3);
        if (out(i) != expected) {
            printf("  int16: mismatch at %d: got %d expected %d\n",
                   i, (int)out(i), (int)expected);
            return 1;
        }
    }
    printf("  int16 arithmetic: OK\n");
    return 0;
}

// ---------------------------------------------------------------------------
// SM 6.6: Float atomic add (InterlockedAddF32 / InterlockedAddF64)
// ---------------------------------------------------------------------------

static int test_float32_atomic(const Target &t) {
    // Parallel histogram over float bins -- forces float atomic add.
    const int img_size = 1000;
    const int hist_size = 7;

    Func im("im"), hist("hist");
    Var x("x");
    RDom r(0, img_size);

    im(x) = (x * x) % hist_size;

    hist(x) = 0.0f;
    hist(im(r)) += 1.0f;
    hist.compute_root();

    RVar ro("ro"), ri("ri");
    hist.update()
        .atomic(true /* float is not provably associative */)
        .split(r, ro, ri, 32)
        .gpu_blocks(ro, DeviceAPI::D3D12Compute)
        .gpu_threads(ri, DeviceAPI::D3D12Compute);

    // Compute expected result on the host.
    Buffer<float> correct(hist_size);
    correct.fill(0.0f);
    for (int i = 0; i < img_size; i++) {
        correct((i * i) % hist_size) += 1.0f;
    }

    // Run several times to shake out any race condition.
    for (int iter = 0; iter < 5; iter++) {
        Buffer<float> out = hist.realize({hist_size}, t);
        for (int i = 0; i < hist_size; i++) {
            if (out(i) != correct(i)) {
                printf("  float32 atomic: mismatch at bin %d (iter %d): got %g expected %g\n",
                       i, iter, out(i), correct(i));
                return 1;
            }
        }
    }
    printf("  float32 atomic add: OK\n");
    return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();

    if (!t.has_feature(Target::D3D12Compute)) {
        printf("[SKIP] D3D12Compute not in target.\n");
        printf("       Set HL_JIT_TARGET=x86-64-windows-d3d12compute_sm60 (or _sm62/_sm66)\n");
        return 0;
    }

    int sm = t.get_d3d12compute_capability_lower_bound();
    if (sm < 60) {
        printf("[SKIP] SM %d: no SM 6.x features to test.\n", sm);
        printf("       Set HL_JIT_TARGET=x86-64-windows-d3d12compute_sm60 (or _sm62/_sm66)\n");
        return 0;
    }

    printf("D3D12Compute SM %d\n\n", sm);
    int failures = 0;

    // SM 6.0: 64-bit types
    printf("[SM 6.0] 64-bit types\n");
    failures += test_float64(t);
    failures += test_int64(t);

    // SM 6.2: native 16-bit types
    if (sm >= 62) {
        printf("\n[SM 6.2] 16-bit types\n");
        failures += test_float16(t);
        failures += test_int16(t);
    } else {
        printf("\n[SM 6.2] Skipped (SM %d < 6.2 -- run with _sm62 for 16-bit type tests)\n", sm);
    }

    // SM 6.6: float atomics
    if (sm >= 66) {
        printf("\n[SM 6.6] Float atomics\n");
        failures += test_float32_atomic(t);
    } else {
        printf("\n[SM 6.6] Skipped (SM %d < 6.6 -- run with _sm66 for float atomic tests)\n", sm);
    }

    if (failures != 0) {
        printf("\n%d sub-test(s) failed.\n", failures);
        return 1;
    }

    printf("\nSuccess!\n");
    return 0;
}
