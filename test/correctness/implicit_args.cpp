#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Var x("x"), y("y"), z("z"), w("w");

    ImageParam im1(Int(32), 3);
    assert(im1.dimensions() == 3);
    // im1 is a 3d imageparam

    Buffer<int> im1_val = lambda(x, y, z, x * y * z).realize({10, 10, 10});
    im1.set(im1_val);

    Buffer<int> im2 = lambda(x, y, x + y).realize({10, 10});
    assert(im2.dimensions() == 2);
    assert(im2(4, 6) == 10);
    // im2 is a 2d image

    Func f;
    f(x, _) = im1(_) + im2(x, _) + im2(_);
    // Equivalent to
    // f(x, i, j, k) = im1(i, j, k) + im2(x, i) + im2(i, j);
    // f(x, i, j, k) = i*j*k + x+i + i+j;

    Buffer<int> result1 = f.realize({2, 2, 2, 2});
    for (int k = 0; k < 2; k++) {
        for (int j = 0; j < 2; j++) {
            for (int i = 0; i < 2; i++) {
                for (int x = 0; x < 2; x++) {
                    int correct = i * j * k + x + i + i + j;
                    if (result1(x, i, j, k) != correct) {
                        printf("result1(%d, %d, %d, %d) = %d instead of %d\n",
                               x, i, j, k, result1(x, i, j, k), correct);
                        return 1;
                    }
                }
            }
        }
    }

    // f is a 4d function (thanks to the first arg having 3 implicit arguments
    assert(f.dimensions() == 4);

    Func g;
    g(_) = f(2, 2, _) + im2(Expr(1), _);
    f.compute_root();
    // Equivalent to
    // g(i, j) = f(2, 2, i, j) + im2(1, i);
    // g(i, j) = 2*i*j + 2+2 + 2+i + 1+i

    assert(g.dimensions() == 2);

    Buffer<int> result2 = g.realize({10, 10});
    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < 10; i++) {
            int correct = 2 * i * j + 2 + 2 + 2 + i + 1 + i;
            if (result2(i, j) != correct) {
                printf("result2(%d, %d) = %d instead of %d\n",
                       i, j, result2(i, j), correct);
                return 1;
            }
        }
    }

    // An image which ensures any transposition of unequal coordinates changes the value
    Buffer<int> im3 = lambda(x, y, z, w, (x << 24) | (y << 16) | (z << 8) | w).realize({10, 10, 10, 10});

    Func transpose_last_two;
    transpose_last_two(_, x, y) = im3(_, y, x);
    // Equivalent to transpose_last_two(_0, _1, x, y) = im3(_0, _1, x, y)

    Buffer<int> transposed = transpose_last_two.realize({10, 10, 10, 10});

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            for (int k = 0; k < 10; k++) {
                for (int l = 0; l < 10; l++) {
                    int correct = (i << 24) | (j << 16) | (l << 8) | k;
                    if (transposed(i, j, k, l) != correct) {
                        printf("transposed(%d, %d, %d, %d) = %d instead of %d\n",
                               i, j, k, l, transposed(i, j, k, l), correct);
                        return 1;
                    }
                }
            }
        }
    }

    Func hairy_transpose;
    hairy_transpose(_, x, y) = im3(y, _, x) + im3(y, x, _);
    // Equivalent to hairy_transpose(_0, _1, x, y) = im3(y, _0, _1, x) +
    // im3(y, x, _0, _1)

    Buffer<int> hairy_transposed = hairy_transpose.realize({10, 10, 10, 10});

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            for (int k = 0; k < 10; k++) {
                for (int l = 0; l < 10; l++) {
                    int correct1 = (l << 24) | (i << 16) | (j << 8) | k;
                    int correct2 = (l << 24) | (k << 16) | (i << 8) | j;
                    int correct = correct1 + correct2;
                    if (hairy_transposed(i, j, k, l) != correct) {
                        printf("hairy_transposed(%d, %d, %d, %d) = %d instead of %d\n",
                               i, j, k, l, hairy_transposed(i, j, k, l), correct);
                        return 1;
                    }
                }
            }
        }
    }

    Func hairy_transpose2;
    hairy_transpose2(_, x) = im3(_, x) + im3(x, x, _);
    // Equivalent to hairy_transpose2(_0, _1, _2, x) = im3(_0, _1, _2, x) +
    // im3(x, x, _0, _1)

    Buffer<int> hairy_transposed2 = hairy_transpose2.realize({10, 10, 10, 10});

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            for (int k = 0; k < 10; k++) {
                for (int l = 0; l < 10; l++) {
                    int correct1 = (i << 24) | (j << 16) | (k << 8) | l;
                    int correct2 = (l << 24) | (l << 16) | (i << 8) | j;
                    int correct = correct1 + correct2;
                    if (hairy_transposed2(i, j, k, l) != correct) {
                        printf("hairy_transposed2(%d, %d, %d, %d) = %d instead of %d\n",
                               i, j, k, l, hairy_transposed2(i, j, k, l), correct);
                        return 1;
                    }
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
