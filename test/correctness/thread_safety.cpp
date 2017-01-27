#include "Halide.h"
#include <stdio.h>
#include <future>

using namespace Halide;

static std::atomic<int> foo;

int main(int argc, char **argv) {
    // Test if the compiler itself is thread-safe. This test is
    // intended to be run in a thread-sanitizer.
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 1000; i++) {
        futures.emplace_back(std::async(std::launch::async, []{
            Func f;
            Var x;
            f(x) = x;
            f.realize(100);
        }));
    }

    for (auto &f : futures) {
        f.wait();
    }

    printf("Success!\n");

    return 0;
}
