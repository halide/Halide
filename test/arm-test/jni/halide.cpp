#include "Halide.h"

using namespace Halide;

void generate_8bit() {
    ImageParam param(UInt(8), 1, "input");
    Var x;
    Func input;
    input(x) = param(clamp(x, param.left(), param.right()));

    Func result("result");
    result(x) = cast<uint8_t>(cast<uint8_t>(input(x) + input(x)) / 2);

    result.vectorize(x, 16);

    std::vector<Argument> args;
    args.push_back(param);
    result.compile_to_file("halide_generated_8bit", args);
}

void generate_16bit() {
    ImageParam param(UInt(16), 1, "input");
    Var x;
    Func input;
    input(x) = param(clamp(x, param.left(), param.right()));

    Func result("result");
    result(x) = cast<uint16_t>(cast<uint16_t>(input(x) + input(x)) / 2);

    result.vectorize(x, 8);

    std::vector<Argument> args;
    args.push_back(param);
    result.compile_to_file("halide_generated_16bit", args);
}

void generate_32bit() {
    ImageParam param(UInt(32), 1, "input");
    Var x;
    Func input;
    input(x) = param(clamp(x, param.left(), param.right()));

    Func result("result");
    result(x) = cast<uint32_t>(cast<uint32_t>(input(x) + input(x)) / 2);

    result.vectorize(x, 4);

    std::vector<Argument> args;
    args.push_back(param);
    result.compile_to_file("halide_generated_32bit", args);
}

int main(int argc, char **argv) {
    generate_8bit();
    generate_16bit();
    generate_32bit();
    std::cout << "Done!" << std::endl;
}
