#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
int call_count = 0;

void inline_everything(const Pipeline &,
                       const Target &,
                       const AutoschedulerParams &,
                       AutoSchedulerResults *) {
    call_count++;
    // Inlining everything is really easy.
}
}  // namespace

TEST(CustomAutoSchedulerTest, InlineEverything) {
    constexpr const char *kSchedulerName = "inline_everything";

    // Add a very simple 'autoscheduler'
    Pipeline::add_autoscheduler(kSchedulerName, inline_everything);

    Func f;
    Var x;
    f(x) = 3;

    Func g;
    g(x) = 3;

    Target t("host");

    AutoschedulerParams autoscheduler_params(kSchedulerName);
    Pipeline(f).apply_autoscheduler(t, autoscheduler_params);
    Pipeline(g).apply_autoscheduler(t, autoscheduler_params);

    EXPECT_EQ(call_count, 2) << "Should have called the custom autoscheduler twice.";
}
