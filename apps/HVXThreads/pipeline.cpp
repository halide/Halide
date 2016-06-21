#include "Halide.h"

using namespace Halide;
int main(int argc, char **argv) {
    Target target = get_target_from_environment();

    std::cout << "Target: " << target.to_string() << "\n";

    Var x("x"), y("y"), c("c");

    // Takes an 8-bit input image.
    ImageParam in1(UInt(8), 2);
    ImageParam in2(UInt(8), 2);
    // Implement this as a separable blur in y followed by x.
    Func f ("f");
    f(x, y) = cast<uint8_t>(clamp(cast(UInt(16), in1(x, y)) + cast(UInt(16), in2(x, y)), 0, 255));
    // Schedule.
    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        const int vector_size = target.has_feature(Target::HVX_128) ? 128 : 64;
        //        f.hexagon().vectorize(x, vector_size).parallel(y, 16);
        f.hexagon().parallel(y, 16);
    } else {
        const int vector_size = target.natural_vector_size<uint8_t>();
        f.vectorize(x, vector_size).parallel(y, 16);
    }

    std::stringstream hdr;
    hdr << argv[2] << ".h";
    f.compile_to_header(hdr.str(), {in1, in2}, argv[2], target);
    std::stringstream obj;
    obj << argv[1] << ".o";
    f.compile_to_object(obj.str(), {in1, in2}, argv[2], target);

    return 0;
}
