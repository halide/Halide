#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("extern_device_function_with_no_target_support", "Func f has an extern definition that may leave the output with a dirty "
                                                                       "OpenCL device allocation, but no compatible target feature is enabled in target ",
                      []() {
                          Func f{"f"};

                          // Can't have a device extern stage if the target doesn't support it.
                          f.define_extern("extern", {}, Halide::type_of<int32_t>(), 1,
                                          NameMangling::Default,
                                          Halide::DeviceAPI::OpenCL);
                          f.compile_jit(Target{"host"});
                      });
}
