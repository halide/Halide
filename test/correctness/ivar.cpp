#include <cassert>
#include <stdio.h>
#include "Halide.h"
#include <iostream>

using namespace Halide;

uint8_t video_val(int32_t x, int32_t y, int32_t c, int32_t frame) {
    x = std::max(std::min(x, 320), 0);
    y = std::max(std::min(y, 320), 0);

    return x + y + c + frame;
}

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

// Imagine that this loads from a file, or tiled storage. Here we'll just fill in the data using the frame number.
extern "C" DLLEXPORT int get_video_frame(buffer_t *out) {
    if (!out->host) {
        return 0;
    }
    assert(out->host && out->elem_size == 1 && out->stride[0] == 1);
    for (int32_t frame = 0; frame < out->extent[3]; frame++) {
        for (int y = 0; y < out->extent[1]; y++) {
            for (int c = 0; c < out->extent[2]; c++) {
                uint8_t *dst = (uint8_t *)out->host + y * out->stride[1] +
                               c * out->stride[2] + frame * out->stride[3];
                for (int x = 0; x < out->extent[0]; x++) {
                    dst[x] = video_val(x + out->min[0], y + out->min[1], c + out->min[2], frame + out->min[3]);
                }
            }
        }
    }
    return 0;
}

uint8_t blurred_val(int32_t x, int32_t y, int32_t c, int32_t frame) {
    int sum = 0;
    for (int32_t y_off = -1; y_off < 2; y_off++) {
        for (int32_t x_off = -1; x_off < 2; x_off++) {
            sum += video_val(x + x_off, y + y_off, c, frame);
        }
    }
    return sum / 9;
}

int whythoff_cxx(int x, int y) {
    float psi = (float)(1 + sqrt(5.0f)) / 2.0f;
    if (x == 1) {
        return (int)floor(floor(psi * y) * psi);
    } else if (x == 2) {
        return (int)floor(floor(psi * y) * psi * psi);
    } else {
        return whythoff_cxx(x - 2, y) + whythoff_cxx(x - 1, y);
    }
}

int fact_cxx(int x) {
    int fact = 1;
    while (x > 1) {
        fact = fact * x;
        x--;
    }
    return fact;
}

void histogram_test(bool use_rfactor) {
    Var x("x"), y("y");
    IVar x_implicit("x_implicit"), y_implicit("y_implicit");
    Func input("input"), f("f"), g("g"), h("h"), output("output");

    Func hist_in("hist_in");
    hist_in(x, y) = cast<uint8_t>(x + 3 * x_implicit + 5 * (y + 3 * y_implicit)) & ~1;

    Var bin;
    Func histogram("histogram");
    RDom range(0, 3, 0, 3);
    histogram(hist_in(range.x, range.y)) += 1;

    if (use_rfactor) {
        Var yi("yi");
        Func inner = histogram.update(0).rfactor(range.y, yi);
        inner.compute_root().update(0).parallel(yi);
    }
    histogram.compute_root();

    output(x_implicit, y_implicit, bin) = histogram(bin);
    Image<int32_t> hists = output.realize(2, 2, 31);

    uint8_t input_data[2][2][3][3];
    for (int32_t x_i = 0; x_i < 2; x_i++) {
        for (int32_t y_i = 0; y_i < 2; y_i++) {
            for (int32_t x = 0; x < 3; x++) {
                for (int32_t y = 0; y < 3; y++) {
                    input_data[x_i][y_i][x][y] = (uint8_t)(x + 3 * x_i + 5 * (y + 3 * y_i)) & ~1;
                }
            }
        }
    }
    int32_t hists_cxx[2][2][31];
    memset(hists_cxx, 0, sizeof(hists_cxx));
    for (int32_t x_i = 0; x_i < 2; x_i++) {
        for (int32_t y_i = 0; y_i < 2; y_i++) {
            for (int32_t x = 0; x < 3; x++) {
                for (int32_t y = 0; y < 3; y++) {
                    hists_cxx[x_i][y_i][input_data[x_i][y_i][x][y]] += 1;
                }
            }
        }
    }

    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            for (int bin = 0; bin < 31; bin++) {
                assert(hists(x, y, bin) == hists_cxx[x][y][bin]);
            }
        }
    }
}

