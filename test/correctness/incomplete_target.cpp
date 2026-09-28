#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("incomplete_target", "Did not understand Halide target debug\n"
                                           "Expected format is arch-bits-os-processor-feature1-feature2-...\n",
                      []() {
                          Target t("debug");
                          (void)t;
                      });
}
