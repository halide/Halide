#include "Halide.h"
#include "halide_test_dirs.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().os != Target::OSX) {
        printf("[SKIP] error/metal_threads_too_large ignored for non-OSX targets\n");
        _halide_user_assert(0);
    }

    ImageParam im(UInt(16), 2, "input");
    Func f("f");
    Var x("x"), y("y");

    f(x, y) = im(x, y) + 42;
    f.gpu_blocks(y).gpu_threads(x, DeviceAPI::Metal);

    // 65536 is larger enough than `maxTotalThreadsPerThreadgroup`
    Buffer<uint16_t> input = lambda(x, y, cast<uint16_t>(x + y)).realize({65536, 1});
    input.set_host_dirty();
    im.set(input);

    Buffer<uint16_t> output(input.width(), input.height());
    Target mac_target{"host-metal-debug"};
    f.realize(output, mac_target);
    output.copy_to_host();

    for (int32_t i = 0; i < output.width(); i++) {
        for (int32_t j = 0; j < output.height(); j++) {
            if (output(i, j) != uint16_t(i + j + 42)) {
                std::cerr << "Expected " << (i + j + 42) << " at (" << i << ", " << j << ") got " << output(i, j) << "\n";
                assert(false);
            }
        }
    }

    printf("Success!\n");
    return 0;
}
