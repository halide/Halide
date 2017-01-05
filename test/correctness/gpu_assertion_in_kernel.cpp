#include "Halide.h"

using namespace Halide;

bool errored = false;
void my_error(void *, const char *msg) {
    printf("Expected error: %s\n", msg);
    errored = true;
}

void my_print(void *, const char *msg) {
    // Empty to neuter debug message spew
}

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA)) {
        printf("Not running test because cuda not enabled\n");
        return 0;
    }

    // Turn on debugging so that the pipeline completes and error
    // checking is done before realize returns. Otherwise errors are
    // discovered too late to call a custom error handler.
    t.set_feature(Target::Debug);

    Func f;
    Var c, x;
    f(c, x) = x + c + 3;
    f.bound(c, 0, 3).unroll(c);

    Func g;
    g(c, x) = f(c, x) * 8;

    Var xi;
    g.gpu_tile(x, xi, 8);
    f.compute_at(g, x).gpu_threads(x);

    g.set_error_handler(&my_error);
    g.set_custom_print(&my_print);

    // Should succeed
    g.realize(3, 100, t);
    if (errored) {
        printf("There was not supposed to be an error\n");
        return -1;
    }

    // Should trap
    g.realize(4, 100, t);

    if (!errored) {
        printf("There was supposed to be an error\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
