#include "Halide.h"
#include <iostream>

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_target_from_environment();
    if (target.arch != Target::WebAssembly) {
        std::cout << "Skipping WebAssembly test since WebAssembly is not specified in the target.\n";
        return 0;
    }
    target.set_feature(Target::NoRuntime);

    ImageParam in(UInt(8), 2);
    Var x("x"), y("y");
    Func bounded("bounded"), f("f");

    bounded(x, y) = cast<uint16_t>(BoundaryConditions::repeat_edge(in)(x, y));
    f(x, y) = (bounded(x - 1, y) + bounded(x, y) + bounded(x + 1, y)) / 3;

     f.compile_to_assembly("/tmp/webassembly.s", { in }, target);

    return 0;
}

