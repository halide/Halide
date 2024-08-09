#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f{"f"};
    RDom r({{0, 5}, {0, 5}, {0, 5}}, "r");
    Var x{"x"}, y{"y"};
    f(x, y) = 0;
    f(x, y) += r.x + r.y + r.z;

    RVar rxy{"rxy"}, yrz{"yrz"}, yr{"yr"};
    Var z{"z"};

    // Error: In schedule for f.update(0), can't perform rfactor() after fusing r$z and y
    f.update()
        .fuse(r.x, r.y, rxy)
        .fuse(y, r.z, yrz)
        .fuse(rxy, yrz, yr)
        .rfactor(yr, z);

    f.print_loop_nest();

    printf("Success!\n");
    return 0;
}