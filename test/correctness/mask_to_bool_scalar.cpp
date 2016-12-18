#include "Halide.h"

using namespace Halide;

int main() {
    Func f("f");
    Var x("x"), y("y");
    ImageParam i(Int(32), 2, "i");

    f(x, y) = select(x < 10 || x > 20 || y < 10 || y > 20, 0, i(x, y));

    //f.vectorize(x, 128);
    f.hexagon().vectorize(x, 128);

    Target t(Target::Android, Target::ARM, 64);
    t.set_feature(Target::HVX_128);
    f.compile_to_file("bool_to_mask_issue", f.infer_arguments(), "bool_to_mask_issue", t);

    return 0;
}
