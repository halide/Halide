#include <stdio.h>
#include <Halide.h>

// This tests out-of-bounds reads from an input image

using namespace Halide;

// Custom error handler. If we don't define this, it'll just print out
// an error message and quit
bool error_occurred = false;
void halide_error(char *msg) {
    printf("%s\n", msg);
    error_occurred = true;
}

extern "C" void set_error_handler(void (*)(char *));

int main(int argc, char **argv) {
    Image<float> input(19);
    Var x;
    Func f;
    f(x) = input(x)*2;

    f.set_error_handler(&halide_error);

    // One easy way to read out of bounds
    f.realize(23);

    if (!error_occurred) {
        printf("There should have been an out-of-bounds error\n");
        return 1;
    }
    error_occurred = false;

    // Another more subtle way to read out of bounds due to bounds
    // expansion when vectorizing
    Func g, h;
    g(x) = input(x)*2;
    h(x) = g(x);
    g.compute_root().vectorize(x, 4);

    h.set_error_handler(&halide_error);
    h.realize(19);
    
    if (!error_occurred) {
        printf("There should have been an out-of-bounds error\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
