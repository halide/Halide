#include <iostream>
#include <cstdlib>
#include "Halide.h"

using namespace Halide;

bool false_cycle() {
    Func f{"f"}, g{"g"};
    Var x{"x"}, y{"y"}, z{"z"};

    f(x, y, z) = x * y * z;
    f(x, y, z) += 1;

    g(x, y, z) = sin(x + y + z);
    g(x, y, z) += 1;

    f.compute_root();
    g.compute_root();

    f.compute_with(g, y);
    g.update(0).compute_with(f.update(0), z);

    Func out{"out"};
    out(x, y, z) = f(x, y, z) + g(x, y, z);

    out.print_loop_nest();

    return true;
}

bool distant_updates() {
    Func f{"f"}, g{"g"};
    Var x{"x"};

    f(x) = x;
    f(x) += 1; // 0
    f(x) += 1; // 1
    f(x) += 1; // 2
    f(x) += 1; // 3
    f(x) += 1; // 4
    f(x) += 1; // 5
    f(x) += 1; // 6
    f(x) += 1; // 7
    f(x) += 1; // 8

    g(x) = x;
    g(x) += 1; // 0
    g(x) += 1; // 1
    g(x) += 1; // 2
    g(x) += 1; // 3
    g(x) += 1; // 4
    g(x) += 1; // 5
    g(x) += 1; // 6
    g(x) += 1; // 7
    g(x) += 1; // 8

    Func output{"output"};
    output() = f(1) + g(1);

    f.compute_root();
    g.compute_root();

    f.update(8).compute_with(g.update(2), x);

    output.print_loop_nest();

    Buffer<int> result = output.realize();
    const int expected_result = 20;

    bool succeeded = expected_result != result(0);
    if (!succeeded) {
        std::cerr << "Error! Expected " << expected_result << " but pipeline returned " << result(0) << "\n";
    }

    return succeeded;
}

int main(int argc, char **argv) {
    bool succeeded = true;
    succeeded &= false_cycle();
    succeeded &= distant_updates();

    if (succeeded) {
        std::cout << "Success!" << std::endl;
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}