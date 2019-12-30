#include "Halide.h"
#include <stdio.h>

// This tests out-of-bounds reads from an input image

using namespace Halide;

// Custom error handler. If we don't define this, it'll just print out
// an error message and quit
bool error_occurred = false;
void halide_error(void *, const char *msg) {
    printf("%s\n", msg);
    error_occurred = true;
}

extern "C" void set_error_handler(void (*)(void *, const char *));

int main(int argc, char **argv) {
    Buffer<float> input(19);
    for (int i = 0; i < 19; i++) {
        input(i) = i;
    }
    Var x;
    Func f;
    f(x) = input(x) * 2;

    f.set_error_handler(&halide_error);

    // One easy way to read out of bounds
    f.realize(23);

    if (!error_occurred) {
        printf("There should have been an out-of-bounds error\n");
        return 1;
    }
    error_occurred = false;

    // Another more subtle way to read out of bounds used to be due to
    // bounds expansion when vectorizing. This used to be an
    // out-of-bounds error, but now isn't! Hooray!
    Func g, h;
    g(x) = input(x) * 2;
    h(x) = g(x);
    g.compute_root().vectorize(x, 4);

    h.set_error_handler(&halide_error);
    h.realize(18);

    if (error_occurred) {
        printf("There should not have been an out-of-bounds error\n");
        return 1;
    }

    // But if we try to make the input smaller than the vector width, it
    // still won't work.
    Buffer<float> small_input(3);
    Func i;
    i(x) = small_input(x);
    i.vectorize(x, 4);
    i.set_error_handler(&halide_error);
    i.realize(4);
    if (!error_occurred) {
        printf("There should have been an out-of-bounds error\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
