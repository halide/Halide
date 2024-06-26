#include "Halide.h"
#include <stdio.h>

using namespace Halide;

bool error_occurred;
void my_error_handler(JITUserContext *user_context, const char *msg) {
    error_occurred = true;
}

int main(int argc, char **argv) {
    // Use explicit set_range() calls
    {
        Func f, g;
        Var x, y;
        Param<float> p;

        Buffer<float> input(100, 100);

        p.set_range(1, 10);

        g(x, y) = input(x, y) + 1.0f;

        g.compute_root();
        f(x, y) = g(cast<int>(x / p), y);

        f.jit_handlers().custom_error = my_error_handler;

        error_occurred = false;
        p.set(2);
        f.realize({100, 100});
        if (error_occurred) {
            printf("Error incorrectly raised\n");
            return 1;
        }

        p.set(0);
        error_occurred = false;
        f.realize({100, 100});
        if (!error_occurred) {
            printf("Error should have been raised\n");
            return 1;
        }
    }
    // Use ctor arguments
    {
        Func f, g;
        Var x, y;
        // initial value: 2, min: 1, max: 10
        Param<float> p(2, 1, 10);
        Buffer<float> input(100, 100);

        g(x, y) = input(x, y) + 1.0f;

        g.compute_root();
        f(x, y) = g(cast<int>(x / p), y);

        f.jit_handlers().custom_error = my_error_handler;

        error_occurred = false;
        f.realize({100, 100});
        if (error_occurred) {
            printf("Error incorrectly raised\n");
            return 1;
        }

        p.set(0);
        error_occurred = false;
        f.realize({100, 100});
        if (!error_occurred) {
            printf("Error should have been raised\n");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
