#include "Halide.h"
using namespace Halide;

int main() {
    Buffer< float > in( 5 );

    Func conv("conv"), back("back");
    Var x("x");

    conv(x) = cast(Float(16), in(x));
    back(x) = cast(Float(32), conv(x));

    Buffer< float > out( 5 );

    conv.compute_root();
    back.realize(out);
}
