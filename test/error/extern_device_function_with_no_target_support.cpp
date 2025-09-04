#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestExternDeviceFunctionWithNoTargetSupport() {
    Func f{"f"};

    // Can't have a device extern stage if the target doesn't support it.
    f.define_extern("extern", {}, Halide::type_of<int32_t>(), 1,
                    NameMangling::Default,
                    Halide::DeviceAPI::OpenCL);
    f.compile_jit(Target{"host"});
}
}  // namespace

TEST(ErrorTests, ExternDeviceFunctionWithNoTargetSupport) {
    EXPECT_COMPILE_ERROR(
        TestExternDeviceFunctionWithNoTargetSupport,
        HasSubstr("extern definition that may leave the output with a dirty "
                  "<OpenCL> device allocation, but no compatible target "
                  "feature is enabled in target"));
}
