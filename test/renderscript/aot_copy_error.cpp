#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

Image<uint8_t> make_interleaved_image(uint8_t *host, int W, int H,
                                      int nChannels) {
    halide_buffer_t buf = { 0 };
    halide_dimension_t shape[] = {{0, W, nChannels},
                                  {0, H, nChannels*W},
                                  {0, nChannels, 1}};
    buf.host = host;
    buf.dim = shape;
    buf.dimensions = 3;
    buf.type = UInt(8);
    return Image<uint8_t>(&buf);
}

void copy_interleaved(bool vectorize, int channels) {
    ImageParam input8(UInt(8), 3, "input");
    input8
        .dim(0).set_stride(channels)
        .dim(2).set_stride(1).set_bounds(0, channels);
    uint8_t *in_buf = new uint8_t[128 * 128 * channels];
    uint8_t *out_buf = new uint8_t[128 * 128 * channels];
    Image<uint8_t> in = make_interleaved_image(in_buf, 128, 128, channels);
    Image<uint8_t> out = make_interleaved_image(out_buf, 128, 128, channels);
    input8.set(in);

    Var x, y, c;
    Func result("result");
    result(x, y, c) = input8(x, y, c);
    result.output_buffer()
        .dim(0).set_stride(channels)
        .dim(2).set_stride(1).set_bounds(0, channels);

    result.bound(c, 0, channels);
    result.shader(x, y, c, DeviceAPI::Renderscript);
    if (vectorize) result.vectorize(c);

    std::vector<Argument> args;
    args.push_back(input8);

    result.compile_to_file("aot_copy_error", args);
    delete[] in_buf;
    delete[] out_buf;
}

int main(int argc, char **argv) {
    const bool VECTORIZE = true;

    copy_interleaved(VECTORIZE, 3);

    std::cout << "Done!" << std::endl;
    return 0;
}
