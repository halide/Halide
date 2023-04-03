#include "Halide.h"

using namespace Halide;

void check_error(bool error) {
    if (!error) {
        std::cout << "There was supposed to be an error!\n";
        exit(1);
    }
}

int main(int argc, char **argv) {
    // test whether normal cases work
    {
        // Test vectorized tile
        Var i, j;

        Func f;
        f(i, j) = i * j;

        Var io, jo;
        f.tile({i, j}, {io, jo}, {i, j}, {8, 8}, {TailStrategy::RoundUp, TailStrategy::RoundUp});
        f.realize({128, 128});
    }

    {
        // Test vectorized tile with default tail strategy
        Var i, j;

        Func f;
        f(i, j) = i * j;

        Var io, jo;
        f.tile({i, j}, {io, jo}, {i, j}, {8, 8});
        f.realize({128, 128});
    }

    {
        // Test Stage.tile with default tail strategy
        Var i, j;

        Func f;
        f(i, j) = 0;
        f(i, j) += i * j;

        Var io, jo;
        f.update(0).tile({i, j}, {io, jo}, {i, j}, {8, 8});
        f.realize({128, 128});
    }

    printf("Success!\n");
    return 0;
}
