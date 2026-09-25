#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    // On some backends, this pipeline hits an unrelated, earlier restriction
    // (e.g. lack of 64-bit integer support) before reaching the mutex-lock
    // check that this test is meant to exercise.
    return error_test("atomics_gpu_mutex", {"The atomic update requires a mutex lock, which is not supported in ", "Metal does not support 64-bit integers", "D3D12Compute does not support atomic operations that require a mutex lock", "HLSL SM 5.1 does not support 64-bit integers", "Atomic updates are not supported inside Vulkan kernels"},
                      []() {
                          int img_size = 10000;

                          Func f;
                          Var x;
                          RDom r(0, img_size);

                          f(x) = Tuple(1, 0);
                          f(r) = Tuple(f(r)[1] + 1, f(r)[0] + 1);

                          f.compute_root();

                          RVar ro, ri;
                          f.update()
                              .atomic()
                              .split(r, ro, ri, 8)
                              .gpu_blocks(ro)
                              .gpu_threads(ri);

                          // hist's update will be lowered to mutex locks,
                          // and we don't allow GPU blocks on mutex locks since
                          // it leads to deadlocks.
                          // This should throw an error
                          Realization out = f.realize({img_size});
                          (void)out;
                      });
}
