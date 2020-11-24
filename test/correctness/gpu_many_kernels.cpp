#include "Halide.h"
#include <algorithm>

#include "halide_benchmark.h"

using namespace Halide;

constexpr size_t kNumKernels = 70;

int main(int argc, char **argv) {
    Var x, y, xi, yi;
    Func adders[kNumKernels];
    ImageParam input(Int(32), 2);

    Target target = get_jit_target_from_environment();
    int i = 1;
    for (Func &f : adders) {
        f(x, y) = input(x, y) + i;
        if (target.has_gpu_feature()) {
            f.compute_root().gpu_tile(x, y, xi, yi, 16, 16);
        } else {
            f.compute_root().vectorize(x, target.natural_vector_size<int32_t>());
        }
        i += 1;
    }

    auto start = Halide::Tools::benchmark_now();

    Buffer<int32_t> buf_a_store(32, 32);
    Buffer<int32_t> buf_b_store(32, 32);
    Buffer<int32_t> *buf_in = &buf_a_store;
    Buffer<int32_t> *buf_out = &buf_b_store;
    buf_in->fill(0);
    for (Func &f : adders) {
        input.set(*buf_in);
        f.realize(*buf_out);
        std::swap(buf_in, buf_out);
    }
    buf_in->copy_to_host();

    auto end = Halide::Tools::benchmark_now();
    double initial_runtime = Halide::Tools::benchmark_duration_seconds(start, end);

    buf_in->for_each_value([](int32_t x) { assert(x == (kNumKernels * (kNumKernels + 1)) / 2); });

    start = Halide::Tools::benchmark_now();

    buf_in->fill(0);
    for (Func &f : adders) {
        input.set(*buf_in);
        f.realize(*buf_out);
        std::swap(buf_in, buf_out);
    }
    buf_in->copy_to_host();

    end = Halide::Tools::benchmark_now();
    double precompiled_runtime = Halide::Tools::benchmark_duration_seconds(start, end);

    buf_in->for_each_value([](int32_t x) { assert(x == (kNumKernels * (kNumKernels + 1)) / 2); });

    buf_a_store.device_free();
    buf_b_store.device_free();
    const halide_device_interface_t *device = get_device_interface_for_device_api(DeviceAPI::Default_GPU, target);
    if (device != nullptr) {
        device->device_release(nullptr, device);
    }

    start = Halide::Tools::benchmark_now();

    buf_in->fill(0);
    for (Func &f : adders) {
        input.set(*buf_in);
        f.realize(*buf_out);
        std::swap(buf_in, buf_out);
    }
    buf_in->copy_to_host();

    end = Halide::Tools::benchmark_now();
    double second_runtime = Halide::Tools::benchmark_duration_seconds(start, end);

    buf_in->for_each_value([](int32_t x) { assert(x == (kNumKernels * (kNumKernels + 1)) / 2); });

    printf("Initial runtime %f, precompiled runtime %f, second runtime %f.\n", initial_runtime, precompiled_runtime, second_runtime);

    printf("Success!\n");
    return 0;
}    
