#include "Halide.h"
#include <iostream>

int main() {
#ifdef __cpp_exceptions
    try {
        Halide::Internal::run_with_large_stack([]() {
            throw Halide::RuntimeError("Error from run_with_large_stack");
        });
    } catch (const Halide::RuntimeError &ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }

#else
    Halide::Internal::run_with_large_stack([]() {
        _halide_user_assert(0) << "Error from run_with_large_stack (no exceptions)";
    });
#endif
    std::cout << "Success!\n";
    return 0;
}
