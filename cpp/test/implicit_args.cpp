#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {

    Var x("x"), y("y"), z("z");

    ImageParam im1(Int(32), 3);
    assert(im1.dimensions() == 3);
    // im1 is a 3d imageparam

    Image<int> im1_val = lambda(x, y, z, x*y*z).realize(10, 10, 10);
    im1.set(im1_val);

    Image<int> im2 = lambda(x, y, x+y).realize(10, 10);
    assert(im2.dimensions() == 2);
    assert(im2(4, 6) == 10);
    // im2 is a 2d image
      
    Func f;
    f(x) = im1 + im2(x) + im2;
    // Equivalent to 
    // f(x, i, j, k) = im1(i, j, k) + im2(x, i) + im2(i, j);
    // f(x, i, j, k) = i*j*k + x+i + i+j;

    Image<int> result1 = f.realize(2, 2, 2, 2);
    for (int k = 0; k < 2; k++) {
        for (int j = 0; j < 2; j++) {
            for (int i = 0; i < 2; i++) {
                for (int x = 0; x < 2; x++) {
                    int correct = i*j*k + x+i + i+j;
                    if (result1(x, i, j, k) != correct) {
                        printf("result1(%d, %d, %d, %d) = %d instead of %d\n",
                               x, i, j, k, result1(x, i, j, k), correct);
                        return -1;
                    }
                }
            }
        }
    }

    // f is a 4d function (thanks to the first arg having 3 implicit arguments
    assert(f.dimensions() == 4);

    Func g;
    g() = f(2, 2) + im2(Expr(1)); 
    f.compute_root();
    // Equivalent to 
    // g(i, j) = f(2, 2, i, j) + im2(1, i);
    // g(i, j) = 2*i*j + 2+2 + 2+i + 1+i

    assert(g.dimensions() == 2);

    Image<int> result2 = g.realize(10, 10);
    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < 10; i++) {
            int correct = 2*i*j + 2+2 + 2+i + 1+i;
            if (result2(i, j) != correct) {
                printf("result2(%d, %d) = %d instead of %d\n", 
                       i, j, result2(i, j), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
