#include "Halide.h"

#include <cstdio>

// A GPU kernel's input may be a crop of another device buffer at any offset,
// so a dense vector load from it at an index that is a multiple of the
// vector's width may not be aligned to the vector's width in memory. Check
// that such loads read the right values (rather than faulting on a
// misaligned address).

using namespace Halide;

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    const int n = 4096;
    ImageParam in(UInt(8), 1, "in");
    Var x("x"), xo("xo"), xi("xi"), xii("xii");
    Func f("f");
    f(x) = in(x) + 1;
    in.dim(0).set_min(0);
    f.output_buffer().dim(0).set_min(0);
    f.split(x, xo, xi, 1024).split(xi, xi, xii, 16).gpu_blocks(xo).gpu_threads(xi).vectorize(xii);

    const halide_device_interface_t *device = get_device_interface_for_device_api(DeviceAPI::Default_GPU, t);
    for (int offset : {1, 3, 8}) {
        Runtime::Buffer<uint8_t> whole(n + 16);
        whole.for_each_element([&](int i) { whole(i) = (uint8_t)(i * 7 + 1); });
        whole.set_host_dirty();
        whole.copy_to_device(device);
        // A crop of the allocation, starting `offset` bytes into it (on the
        // device too, as halide_cuda_device_crop would make it).
        halide_buffer_t crop = *whole.raw_buffer();
        halide_dimension_t dim = crop.dim[0];
        dim.min = 0;
        dim.extent = n;
        crop.dim = &dim;
        crop.host += offset;
        crop.device += offset;
        in.set(Buffer<uint8_t>(crop));
        Buffer<uint8_t> out = f.realize({n}, t);
        out.copy_to_host();
        for (int i = 0; i < n; i++) {
            const uint8_t expected = (uint8_t)((i + offset) * 7 + 2);
            if (out(i) != expected) {
                printf("At offset %d: out(%d) = %d instead of %d\n", offset, i, out(i), expected);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
