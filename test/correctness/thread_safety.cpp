#include "Halide.h"
#include <stdio.h>
#include <thread>

using namespace Halide;

int main(int argc, char **argv) {
    // Test if the compiler itself is thread-safe. This test is
    // intended to be run in a thread-sanitizer.
    std::vector<std::thread> threads;
    for (int i = 0; i < 1000; i++) {
        threads.emplace_back([]{
            Func f;
            Var x;
            f(x) = x;
            f.realize(100);
        });
    }

    for (auto &t : threads) {
        t.join();
    }

    printf("Success!\n");

    return 0;
}
