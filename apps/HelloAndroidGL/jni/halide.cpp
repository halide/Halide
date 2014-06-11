#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam input(UInt(8), 3, "input");
    input.set_min(2, 0);
    input.set_stride(0, 4);


    Var x, y, c;
    Func result;
    result(x, y, c) = select(c == 3 || c == 1, 255,
                             clamp(input(x, y, c) + 1, 0, 255));
    // dst.output_buffer().set_stride(0, 1);
    // dst.output_buffer().set_extent(2, 3);
    result.output_buffer().set_stride(0, 4);
    result.bound(c, 0, 4);
    result.glsl(x, y, c);

    std::vector<Argument> args;
    args.push_back(input);
    result.compile_to_file("halide", args);

    return 0;
}
