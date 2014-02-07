#include <stdio.h>
#include <Halide.h>

// This tests out-of-bounds reads from an input image

using namespace Halide;

// Custom error handler. If we don't define this, it'll just print out
// an error message and quit
bool error_occurred = false;
void halide_error(void *, const char *msg, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, msg);
    printf("Not an error: ");
    vprintf(msg, args);
    __builtin_va_end(args);
    error_occurred = true;
}

int main(int argc, char **argv) {
    Image<float> input(19);
    for (int i = 0; i < 19; i++) {
        input(i) = i;
    }
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

    // Another more subtle way to read out of bounds used to be due to
    // bounds expansion when vectorizing. This used to be an
    // out-of-bounds error, but now isn't! Hooray!
    Func g, h;
    g(x) = input(x)*2;
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
    Image<float> small_input(3);
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
