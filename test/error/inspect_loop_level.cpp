#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestInspectLoopLevel() {
    LoopLevel root = LoopLevel::root();

    printf("LoopLevel is %s\n", root.to_string().c_str());  // should fail
}
}  // namespace

TEST(ErrorTests, InspectLoopLevel) {
    EXPECT_COMPILE_ERROR(TestInspectLoopLevel, MatchesPattern(R"(Cannot inspect an unlocked LoopLevel: \.__root)"));
}
