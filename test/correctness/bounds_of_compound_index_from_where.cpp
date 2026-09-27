#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// A where clause on an index made of several variables bounds both the index
// as a whole and each of the variables it is made of. Bounds inference must
// use it both ways: for the input read at that index, and for any input read
// at just one of its variables.

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

// As above, but the input is read at half the position the where clause
// bounds, so the clamped index is only a part of what the condition mentions.
int halved_index_max_pool() {
    ImageParam input(UInt(8), 1, "input");
    Param<int> stride_x("stride_x"), filter_width("filter_width");

    Var x("x");

    Expr min_x = input.dim(0).min();
    Expr max_x = input.dim(0).max();

    Func input_bounded("input_bounded");
    input_bounded(x) = input(clamp(x, min_x, max_x));

    RDom r(0, filter_width);
    Expr x_rx = x * stride_x + r.x;
    r.where(2 * min_x <= x_rx && x_rx <= 2 * max_x + 1);

    Func maximum("maximum");
    maximum(x) = cast<uint8_t>(0);
    maximum(x) = max(maximum(x), input_bounded(x_rx / 2));

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
            int xi = i + rx;
            if (xi >= 2 && xi <= 2 * w + 1) {
                correct = std::max(correct, input_buf(xi / 2));
            }
        }
        if (out(i) != correct) {
            printf("out(%d) = %d instead of %d\n", i, out(i), correct);
            return 1;
        }
    }
    return 0;
}

// A where clause on the sum of two reduction variables. Bounds inference names
// the sum so that the clause can bound it as a whole, but the clause must keep
// bounding each variable on its own too, for the input that is read at just
// one of them.
int where_on_sum_bounds_each_term() {
    ImageParam in(Int(32), 1, "in"), in2(Int(32), 1, "in2");

    Var x("x");
    RDom r(0, 200, 0, 200);
    r.where(r.x + r.y < 50);

    Func f("f");
    f(x) = 0;
    f(x) += in(r.x) + in2(r.x + r.y);

    f.infer_input_bounds({4});
    for (ImageParam *p : {&in, &in2}) {
        Buffer<> b = p->get();
        if (b.dim(0).min() != 0 || b.dim(0).extent() != 50) {
            printf("%s is required over [%d, %d] instead of [0, 49]\n",
                   p->name().c_str(), b.dim(0).min(), b.dim(0).max());
            return 1;
        }
    }

    Buffer<int> in_buf(50), in2_buf(50);
    in_buf.for_each_element([&](int i) { in_buf(i) = i; });
    in2_buf.for_each_element([&](int i) { in2_buf(i) = 1000 * i; });
    in.set(in_buf);
    in2.set(in2_buf);

    Buffer<int> out = f.realize({4});
    int correct = 0;
    for (int ry = 0; ry < 200; ry++) {
        for (int rx = 0; rx < 200; rx++) {
            if (rx + ry < 50) {
                correct += in_buf(rx) + in2_buf(rx + ry);
            }
        }
    }
    for (int i = 0; i < 4; i++) {
        if (out(i) != correct) {
            printf("out(%d) = %d instead of %d\n", i, out(i), correct);
            return 1;
        }
    }
    return 0;
}

// The where clause bounds x*2 + r.x + r.y, and the clamp on the input read at
// it is redundant, but a second input is read at just the x*2 + r.x part. That
// part must still be bounded by the clause: through what it says about r.x
// and x on their own, the index stays within [0, 32] for a 16 wide output.
int where_on_compound_index_bounds_its_parts() {
    ImageParam input(Int(32), 1, "input"), in2(Int(32), 1, "in2");
    Param<int> min_x("min_x"), max_x("max_x");

    Var x("x");
    RDom r(0, 200, 0, 200);
    Expr xr = x * 2 + r.x;
    r.where(min_x <= xr + r.y && xr + r.y <= max_x);

    Func m("m");
    m(x) = 0;
    m(x) += input(clamp(xr + r.y, min_x, max_x)) + in2(xr);

    Func output("output");
    output(x) = m(x);

    const int w = 16;
    min_x.set(1);
    max_x.set(w);
    output.infer_input_bounds({w});
    {
        Buffer<> b = input.get();
        if (b.dim(0).min() != 1 || b.dim(0).max() != w) {
            printf("input is required over [%d, %d] instead of [1, %d]\n",
                   b.dim(0).min(), b.dim(0).max(), w);
            return 1;
        }
        b = in2.get();
        if (b.dim(0).min() < 0 || b.dim(0).max() > 32) {
            printf("in2 is required over [%d, %d], which is wider than [0, 32]\n",
                   b.dim(0).min(), b.dim(0).max());
            return 1;
        }
    }

    Buffer<int> input_buf(w), in2_buf(33);
    input_buf.set_min(1);
    input_buf.for_each_element([&](int i) { input_buf(i) = i; });
    in2_buf.for_each_element([&](int i) { in2_buf(i) = 1000 * i; });
    input.set(input_buf);
    in2.set(in2_buf);

    Buffer<int> out = output.realize({w});
    for (int i = 0; i < w; i++) {
        int correct = 0;
        for (int ry = 0; ry < 200; ry++) {
            for (int rx = 0; rx < 200; rx++) {
                int xi = i * 2 + rx;
                if (1 <= xi + ry && xi + ry <= w) {
                    correct += input_buf(xi + ry) + in2_buf(xi);
                }
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
    if (halved_index_max_pool() != 0) {
        return 1;
    }
    if (where_on_sum_bounds_each_term() != 0) {
        return 1;
    }
    if (where_on_compound_index_bounds_its_parts() != 0) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
