#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadHoistStorage() {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");

    f(x) = x;
    g(x) = f(x);
    h(x, y) = g(x);

    g.compute_at(h, y);

    // This makes no sense, because the compute_at level is higher than the hoist_storage level
    f.hoist_storage(h, y).compute_root();

    h.realize({10, 10});
}
}  // namespace

TEST(ErrorTests, BadHoistStorage) {
    EXPECT_COMPILE_ERROR(
        TestBadHoistStorage,
        MatchesPattern(
            R"(Func \"f(\$\d+)?\" is computed at the following invalid location:\n)"
            R"(  f(\$\d+)?\.compute_root\(\);\n)"
            R"(Legal locations for this function are:\n)"
            R"(  f(\$\d+)?\.compute_root\(\);\n)"
            R"(  f(\$\d+)?\.compute_at\(h(\$\d+)?, Var::outermost\(\)\);\n)"
            R"(  f(\$\d+)?\.compute_at\(h(\$\d+)?, y\);\n)"
            R"(  f(\$\d+)?\.compute_at\(g(\$\d+)?, Var::outermost\(\)\);\n)"
            R"(  f(\$\d+)?\.compute_at\(g(\$\d+)?, x\);\n)"
            R"(\"f(\$\d+)?\" is used in the following places:\n)"
            R"( for h(\$\d+)?\.s\d+\.y:\n)"
            R"(  for g(\$\d+)?\.s\d+\.x:\n )"
            R"(  g(\$\d+)? uses f(\$\d+)?\n)"
            R"(  \.\.\.)"));
}
