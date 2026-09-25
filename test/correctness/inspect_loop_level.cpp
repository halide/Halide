#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("inspect_loop_level", "Cannot inspect an unlocked LoopLevel: .__root\n", []() {
        LoopLevel root = LoopLevel::root();

        printf("LoopLevel is %s\n", root.to_string().c_str());  // should fail
    });
}
