#include <Halide.h>
#include <cstdio>
#include <cstdlib>
using namespace Halide;

int main() {
    Var x{"x"}, y{"y"};
    Func test{"test"};

    test(x, y) = x + y;
    Buffer<int, 2> output = test.realize({4, 4});

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (output(i, j) != (i + j)) {
                fprintf(stderr, "output(%d, %d) = %d, expected %d", i, j, output(i, j), i + j);
                return EXIT_FAILURE;
            }
        }
    }

    printf("Success!\n");
    return EXIT_SUCCESS;
}
