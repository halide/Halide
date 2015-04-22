#include "Halide.h"

using namespace Halide;

void Blur(std::string suffix, ImageParam input8, const int nChannels,
          bool vectorized) {
    Var x, y, c;
    Func input;
    input(x, y, c) = input8(clamp(x, input8.left(), input8.right()),
                            clamp(y, input8.top(), input8.bottom()), c);

    Func blur_x("blur_x");
    blur_x(x, y, c) = cast<uint8_t>(
        (input(x, y, c) + input(x + 1, y, c) + input(x + 2, y, c)) / 3);
    blur_x.output_buffer()
        .set_stride(0, input8.stride(0))
        .set_stride(2, input8.stride(2))
        .set_bounds(2, 0, nChannels);

    Func result("result");
    result(x, y, c) = cast<uint8_t>(
        (blur_x(x, y, c) + blur_x(x, y + 1, c) + blur_x(x, y + 2, c)) / 3);
    result.output_buffer()
        .set_stride(0, input8.stride(0))
        .set_stride(2, input8.stride(2))
        .set_bounds(2, 0, nChannels);

    result.bound(c, 0, nChannels);
    if (suffix == "_rs") {
        result.rs(x, y, c);
    } else {
        result.parallel(y);
    }
    if (vectorized) {
        result.vectorize(c);
    }

    std::vector<Argument> args;
    args.push_back(input8);
    std::string filename("generated_blur");
    result.compile_to_file(
        filename + (vectorized ? "_vectorized" : "") + suffix, args);
}

void Copy(std::string suffix, ImageParam input8, const int nChannels,
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
        .set_bounds(2, 0, nChannels);

    result.bound(c, 0, nChannels);
    if (suffix == "_rs") {
        result.rs(x, y, c);
    } else {
        result.parallel(y);
    }
    if (vectorized) {
        result.vectorize(c);
    }

    std::vector<Argument> args;
    args.push_back(input8);
    std::string filename("generated_copy");
    result.compile_to_file(
        filename + (vectorized ? "_vectorized" : "") + suffix, args);
}

int main(int argc, char **argv) {
    const int nChannels = 4;

    ImageParam inputPlanar(UInt(8), 3, "input");
    inputPlanar.set_stride(0, 1).set_bounds(2, 0, nChannels);
    Blur(argc > 1 ? argv[1] : "", inputPlanar, nChannels, false);
    Copy(argc > 1 ? argv[1] : "", inputPlanar, nChannels, false);

    ImageParam inputInterleaved(UInt(8), 3, "input");
    inputInterleaved.set_stride(0, nChannels)
        .set_stride(2, 1)
        .set_bounds(2, 0, nChannels);
    Blur(argc > 1 ? argv[1] : "", inputInterleaved, nChannels, true);
    Copy(argc > 1 ? argv[1] : "", inputInterleaved, nChannels, true);

    std::cout << "Done!" << std::endl;
}