int main(int argc, char **argv) {
    // Implicit based input function used in pointwise function
    {
        Var x("x"), y("y");
        IVar x_implicit("x_implicit"), y_implicit("y_implicit");
        Func input("input"), f("f"), g("g"), h("h"), output("output");

        input(x, y) = x + y * 256;

        f() = input(x_implicit, y_implicit);

        g() = f() + 42;
        h(x, y) = input(x, y) * 2;
        output(x_implicit, y_implicit) = g() + h(x_implicit, y_implicit);

        Image<int32_t> result = output.realize(10, 10);
        for (int32_t y = 0; y < 10; y++) {
            for (int32_t x = 0; x < 10; x++) {
                assert(result(x, y) == (x + y * 256) * 3 + 42);
            }
        }
    }

    // Implicit based input function used in reduction
    histogram_test(false);

    // Implicit based input function used in reduction with rfactor
    histogram_test(true);

    // TODO: make test case where ivar breaks associativity and try
    // rfactor, may need to go in errors cases.

    // Implicit used in expression only
    {
        Var x("x");
        IVar y("y");
        Func whythoff("whythoff"), row("row");
        RDom r(0, 10);

        Expr psi = (float)(1 + sqrt(5.0f)) / 2.0f;

        row(x) = 0;
        row(r.x) = select(r.x == 1, cast<int32_t>(floor(floor(y * psi) * psi)),
                          select(r.x == 2, cast<int32_t>(floor(floor(y * psi) * psi * psi)),
                                 row(r.x - 2) + row(r.x - 1)));
        whythoff(x, y) = row(x);

        Image<int32_t> result = whythoff.realize(10, 10);
        for (int y = 1; y < 10; y++) {
            for (int x = 1; x < 10; x++) {
                assert(result(x, y) == whythoff_cxx(x, y));
            }
        }
    }

    // Implicit used in where clause of RDom
    {
        Var k("k");
        IVar n("n");
        RDom rk(1, 10);
        rk.where(rk.x <= n);

        Func fact("fact");
        fact(k) = 1;
        fact(rk.x) = rk.x * fact(rk.x - 1);

        Func pascal("pascal");
        pascal(k) = 0;
        pascal(0) = 1;
        pascal(rk.x) = fact(n) / (fact(rk.x) * fact(n - rk.x));

        Func pascal_unwrap("pascal_wrap");
        pascal_unwrap(k, n) = pascal(k);

        Image<int32_t> result = pascal_unwrap.realize(10, 10);
        for (int y = 0; y < 10; y++) {
            for (int x = 0; x <= y; x++) {
                assert(result(x, y) == fact_cxx(y) / (fact_cxx(x) * fact_cxx(y - x)));
            }
        }
    }

    // Implicit with Var::outermost() used in scheduling
    {
        Var x("x"), y("y");
        IVar w("w");

        Func top("top");
        Func middle("middle");
        Func f("f"), g("g");
        Func common("common");
  
        common(x, y) = w * (x + y);
        f(x, y) = common(x, y) *.5f;
        g(x, y) = common(x, y) *2.0f;

        middle(x, y) = f(x, y) + g(x, y);
        top(x, y, w) = middle(x, y);

        f.compute_at(middle, y);
        g.compute_at(middle, y);
        middle.compute_at(top, x);
        common.compute_at(middle, Var::outermost());

        // TODO: convert to an assertion
        top.print_loop_nest();
        top.compile_to_lowered_stmt("/tmp/top.stmt", { });
        Image<float> result = top.realize(3, 3, 3);
    }

    // Implicit used with define_extern
    {
        Var x, y, c;
        IVar frame("frame");
        Func video_source("video_source");
        video_source.define_extern("get_video_frame",
                                   std::vector<ExternFuncArgument>(),
                                   UInt(8), 4);

        Func input("input");
        input(x, y, c) = cast<uint16_t>(video_source(x, y, c, frame));

        Func blur_x("blur_x");
        blur_x(x, y, c) = input(x - 1, y, c) + input(x, y, c) + input(x + 1, y, c);
        Func blur_y("blur_y");
        blur_y(x, y, c) = blur_x(x, y - 1, c) + blur_x(x, y, c) + blur_x(x, y + 1, c);

        Func blurred_frames("blurred_frames");
        blurred_frames(_, frame) = cast<uint8_t>(blur_y(_) / 9);

        Image<uint8_t> result = blurred_frames.realize(320, 320, 3, 4);

        for (int32_t frame = 0; frame < 4; frame++) {
            for (int32_t c = 0; c < 3; c++) {
                for (int32_t y = 0; y < 4; y++) {
                    for (int32_t x = 0; x < 4; x++) {
                        assert(result(x, y, c, frame) == blurred_val(x, y, c, frame));
                    }
                }
            }
        }
    }

    // IVar used with implciit argument deduction
    {
        Var x, y;
        IVar tx("tx"), ty("ty");

        Func f("f");
        f(x, y) = x * y + (tx * 256) + (ty * 1024);

        Func g("g");
        g(x, _) = f(x, _);
        Func h("h");
        h(_) = g(_);
        Func i("i");
        i(_, tx, ty) = g(_);

        Image<int32_t> result = i.realize(16, 16, 2, 2);


        for (int32_t ty = 0; ty < 2; ty++) {
            for (int32_t tx = 0; tx < 2; tx++) {
                for (int32_t y = 0; y < 16; y++) {
                    for (int32_t x = 0; x < 16; x++) {
                        assert(result(x, y, tx, ty) == x * y + (tx * 256) + (ty * 1024));
                    }
                }
            }
        }
    }

    // Ivar used with rfactor
    {
      
    }

    printf("Success!\n");
    return 0;
}
