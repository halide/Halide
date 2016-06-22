#include "Halide.h"
#include <string.h>
#include <stdio.h>

using namespace Halide;


class MyCustomErrorReporter : public Halide::CompileTimeErrorReporter {
public:
    int errors_occurred;
    int warnings_occurred;

    MyCustomErrorReporter() : errors_occurred(0), warnings_occurred(0) {}

    void warning(const char* msg) {
        printf("Custom warning: %s\n", msg);
        warnings_occurred++;
    }

    void error(const char* msg) {
        printf("Custom error: %s\n", msg);
        errors_occurred++;

        if (warnings_occurred != 1 || errors_occurred != 1) {
            printf("There should have been 1 warning and 1 error\n");
            exit(-1);
        }

        // CompileTimeErrorReporter::error() must not return.
        printf("Success!\n");
        exit(0);
    }
};

int main(int argc, char **argv) {

    MyCustomErrorReporter reporter;
    set_custom_compile_time_error_reporter(&reporter);

    Halide::Internal::ErrorReport("", 0, nullptr, false, true, true, false) << "Here is a warning.";

    // This call should not return.
    Halide::Internal::ErrorReport("", 0, nullptr, false, true, false, false) << "Here is an error.";

    printf("CompileTimeErrorReporter::error() must not return.\n");
    return -1;
}
