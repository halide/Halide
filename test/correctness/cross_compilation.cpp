#include "Halide.h"
#include "halide_test_dirs.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
const std::string targets[] = {
    "arm-32-android",
    "arm-32-ios",
    "arm-32-linux",
    "arm-32-noos-semihosting",
    "arm-64-android",
    "arm-64-android-hvx",
    "arm-64-ios",
    "arm-64-ios-armv8a",
    "arm-64-ios-armv81a",
    "arm-64-ios-armv82a",
    "arm-64-ios-armv83a",
    "arm-64-ios-armv84a",
    "arm-64-ios-armv85a",
    "arm-64-ios-armv86a",
    "arm-64-ios-armv87a",
    "arm-64-ios-armv88a",
    "arm-64-ios-armv89a",
    "arm-64-linux",
    "arm-64-noos-semihosting",
    "arm-64-windows",
    "arm-64-windows-d3d12compute",
    "wasm-32-wasmrt",
    "x86-32-linux",
    "x86-32-osx",
    "x86-32-windows",
    "x86-64-linux",
    "x86-64-osx",
    "x86-64-windows",
    "x86-64-windows-d3d12compute",
};
}

class CrossCompilationTargetTest : public testing::TestWithParam<std::string> {};
TEST_P(CrossCompilationTargetTest, OutputGenerationForTarget) {
    const std::string &t = GetParam();
    Target target(t);
    if (!target.supported()) {
        GTEST_SKIP() << "Target not supported: " << t;
    }

    const auto tmp = Internal::get_test_tmp_dir();

    std::map<OutputFileType, std::string> outputs = {
        {OutputFileType::assembly, ""},
        {OutputFileType::object, ""},
        {OutputFileType::static_library, ""},
        {OutputFileType::stmt, ""},
        {OutputFileType::stmt_html, ""},
    };
    const auto info = Internal::get_output_info(target);
    for (auto &[file_type, file_name] : outputs) {
        const auto &i = info.at(file_type);
        file_name = tmp + "test-" + i.name + "-" + t + i.extension;
        EXPECT_NO_THROW(Internal::ensure_no_file_exists(file_name)) << "Failed to ensure no file exists: " << file_name;
    }

    Param<float> p("myParam");
    Var x, y;

    Func f("f-" + t), g("g-" + t), h("h-" + t), j("j-" + t);
    f(x, y) = x + y;
    g(x, y) = cast<float>(f(x, y) + f(x + 1, y)) * p;
    h(x, y) = f(x, y) + g(x, y);
    j(x, y) = h(x, y) * 2;

    f.compute_root();
    g.compute_root();
    h.compute_root();

    // Ensure that HVX codegen has a submodule, since that is a unique path
    // that isn't exercised otherwise
    if (target.has_feature(Target::HVX)) {
        j.hexagon();
    }

    j.compile_to(outputs, j.infer_arguments(), "", target);

    for (auto &[_, file] : outputs) {
        EXPECT_TRUE(Internal::file_exists(file)) << "Output file missing for target " << t << ": " << file;
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllTargets,
    CrossCompilationTargetTest,
    ::testing::ValuesIn(targets));
