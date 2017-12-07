#include "Halide.h"
#include <stdio.h>
#include <thread>

using namespace Halide;

int main(int argc, char **argv) {
    // Test if the compiler itself is thread-safe. This test is
    // intended to be run in a thread-sanitizer.

    // std::thread has implementation-dependent behavior; some implementations
    // may refuse to create an arbitrary number. So let's create a smallish
    // number (8) and have each one do enough work that contention is likely
    // to be encountered.
    constexpr int total_iters = 1024;
    constexpr int num_threads = 8;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([=]{
            for (int i = 0; i < (total_iters / num_threads); i++) {
                Func f;
                Var x;
                f(x) = x;
                f.realize(100);
            }
        });
    }

    for (auto &t : threads) {
        t.join();
    }

    printf("Success!\n");

    return 0;
}
