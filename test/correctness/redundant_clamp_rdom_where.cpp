#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// A max pool in the style of hannk's: the input is read through a clamp, and
// the reduction domain is restricted by a where clause that says the same
// thing. The simplifier may remove the clamp as redundant, so bounds inference
// must get the region of the input that is read from the where clause alone.
// Otherwise the pipeline asks for input it doesn't need and fails its own
// bounds check.
int hannk_style_max_pool() {
    ImageParam input(UInt(8), 2, "input");
    Param<int> stride_x("stride_x"), stride_y("stride_y");
    Param<int> filter_width("filter_width"), filter_height("filter_height");

    Var x("x"), y("y");

    Expr min_x = input.dim(0).min();
    Expr max_x = input.dim(0).max();
    Expr min_y = input.dim(1).min();
    Expr max_y = input.dim(1).max();

    Func input_bounded("input_bounded");
    input_bounded(x, y) = input(clamp(x, min_x, max_x), clamp(y, min_y, max_y));

    RDom r(0, filter_width, 0, filter_height);
    Expr x_rx = x * stride_x + r.x;
    Expr y_ry = y * stride_y + r.y;
    r.where(min_x <= x_rx && x_rx <= max_x && min_y <= y_ry && y_ry <= max_y);

    Func maximum("maximum");
    maximum(x, y) = cast<uint8_t>(0);
    maximum(x, y) = max(maximum(x, y), input_bounded(x_rx, y_ry));

    Func output("output");
    output(x, y) = maximum(x, y);

    // The input starts at 1, and the first output pixel's window starts at 0,
    // off the edge of it, the way padding does.
    const int w = 16, h = 12, fw = 3, fh = 3;
    Buffer<uint8_t> input_buf(w, h);
    input_buf.set_min(1, 1);
    input_buf.for_each_element([&](int i, int j) {
        input_buf(i, j) = (uint8_t)(i * 7 + j * 13);
    });

    input.set(input_buf);
    stride_x.set(1);
    stride_y.set(1);
    filter_width.set(fw);
    filter_height.set(fh);

    Buffer<uint8_t> out = output.realize({w, h});

    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            uint8_t correct = 0;
            for (int ry = 0; ry < fh; ry++) {
                for (int rx = 0; rx < fw; rx++) {
                    int xi = i + rx, yi = j + ry;
                    if (xi >= 1 && xi <= w && yi >= 1 && yi <= h) {
                        correct = std::max(correct, input_buf(xi, yi));
                    }
                }
            }
            if (out(i, j) != correct) {
                printf("out(%d, %d) = %d instead of %d\n", i, j, out(i, j), correct);
                return 1;
            }
        }
    }
    return 0;
}

// As above, but the window samples every other input pixel, and the where
// clause bounds the scaled reduction variable, 2 * r.x, rather than the index
// itself. The two only agree once the coefficient is taken into account.
int scaled_rdom_max_pool() {
    ImageParam input(UInt(8), 1, "input");
    Param<int> stride_x("stride_x"), filter_width("filter_width");

    Var x("x");

    Expr min_x = input.dim(0).min();
    Expr max_x = input.dim(0).max();

    Func input_bounded("input_bounded");
    input_bounded(x) = input(clamp(x, min_x, max_x));

    RDom r(0, filter_width);
    Expr x_rx = x * stride_x + 2 * r.x;
    r.where(min_x <= x_rx && 2 * r.x <= max_x - x * stride_x);

    Func maximum("maximum");
    maximum(x) = cast<uint8_t>(0);
    maximum(x) = max(maximum(x), input_bounded(x_rx));

    Func output("output");
    output(x) = maximum(x);

    const int w = 16, fw = 3;
    Buffer<uint8_t> input_buf(w);
    input_buf.set_min(1);
    input_buf.for_each_element([&](int i) {
        input_buf(i) = (uint8_t)(i * 7);
    });

    input.set(input_buf);
    stride_x.set(1);
    filter_width.set(fw);

    Buffer<uint8_t> out = output.realize({w});

    for (int i = 0; i < w; i++) {
        uint8_t correct = 0;
        for (int rx = 0; rx < fw; rx++) {
            int xi = i + 2 * rx;
            if (xi >= 1 && xi <= w) {
                correct = std::max(correct, input_buf(xi));
            }
        }
        if (out(i) != correct) {
            printf("out(%d) = %d instead of %d\n", i, out(i), correct);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (hannk_style_max_pool() != 0) {
        return 1;
    }
    if (scaled_rdom_max_pool() != 0) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
