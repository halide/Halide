#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("forward_on_undefined_buffer", "Undefined buffer calling const method raw_buffer", []() {
        const Buffer<> foo;
        foo.raw_buffer();
    });
}
