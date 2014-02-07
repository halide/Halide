#include "Halide.h"
#include <stdio.h>

using namespace Halide;

bool error_occurred = false;
void my_error_handler(void *user_context, const char *msg) {
    printf("%s\n", msg);
    error_occurred = true;
}

int main(int argc, char **argv) {
    Func f, g;
    Var x, y;
    ImageParam param(Int(32), 2);
    Image<int> image1(128, 73);
    Image<int> image2(144, 23);

    f(x, y) = param(x, y)*2;

    param.set_bounds(0, 0, 128);

    f.set_error_handler(my_error_handler);

    // This should be fine
    param.set(image1);
    error_occurred = false;
    f.realize(20, 20);

    if (error_occurred) {
        printf("Error incorrectly raised\n");
        return -1;
    }
    // This should be an error, because dimension 0 of image 2 is not from 0 to 128 like we promised
    param.set(image2);
    error_occurred = false;
    f.realize(20, 20);

    if (!error_occurred) {
        printf("Error incorrectly not raised\n");
        return -1;
    }

    // Now try constraining the output buffer of a function
    g(x, y) = x*y;
    g.set_error_handler(my_error_handler);
    g.output_buffer().set_stride(0, 2);
    error_occurred = false;
    g.realize(image1);
    if (!error_occurred) {
        printf("Error incorrectly not raised when constraining output buffer\n");
        return -1;
    }

    Func h;
    h(x, y) = x*y;
    h.set_error_handler(my_error_handler);
    h.output_buffer()
        .set_stride(0, 1)
        .set_bounds(1, 0, image1.extent(1))
        .set_bounds(0, 0, ((h.output_buffer().extent(0))/8)*8);
    error_occurred = false;
    h.realize(image1);
    // Also check it compiles ok without an inferred argument list
    h.compile_to_assembly("h.s", Internal::vec<Argument>(image1), "h");
    if (error_occurred) {
        printf("Error incorrectly raised when constraining output buffer\n");
        return -1;
    }

    printf("Success!\n");
    return 0;
}
