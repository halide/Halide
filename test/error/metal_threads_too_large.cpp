#include "Halide.h"
#include "halide_test_dirs.h"

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam im(UInt(16), 2, "input");
    Func f("f");
    Var x("x"), y("y");

    f(x, y) = im(x, y) + 42;
    f.gpu_blocks(y).gpu_threads(x, DeviceAPI::Metal);

    // 65536 is larger enough than `maxTotalThreadsPerThreadgroup`
    Buffer<uint16_t> input = lambda(x, y, cast<uint16_t>(x + y)).realize(65536, 1);
    im.set(input);

    Buffer<uint16_t> output(input.width(), input.height());
    f.realize(output);
    output.copy_to_host();

    for (int32_t i = 0; i < output.width(); i++) {
        for (int32_t j = 0; j < output.height(); j++) {
            if (output(i, j) != uint16_t(i + j + 42)) {
                std::cerr << "Expected " << (x + y + 42) << " at (" << i << ", " << j << ") got " << output(i, j) << "\n";
                assert(false);
            }
        }
    }

    printf("Success!\n");
    return 0;
}
