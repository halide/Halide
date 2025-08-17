#include "Halide.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::ConciseCasts;
using ::testing::HasSubstr;

TEST(WarningTests, EmulatedFloat16) {
    testing::internal::CaptureStderr();

    Func f;
    Var x;

    f(x) = u8_sat(f16(x) / f16(2.5f));

    // Make sure target has no float16 native support
    Target t = get_host_target();
    for (auto &feature : {Target::F16C, Target::ARMFp16}) {
        t = t.without_feature(feature);
    }

    f.compile_jit(t);

    const std::string captured_stderr = testing::internal::GetCapturedStderr();
    EXPECT_THAT(captured_stderr, HasSubstr("Warning:"));
    EXPECT_THAT(captured_stderr, HasSubstr("(b)float16 type operation is emulated"));
}
