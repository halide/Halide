#include "Halide.h"
#include "expect_user_error.h"

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

#ifdef HALIDE_WITH_EXCEPTIONS
    {
        // Test reporting of mismatched sizes error in vector-of-strategies variant
        bool ok = expect_user_error("vector_tile", "Vectors passed to Stage::tile must all be the same length.", []() {
            Var i, j;

            Func f;
            f(i, j) = i * j;

            Var io, jo;
            // Bad because the vector of tail strategies has a different
            // length than the vectors of vars/factors.
            f.tile({i, j}, {io, jo}, {i, j}, {8, 8}, {TailStrategy::RoundUp, TailStrategy::RoundUp, TailStrategy::RoundUp});
        });
        if (!ok) return 1;
    }
#endif

    printf("Success!\n");
    return 0;
}
