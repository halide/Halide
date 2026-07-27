#include "Halide.h"
#include <stdio.h>
#include <stdlib.h>

using namespace Halide;

int main(int argc, char **argv) {
#ifdef _WIN32
    printf("[SKIP] Windows does not have a working setenv\n");
    return 0;
#else
    // The JIT requires bounds query, so no_bounds_query in HL_JIT_TARGET is an error.
    setenv("HL_JIT_TARGET", "host-no_bounds_query", 1);
    (void)get_jit_target_from_environment();

    printf("Success!\n");
    return 0;
#endif
}
