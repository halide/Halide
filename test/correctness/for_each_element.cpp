#include "HalideBuffer.h"

#include <stdio.h>

using namespace Halide::Runtime;

int main(int argc, char **argv) {
    // Try several different ways of accessing a the pixels of an image,
    // and check that they all do the same thing.
    Buffer<int> im(1000, 1000, 3);

    // Make the image non-dense in memory to make life more interesting
    im = im.cropped(0, 100, 800).cropped(1, 200, 600);

    im.for_each_element([&](int x, int y, int c) {
        im(x, y, c) = 10 * x + 5 * y + c;
    });

    im.for_each_element([&](const int *pos) {
        int x = pos[0], y = pos[1], c = pos[2];
        int correct = 10 * x + 5 * y + c;
        if (im(x, y, c) != correct) {
            printf("im(%d, %d, %d) = %d instead of %d\n",
                   x, y, c, im(x, y, c), correct);
            abort();
        }
        im(pos) *= 3;
    });

    im.for_each_element([&](int x, int y) {
        for (int c = 0; c < 3; c++) {
            int correct = (10 * x + 5 * y + c) * 3;
            if (im(x, y, c) != correct) {
                printf("im(%d, %d, %d) = %d instead of %d\n",
                       x, y, c, im(x, y, c), correct);
                abort();
            }
            im(x, y, c) *= 2;
        }
    });

    for (int c = im.dim(2).min(); c < im.dim(2).min() + im.dim(2).extent(); c++) {
        for (int y = im.dim(1).min(); y < im.dim(1).min() + im.dim(1).extent(); y++) {
            for (int x = im.dim(0).min(); x < im.dim(0).min() + im.dim(0).extent(); x++) {
                int correct = (10 * x + 5 * y + c) * 6;
                if (im(x, y, c) != correct) {
                    printf("im(%d, %d, %d) = %d instead of %d\n",
                           x, y, c, im(x, y, c), correct);
                    abort();
                }
                im(x, y, c) *= 2;
            }
        }
    }

    for (int c : im.dim(2)) {
        for (int y : im.dim(1)) {
            for (int x : im.dim(0)) {
                int correct = (10 * x + 5 * y + c) * 12;
                if (im(x, y, c) != correct) {
                    printf("im(%d, %d, %d) = %d instead of %d\n",
                           x, y, c, im(x, y, c), correct);
                    abort();
                }
            }
        }
    }

    // Test a zero-dimensional image too.
    Buffer<int> scalar_im = Buffer<int>::make_scalar();
    scalar_im() = 5;

    // Not sure why you'd ever do this, but it verifies that
    // for_each_element does the right thing even in a corner case.
    scalar_im.for_each_element([&]() { scalar_im()++; });

    if (scalar_im() != 6) {
        printf("scalar_im() == %d instead of 6\n", scalar_im());
        return 1;
    }

    printf("Success!\n");
    return 0;
}
