#include "Halide.h"

using namespace Halide;

Expr u16(Expr x) { return cast<uint16_t>(x); }
Expr u8(Expr x) { return cast<uint8_t>(x); }

int main(int argc, char **argv) {

    Target target = get_target_from_environment();

    std::cout << "Target: " << target.to_string() << "\n";

    Var x("x"), y("y"), c("c");

    // Takes an 8-bit input
    ImageParam input(UInt(8), 3);

    input.set_min(0, 0).set_extent(0, (input.extent(0)/128)*128);
    input.set_stride(1, (input.stride(1)/128)*128);
    // Putting a boundary condition on x generates constant data,
    // which currently gets miscompiled. We only need the boundary
    // condition on y anyways.
    Func input_bounded = BoundaryConditions::repeat_edge(input, Expr(), Expr(), input.min(1), input.extent(1));

    int radius = 3;

    RDom ry(-radius, 2*radius + 1);

    Func f("f");
    f(x, y, c) = u8(sum(u16(input_bounded(x, y + ry, c)))/(2*radius + 1));
    Func g("g");
    g(x, y, c) = f(x, y, c);

    f.compute_root().hexagon(c).vectorize(x, 64);

    g.output_buffer().set_min(0, 0).set_extent(0, (g.output_buffer().extent(0)/128)*128);

    g.compile_to_header("scale.h", {input}, "scale");
    std::stringstream obj;
    obj << "scale-" << argv[1] << ".o";
    g.compile_to_object(obj.str(), {input}, "scale", target);

    return 0;
}
