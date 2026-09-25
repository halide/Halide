#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().has_feature(Target::WebGPU)) {
        printf("[SKIP] WebGPU will (incorrectly) fail here because 8-bit types are currently emulated using atomics.\n");
        return 0;
    }

    if (get_jit_target_from_environment().has_feature(Target::D3D12Compute)) {
        printf("[SKIP] D3D12Compute supports 8/16-bit atomics, so no error is raised.\n");
        return 0;
    }

    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    // The exact wording of this error differs by GPU backend.
    return error_test("atomics_gpu_8_bit", {"8-bit or 16-bit atomics are not supported", "OpenCL only support 32 and 64 bit atomics", "Atomic updates are not supported inside Metal kernels", "Atomic updates are not supported inside Vulkan kernels"},
                      []() {
                          int img_size = 10000;
                          int hist_size = 7;

                          Func im, hist;
                          Var x;
                          RDom r(0, img_size);

                          im(x) = (x * x) % hist_size;

                          hist(x) = cast<uint8_t>(0);
                          hist(im(r)) += cast<uint8_t>(1);

                          hist.compute_root();

                          RVar ro, ri;
                          hist.update()
                              .atomic()
                              .split(r, ro, ri, 8)
                              .gpu_blocks(ro)
                              .gpu_threads(ri);

                          // GPU doesn't support 8/16-bit atomics
                          Realization out = hist.realize({hist_size});
                          (void)out;
                      });
}
