#include "HalideBuffer.h"
#include <gtest/gtest.h>

using namespace Halide::Runtime;

TEST(ForEachElementTest, Basic) {
    // Try several different ways of accessing the pixels of an image,
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
        EXPECT_EQ(im(x, y, c), correct)
            << "x = " << x << ", y = " << y << ", c = " << c;
        im(pos) *= 3;
    });

    im.for_each_element([&](int x, int y) {
        for (int c = 0; c < 3; c++) {
            int correct = (10 * x + 5 * y + c) * 3;
            EXPECT_EQ(im(x, y, c), correct)
                << "x = " << x << ", y = " << y << ", c = " << c;
            im(x, y, c) *= 2;
        }
    });

    for (int c = im.dim(2).min(); c < im.dim(2).min() + im.dim(2).extent(); c++) {
        for (int y = im.dim(1).min(); y < im.dim(1).min() + im.dim(1).extent(); y++) {
            for (int x = im.dim(0).min(); x < im.dim(0).min() + im.dim(0).extent(); x++) {
                int correct = (10 * x + 5 * y + c) * 6;
                EXPECT_EQ(im(x, y, c), correct)
                    << "x = " << x << ", y = " << y << ", c = " << c;
                im(x, y, c) *= 2;
            }
        }
    }

    for (int c : im.dim(2)) {
        for (int y : im.dim(1)) {
            for (int x : im.dim(0)) {
                int correct = (10 * x + 5 * y + c) * 12;
                EXPECT_EQ(im(x, y, c), correct)
                    << "x = " << x << ", y = " << y << ", c = " << c;
            }
        }
    }
}

TEST(ForEachElementTest, ZeroDimensional) {
    Buffer<int> scalar_im = Buffer<int>::make_scalar();
    scalar_im() = 5;

    // Not sure why you'd ever do this, but it verifies that
    // for_each_element does the right thing even in a corner case.
    scalar_im.for_each_element([&] { scalar_im()++; });
    EXPECT_EQ(scalar_im(), 6);
}
