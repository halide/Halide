#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().has_feature(Target::WebGPU)) {
        printf("[SKIP] WebGPU will (incorrectly) fail here because 8-bit types are currently emulated using atomics.\n");
        return 0;
    }

    return error_test("atomics_gpu_8_bit", "8-bit or 16-bit atomics are not supported", []() {
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
