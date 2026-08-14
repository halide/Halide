#include "Halide.h"

#include <cstdio>

// Regression test for a bug where HL_COMPILER_STACK_SIZE was parsed (and
// validated) during global static initialization -- so a malformed value
// crashed the process before main() even started, uncatchable by any user
// code. Parsing is now deferred to first use (a lazily-initialized
// function-local static), so the resulting Halide::CompileError is a normal,
// catchable exception. This must be the very first thing this process does
// with the environment variable, since the lazy singleton only parses it
// once, on first access.

int main(int argc, char **argv) {
#ifdef _MSC_VER
    _putenv_s("HL_COMPILER_STACK_SIZE", "not_a_number");
#else
    setenv("HL_COMPILER_STACK_SIZE", "not_a_number", 1);
#endif

    try {
        (void)Halide::get_compiler_stack_size();
    } catch (const Halide::CompileError &e) {
        std::string msg = e.what();
        if (msg.find("HL_COMPILER_STACK_SIZE") == std::string::npos) {
            std::cout << "FAILED: exception message didn't mention HL_COMPILER_STACK_SIZE: " << msg << "\n";
            return 1;
        }
        printf("Success!\n");
        return 0;
    }

    std::cout << "FAILED: expected a Halide::CompileError to be thrown for a malformed HL_COMPILER_STACK_SIZE\n";
    return 1;
}
