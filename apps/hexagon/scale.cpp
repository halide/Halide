#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {

    Target target = get_target_from_environment();

    Var x("x"), y("y"), c("c");

    Param<int> scale;
    // Takes an 8-bit input
    ImageParam input(UInt(8), 3);

    Func f("f");
    f(x, y, c) = input(x, y, c) + 1;

    Func g("g");
    g(x, y, c) = f(x, y, c) * scale;

    Func h("h");
    h(x, y, c) = cast<uint8_t>(g(x, y, c) - 1);

    f.compute_root();
    g.compute_root().hexagon(c);
    h.compute_root();

    h.compile_to_header("scale.h", {scale, input}, "scale");
    std::stringstream obj;
    obj << "scale-" << argv[1] << ".o";
    h.compile_to_object(obj.str(), {scale, input}, "scale", target);

    return 0;
}
