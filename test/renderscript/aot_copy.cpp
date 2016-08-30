#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

void copy_interleaved(bool vectorize, int channels) {
    ImageParam input8(UInt(8), 3, "input");
    input8
        .dim(0).set_stride(channels)
        .dim(2).set_stride(1).set_bounds(0, channels);

    Image<uint8_t> in = Image<uint8_t>::make_interleaved(128, 128, channels);
    Image<uint8_t> out = Image<uint8_t>::make_interleaved(128, 128, channels);
    input8.set(in);

    Var x, y, c;
    Func result("result");
    result(x, y, c) = input8(x, y, c);

    result.output_buffer()
        .dim(0).set_stride(channels)
        .dim(2).set_stride(1).set_bounds(0, channels);

    result.bound(c, 0, channels);
    result.shader(x, y, c, DeviceAPI::Renderscript);
    if (vectorize) {
        result.vectorize(c);
    }

    std::vector<Argument> args;
    args.push_back(input8);

    result.compile_to_file("aot_copy", args);
}

int main(int argc, char **argv) {
    copy_interleaved(false, 4);
    copy_interleaved(false, 3);

    std::cout << "Done!" << std::endl;
    return 0;
}
