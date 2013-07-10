#include <Halide.h>
#include <stdio.h>

using namespace Halide;

bool error_occurred;
void my_error_handler(char *msg) {
    error_occurred = true;
}

int main(int argc, char **argv) {
    // Extract the ith column of an image
    Func f, g;
    Var x, y;
    Param<float> p;

    Image<float> input(100, 100);

    p.set_range(1, 10);

    g(x, y) = input(x, y) + 1.0f;

    g.compute_root();
    f(x, y) = g(cast<int>(x/p), y);

    f.set_error_handler(my_error_handler);

    error_occurred = false;
    p.set(2);
    f.realize(100, 100);
    if (error_occurred) {
        printf("Error incorrectly raised\n");
        return -1;
    }

    p.set(0);
    error_occurred = false;
    f.realize(100, 100);
    if (!error_occurred) {
        printf("Error should have been raised\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
