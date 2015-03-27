#include "Halide.h"
#include <string.h>
#include <stdio.h>

using namespace Halide;

bool error_occurred;
void halide_error(void *user_context, const char *msg) {
    printf("Custom error: %s\n", msg);
    error_occurred = true;
}

class Reporter : public Halide::CompileTimeErrorReporter {
    virtual void warning(const char *msg) {
        printf("Custom warning: %s\n", msg);
    }

    virtual void error(const char *msg) {
        printf("Custom error: %s\n", msg);
        throw "error";
    }
};

int main(int argc, char **argv) {
    Var x, y;
    Func f;
    f(x, y) = x + y;

    Reporter reporter;
    set_custom_compile_time_error_reporter(&reporter);

    f.set_error_handler(&halide_error);
    error_occurred = false;

    try {
        Image<uint8_t> out(10, 10);
        f.realize(out);
    } catch (const char *msg) {
        error_occurred = true;
    }

    if (error_occurred) {
        printf("Success!\n");
        return 0;
    } else {
        printf("There should have been a runtime error\n");
        return -1;
    }
}
