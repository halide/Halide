#include "Halide.h"
#include "expect_user_error.h"
#include "halide_test_dirs.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().os != Target::MacOS) {
        printf("[SKIP] error/metal_threads_too_large ignored for non-OSX targets\n");
        return 0;
    }
    return error_test("metal_threads_too_large", "Metal: threadsX(65536) * threadsY(1) * threadsZ(1) (65536) must be <=", []() {
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
    });
}
