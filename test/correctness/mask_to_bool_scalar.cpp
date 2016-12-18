#include "Halide.h"

using namespace Halide;

int main() {
    Func f("f");
    Var x("x"), y("y");
    ImageParam i(Int(32), 2, "i");

    f(x, y) = select(x < 10 || x > 20 || y < 10 || y > 20, 0, i(x, y));
    Target t(Target::Android, Target::ARM, 64);
    t.set_feature(Target::HVX_128);

    if (t.features_any_of({Target::HVX_64, Target::HVX_128})) {
        f.hexagon().vectorize(x, 128);
    } else {
        f.vectorize(x, 128);
    }

    f.compile_to_file("bool_to_mask_issue", f.infer_arguments(), "bool_to_mask_issue", t);

    std::cout << "Success!\n";
    return 0;
}
