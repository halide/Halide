#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (t.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    if (!(t.has_feature(Target::CUDA) ||
          t.has_feature(Target::Metal))) {
        printf("[SKIP] No GPU target enabled supporting half-precision.\n");
        return 0;
    }

    std::vector<Target::Feature> cuda_capabilities{Target::CUDACapability30, Target::CUDACapability32, Target::CUDACapability35, Target::CUDACapability50, Target::CUDACapability61};
    if (t.has_feature(Target::CUDA) && !(t.features_any_of(cuda_capabilities))) {
        printf("[SKIP] Need CUDA Capability 30 or greater.\n");
        return 0;
    }

    std::cout << t.to_string() << "\n";

    // Test three variants, in increasing order of speed.
    // 1) Store as float, math as float
    // 2) Store as half, math as float
    // 3) Store as half, math as half

    const int size = 1024 * 1024 * 10;
    const int step = 1024;

    Buffer<float> f32_in(size + step);
    Buffer<float16_t> f16_in(size + step);

    f32_in.fill(2.0f);
    f16_in.fill(float16_t(2.0f));

    Buffer<float> f32_out(size);
    Buffer<float16_t> f16_out(size);

    Func f1, f2, f3;
    Var x;

    f1(x) = f32_in(x) * f32_in(x + step);
    f2(x) = f16_in(x) * f16_in(x + step);

    Var xi;
    f1.bound(x, 0, size).vectorize(x, 2).gpu_tile(x, xi, 32);
    f2.bound(x, 0, size).vectorize(x, 4).gpu_tile(x, xi, 32);

    f1.compile_jit(t);
    f2.compile_jit(t);

    double t1 = Tools::benchmark([&]() {f1.realize(f32_out); f32_out.device_sync(); });
    double t2 = Tools::benchmark([&]() {f2.realize(f16_out); f16_out.device_sync(); });

    printf("Times: %f %f\n", t1, t2);
    printf("Speed-up from using half type: %f x\n", t1 / t2);

    if (t2 > t1) {
        printf("Half should not have been slower than float\n");
        return 1;
    }

    f32_out.copy_to_host();
    f16_out.copy_to_host();
    for (int i = 0; i < size; i++) {
        if (f32_out(i) != 4.0f) {
            printf("f32_out(%d) = %f instead of 4\n", i, f32_out(i));
            return 1;
        }
        if (f16_out(i) != float16_t(4.0f)) {
            printf("f16_out(%d) = %f instead of 4\n", i, (float)f16_out(i));
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
