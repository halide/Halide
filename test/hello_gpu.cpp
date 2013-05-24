#include <stdio.h>
#include <Halide.h>
#include <iostream>

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y");
    Func f("f");

    printf("Defining function...\n");

    f(x, y) = x*y;

    char *target = getenv("HL_TARGET");
    if (target && std::string(target) == "ptx") {
        f.cuda_tile(x, y, 8, 8);
    }
 
    printf("Realizing function...\n");

    Image<int> imf = f.realize(32, 32);

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            if (imf(i, j) != i*j) {
                printf("imf[%d, %d] = %d\n", i, j, imf(i, j));
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
