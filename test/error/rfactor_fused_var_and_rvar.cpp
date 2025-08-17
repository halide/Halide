#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestRfactorFusedVarAndRvar() {
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
}
}  // namespace

TEST(ErrorTests, RfactorFusedVarAndRvar) {
    EXPECT_COMPILE_ERROR(
        TestRfactorFusedVarAndRvar,
        MatchesPattern(R"(In schedule for f(\$\d+)?\.update\(0\): can't rfactor an )"
                       R"(Func that has fused a Var into an RVar: r\$z, y\n)"
                       R"(Vars: r\$x\.rxy\.yr x __outermost)"));
}
