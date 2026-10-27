#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("run_with_large_stack_throws", "Error from run_with_large_stack", []() {
        Halide::Internal::run_with_large_stack([]() {
            throw Halide::RuntimeError("Error from run_with_large_stack");
        });
    });
}
