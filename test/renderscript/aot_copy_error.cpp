#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

Image<uint8_t> make_interleaved_image(uint8_t *host, int W, int H,
                                      int nChannels) {
    buffer_t buf = { 0 };
    buf.host = host;
    buf.extent[0] = W;
    buf.stride[0] = nChannels;
    buf.extent[1] = H;
    buf.stride[1] = buf.stride[0] * buf.extent[0];
    buf.extent[2] = nChannels;
    buf.stride[2] = 1;
    buf.elem_size = 1;
    return Image<uint8_t>(&buf);
}

void copy_interleaved(bool vectorize, int channels) {
    ImageParam input8(UInt(8), 3, "input");
    input8.set_stride(0, channels)
        .set_stride(1, Halide::Expr())
        .set_stride(2, 1)
        .set_bounds(2, 0, channels);  // expecting interleaved image
    uint8_t in_buf[128 * 128 * channels];
    uint8_t out_buf[128 * 128 * channels];
    Image<uint8_t> in = make_interleaved_image(in_buf, 128, 128, channels);
    Image<uint8_t> out = make_interleaved_image(out_buf, 128, 128, channels);
    input8.set(in);

    Var x, y, c;
    Func result("result");
    result(x, y, c) = input8(x, y, c);
    result.output_buffer()
        .set_stride(0, channels)
        .set_stride(1, Halide::Expr())
        .set_stride(2, 1)
        .set_bounds(2, 0, channels);  // expecting interleaved image

    result.bound(c, 0, channels);
    result.shader(x, y, c, DeviceAPI::Renderscript);
    if (vectorize) result.vectorize(c);

    std::vector<Argument> args;
    args.push_back(input8);

    result.compile_to_file("aot_copy_error", args);
}

int main(int argc, char **argv) {
    const bool VECTORIZE = true;

    copy_interleaved(VECTORIZE, 3);

    std::cout << "Done!" << std::endl;
    return 0;
}