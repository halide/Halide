#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestAutoScheduleNoReorder() {
    Func f, g;
    Var x, y;
    RDom r(2, 18);

    f(x, y) = 1;
    f(r, y) = f(r - 2, y) + f(r - 1, y);

    g(x, y) = f(x + 10, y) + 2;

    // Provide estimates for pipeline output
    g.set_estimates({{0, 50}, {0, 50}});

    // Partially specify some schedules
    g.reorder(y, x);

    // Auto schedule the pipeline
    Target target = get_target_from_environment();
    Pipeline p(g);

    // This should throw an error since auto-scheduler does not currently
    // support partial schedules
    p.apply_autoscheduler(target, {"Mullapudi2016"});
}
}  // namespace

TEST(ErrorTests, AutoScheduleNoReorder) {
    GTEST_SKIP() << "TODO: load the Mullapudi2016 autoscheduler";
    EXPECT_COMPILE_ERROR(TestAutoScheduleNoReorder, HasSubstr("TODO"));
}
