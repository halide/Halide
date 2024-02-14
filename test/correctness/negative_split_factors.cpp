#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>

using namespace Halide;

bool error_occurred = false;
void my_error_handler(JITUserContext *user_context, const char *msg) {
    error_occurred = true;
}

int main(int argc, char **argv) {
    // Trying to realize a Pipeline with a negative or zero split factor should
    // error out cleanly, and not for example segfault because the output bounds
    // query returned a garbage buffer.

    Param<int> split;

    Func f;
    Var x;

    f(x) = x;
    f.parallel(x, split);

    split.set(-17);

    f.jit_handlers().custom_error = my_error_handler;

    f.realize({32});

    if (!error_occurred) {
        printf("There was supposed to be an error!\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
