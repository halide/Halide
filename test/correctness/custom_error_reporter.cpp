#include <Halide.h>
#include <string.h>
#include <stdio.h>

using namespace Halide;

int errors_occurred;
int warnings_occurred;

void my_custom_error_report(const char* msg, bool warning) {
    printf("Custom error: %s (warning = %d)\n", msg, (int) warning);
    if (warning)
        warnings_occurred++;
    else
        errors_occurred++;
}

int main(int argc, char **argv) {

    set_custom_error_reporter(my_custom_error_report);

    Halide::Internal::ErrorReport("", 0, NULL, false, true, false, false) << "Here is an error.";
    Halide::Internal::ErrorReport("", 0, NULL, false, true, true, false) << "Here is a warning.";

    if (warnings_occurred != 1 || errors_occurred != 1) {
        printf("There should have been 1 warning and 1 error\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
