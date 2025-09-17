#include "Halide.h"
#include <cstdio>
#include <cstdlib>

using namespace Halide;

int evaluated = 0;

int should_be_evaluated() {
    printf("Should be evaluated\n");
    evaluated += 1;
    return 0;
}

int should_never_be_evaluated() {
    printf("Should never be evaluated\n");
    exit(1);
    return 0;
}

struct MyCustomErrorReporter final : CompileTimeErrorReporter {
    int errors_occurred{0};
    int warnings_occurred{0};

    void warning(const char *msg) override {
        const auto msg_safe = Internal::replace_all(msg, ":", "(colon)");
        printf("Custom warn: %s\n", msg_safe.c_str());
        warnings_occurred++;
    }

    [[noreturn]] void error(const char *msg) override {
        // Emitting "error.*:" to stdout or stderr will cause CMake to report the
        // test as a failure on Windows, regardless of the error code returned.
        // The error text we get from ErrorReport probably contains some variant
        // of this, so let's make sure it doesn't match that pattern.
        const auto msg_safe = Internal::replace_all(msg, ":", "(colon)");
        printf("Custom err: %s\n", msg_safe.c_str());
        errors_occurred++;

        if (warnings_occurred != 1 || errors_occurred != 1 || evaluated != 1) {
            printf("There should have been 1 warning and 1 error and 1 evaluated assertion argument\n");
            exit(1);
        }

        // CompileTimeErrorReporter::error() must not return.
        printf("Success!\n");
        exit(0);
    }
};

int main(int argc, char **argv) {

    // Use argc here so that the compiler cannot optimize it away:
    // we know argc > 0 always, but the compiler (probably) doesn't.
    _halide_user_assert(argc > 0) << should_never_be_evaluated();

    MyCustomErrorReporter reporter;
    set_custom_compile_time_error_reporter(&reporter);

    user_warning << "Here is a warning.";

    // This call should not return.
    user_assert(argc == 0) << should_be_evaluated();

    printf("CompileTimeErrorReporter::error() must not return.\n");
    return 1;
}
