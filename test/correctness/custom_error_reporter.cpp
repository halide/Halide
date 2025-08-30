#include "Halide.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Halide;

namespace {
volatile bool always_true = true;

struct my_custom_error final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct MyCustomErrorReporter final : CompileTimeErrorReporter {
    int errors_occurred{0};
    int warnings_occurred{0};
    int evaluated{0};

    void warning(const char *msg) override {
        printf("Custom warn: %s\n", msg);
        warnings_occurred++;
    }

    [[noreturn]] void error(const char *msg) override {
        printf("Custom err: %s\n", msg);
        errors_occurred++;
        throw my_custom_error(msg);
    }
};

int should_be_evaluated(MyCustomErrorReporter *reporter) {
    printf("Should be evaluated\n");
    reporter->evaluated++;
    return 421337;
}

class CustomErrorReporterTest : public ::testing::Test {
protected:
    MyCustomErrorReporter reporter;
    void SetUp() override {
        set_custom_compile_time_error_reporter(&reporter);
    }
    void TearDown() override {
        set_custom_compile_time_error_reporter(nullptr);
    }
};
}  // namespace

TEST_F(CustomErrorReporterTest, WarningAndError) {
    testing::internal::CaptureStdout();

    user_warning << "Here is a warning.";

    EXPECT_THAT([&] { user_assert(!always_true) << should_be_evaluated(&reporter); },
                testing::ThrowsMessage<my_custom_error>(testing::HasSubstr("421337")));

    EXPECT_THAT(
        testing::internal::GetCapturedStdout(),
        testing::AllOf(testing::HasSubstr("Here is a warning"),
                       testing::HasSubstr("Should be evaluated")));

    EXPECT_EQ(reporter.warnings_occurred, 1) << "Expected one warning to be issued";
    EXPECT_EQ(reporter.errors_occurred, 1) << "Expected one error to be issued";
    EXPECT_EQ(reporter.evaluated, 1) << "Expected `should_be_evaluated` to be evaluated";
}
