#include "Halide.h"
#include <iostream>

int main() {
    try {
        Halide::Internal::run_with_large_stack([]() {
            throw Halide::RuntimeError("Error from run_with_large_stack");
        });
    } catch (const Halide::RuntimeError &ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }

    std::cout << "Success!\n";
    return 0;
}
