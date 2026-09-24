#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("rfactor_fused_var_and_rvar", "can't rfactor an Func that has fused a Var into an RVar: y, r$z", []() {
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
    });
}
