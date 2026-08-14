#include "Halide.h"

#include <cstdio>

using namespace Halide;

int main(int argc, char **argv) {
    // Round trip through an ordinary size.
    set_compiler_stack_size(1024 * 1024);
    if (get_compiler_stack_size() != 1024 * 1024) {
        std::cout << "FAILED: get_compiler_stack_size() didn't return the size just set\n";
        return 1;
    }

    // A small, deliberately page-unaligned size exercises
    // run_with_large_stack()'s page-rounding and two-sided-guard-page
    // arithmetic, rather than happening to land on an already-page-aligned
    // value like the 32 MiB default.
    set_compiler_stack_size(100001);
    if (get_compiler_stack_size() != 100001) {
        std::cout << "FAILED: get_compiler_stack_size() didn't return the size just set (2)\n";
        return 1;
    }

    bool ran = false;
    Internal::run_with_large_stack([&]() {
        ran = true;
    });
    if (!ran) {
        std::cout << "FAILED: run_with_large_stack() did not run the action\n";
        return 1;
    }

    printf("Success!\n");
    return 0;
}
