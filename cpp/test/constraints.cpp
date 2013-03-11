#include "Halide.h"
#include <stdio.h>

using namespace Halide;

bool error_occurred = false;
void my_error_handler(char *) {
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

    printf("Success!\n");
    return 0;
}
