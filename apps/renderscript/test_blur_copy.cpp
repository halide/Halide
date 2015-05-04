#include "Halide.h"

using namespace Halide;

void blur(std::string suffix, ImageParam input8, const int channels,
          bool vectorized) {
    Var x, y, c;
    Func input;
    input(x, y, c) = input8(clamp(x, input8.left(), input8.right()),
                            clamp(y, input8.top(), input8.bottom()), c);

    Func blur_x("blur_x");
    blur_x(x, y, c) = cast<uint8_t>(
        (cast<uint16_t>(input(x, y, c)) +
        input(x + 1, y, c) +
        input(x + 2, y, c)) / 3);

    Func result("result");
    result(x, y, c) = cast<uint8_t>(
        (cast<uint16_t>(blur_x(x, y, c)) +
        blur_x(x, y + 1, c) +
        blur_x(x, y + 2, c)) / 3);

    result.output_buffer()
        .set_stride(0, input8.stride(0))
        .set_stride(2, input8.stride(2))
        .set_bounds(2, 0, channels);

    result.bound(c, 0, channels);
    if (suffix == "_rs") {
        result.shader(x, y, c, DeviceAPI::Renderscript);
        if (vectorized) {
            result.vectorize(c);
        }
    } else {
        Var yi;
        result
            .reorder(c, x, y).unroll(c)
            .split(y, y, yi, 8)
            .parallel(y);
        blur_x.store_at(result, y)
            .compute_at(result, yi)
            .reorder(c, x, y).unroll(c);
        if (vectorized) {
            result.vectorize(x, 16);
            blur_x.vectorize(x, 16);
        }
    }

    std::vector<Argument> args;
    args.push_back(input8);
    std::string filename("generated_blur");
    result.compile_to_file(
        filename + (vectorized ? "_vectorized" : "") + suffix, args);
}

void copy(std::string suffix, ImageParam input8, const int channels,
          bool vectorized) {
    Var x, y, c;
    Func input;
    input(x, y, c) = input8(clamp(x, input8.left(), input8.right()),
                            clamp(y, input8.top(), input8.bottom()), c);

    Func result("result");
    result(x, y, c) = input(x, y, c);
    result.output_buffer()
        .set_stride(0, input8.stride(0))
        .set_stride(2, input8.stride(2))
        .set_bounds(2, 0, channels);

    result.bound(c, 0, channels);
    if (suffix == "_rs") {
        result.shader(x, y, c, DeviceAPI::Renderscript);
        if (vectorized) {
            result.vectorize(c);
        }
    } else {
        Var xc;
        result.reorder(c, x, y)
            .parallel(y)
            .unroll(c);
        if (vectorized) {
            result.vectorize(x, 16);
        }
    }

    std::vector<Argument> args;
    args.push_back(input8);
    std::string filename("generated_copy");
    result.compile_to_file(
        filename + (vectorized ? "_vectorized" : "") + suffix, args);
}

int main(int argc, char **argv) {
    const int channels = 4;

    ImageParam input_planar(UInt(8), 3, "input");
    input_planar.set_stride(0, 1).set_bounds(2, 0, channels);
    blur(argc > 1 ? argv[1] : "", input_planar, channels, false);
    copy(argc > 1 ? argv[1] : "", input_planar, channels, false);

    ImageParam input_interleaved(UInt(8), 3, "input");
    input_interleaved.set_stride(0, channels)
        .set_stride(2, 1)
        .set_bounds(2, 0, channels);
    blur(argc > 1 ? argv[1] : "", input_interleaved, channels, true);
    copy(argc > 1 ? argv[1] : "", input_interleaved, channels, true);

    std::cout << "Done!" << std::endl;
}
