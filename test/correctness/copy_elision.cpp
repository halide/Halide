#include "Halide.h"
#include "test/common/check_call_graphs.h"

#include <stdio.h>
#include <map>

namespace {

using std::map;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

int test_0() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    // TODO(psuriana): Copy elision make the loop nest of initial and update
    // stage of f not be in the same loop nest, since now the bound of f
    // is affected by the bound of g
    f(x, y) = x + y;
    f(x, y) += 5;
    g(x, y) = f(x, y);
    Func wrapper = f.in(g).compute_root();
    f.compute_at(wrapper, x);

    // TODO(psuriana): Also need to check the IR calls

    Buffer<int> im = g.realize(10, 10);
    auto func = [](int x, int y) {
        return x + y + 5;
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int test_1() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y + 1;
    g(x, y) = f(x, y);

    f.compute_root();

    Buffer<int> im = g.realize(10, 10);
    auto func = [](int x, int y) {
        return x + y + 1;
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int test_2() {
    Func f("f"), g("g"), out("out");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(x, y);
    out(x, y) = g(x, y) + 2;

    f.compute_at(g, x);
    g.compute_at(out, y);

    Buffer<int> im = out.realize(16, 16);
    auto func = [](int x, int y) {
        return x + y + 2;
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int test_3() {
    Func tile("tile"), output("output");
    Var x("x"), y("y"), tx("tx"), ty("ty");

    tile(x, y) = {x + y, x};
    output(x, y) = tile(x, y);

    output.bound(x, 0, 120);
    output.bound(y, 0, 120);
    output.tile(x, y, tx, ty, x, y, 8, 8, TailStrategy::GuardWithIf);

    tile.compute_at(output, tx);
    tile.tile(x, y, tx, ty, x, y, 4, 4, TailStrategy::ShiftInwards);

    Realization rn = output.realize(120, 120);
    Buffer<int> im1(rn[0]);
    Buffer<int> im2(rn[1]);

    auto func1 = [](int x, int y) {
        return x + y;
    };
    if (check_image(im1, func1)) {
        return -1;
    }
    auto func2 = [](int x, int y) {
        return x;
    };
    if (check_image(im2, func2)) {
        return -1;
    }
    return 0;
}

int test_4() {
    Func tile("tile"), f("f"), output("output");
    Var x("x"), y("y"), tx("tx"), ty("ty");

    tile(x, y) = {x + y, x};
    f(x, y) = tile(x, y);
    output(x, y) = {f(x, y)[0], f(x, y)[1] + 2};

    output.bound(x, 0, 120);
    output.bound(y, 0, 120);

    f.compute_root();
    f.tile(x, y, tx, ty, x, y, 8, 8, TailStrategy::GuardWithIf);
    tile.compute_at(f, tx);
    tile.tile(x, y, tx, ty, x, y, 16, 16, TailStrategy::ShiftInwards);

    Realization rn = output.realize(120, 120);
    Buffer<int> im1(rn[0]);
    Buffer<int> im2(rn[1]);

    auto func1 = [](int x, int y) {
        return x + y;
    };
    if (check_image(im1, func1)) {
        return -1;
    }
    auto func2 = [](int x, int y) {
        return x + 2;
    };
    if (check_image(im2, func2)) {
        return -1;
    }
    return 0;
}

int test_5() {
    Func f("f"), g("g"), out("out");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(x, y);
    out(x, y) = g(x, y);

    f.compute_root();

    Buffer<int> im = out.realize(16, 16);
    auto func = [](int x, int y) {
        return x + y;
    };
    if (check_image(im, func)) {
        return -1;
    }

    return 0;
}

int test_6() {
    const int W = 1024, H = 512;
    Buffer<uint8_t> img(W, H);
    for (int32_t y = 0; y < H; y++) {
        for (int32_t x = 0; x < W; x++) {
            img(x, y) = x + y * W;
        }
    }

    ImageParam input(UInt(8), 2);

    Var x("x"), y("y"), tx("tx"), ty("ty");

    Func input_copy("input_copy"), output_copy("output_copy");
    Func output("output"), work("work");

    input_copy(x, y) = input(x, y);
    work(x, y) = input_copy(x, y) * 2;
    output(x, y) = work(x, y);
    output_copy(x, y) = output(x, y);

    const int tile_width = 256;
    const int tile_height = 128;

    output_copy
        .compute_root()
        .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);
    input_copy.compute_at(output_copy, tx);
    work.compute_at(output_copy, tx);
    output.compute_at(output_copy, tx);

    input.set(img);
    Buffer<uint8_t> out_img = output_copy.realize(W, H);
    out_img.for_each_element([&](int x, int y) {
            uint8_t expected = (x + y * W) * 2;
            if (out_img(x, y) != expected) {
                printf("out_img(%d, %d) = %d instead of %d\n", x, y, out_img(x, y), expected);
                abort();
            }
        });

    return 0;
}

int test_7() {
    const int W = 1024, H = 512;
    Buffer<uint8_t> img(W, H);
    for (int32_t y = 0; y < H; y++) {
        for (int32_t x = 0; x < W; x++) {
            img(x, y) = x + y * W;
        }
    }

    ImageParam input(UInt(8), 2);

    Var x("x"), y("y"), tx("tx"), ty("ty");

    Func input_copy("input_copy"), output_copy("output_copy");
    Func output("output"), work("work");

    input_copy(x, y) = input(x, y);
    work(x, y) = input_copy(x, y) * 2;
    output(x, y) = work(x, y);
    output_copy(x, y) = output(x, y);

    const int tile_width = 256;
    const int tile_height = 128;

    output_copy
        .compute_root()
        .tile(x, y, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp);

    Stage s = output_copy;
    s.set_dim_device_api(tx, DeviceAPI::HexagonDma);

    input_copy
        .compute_at(output_copy, tx)
        .copy_to_host();

    work.compute_at(output_copy, tx);

    output
        .compute_at(output_copy, tx)
        .copy_to_device();

    input.set(img);

    Target t = get_jit_target_from_environment();
    output_copy.compile_jit(t.with_feature(Target::HexagonDma));
    Buffer<uint8_t> out_img = output_copy.realize(W, H);

    out_img.for_each_element([&](int x, int y) {
            uint8_t expected = (x + y * W) * 2;
            if (out_img(x, y) != expected) {
                printf("out_img(%d, %d) = %d instead of %d\n", x, y, out_img(x, y), expected);
                abort();
            }
        });

    return 0;
}

int test_8() {
    Func f("f"), g("g"), out("out");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(x, y);
    out(x, y) = g(x, y) + 1;

    f.compute_root();
    g.compute_root();

    Buffer<int> out_img = out.realize(20, 20);
    out_img.for_each_element([&](int x, int y) {
            int expected = x + y + 1;
            if (out_img(x, y) != expected) {
                printf("out_img(%d, %d) = %d instead of %d\n", x, y, out_img(x, y), expected);
                abort();
            }
        });

    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    /*printf("Running copy elision test 0\n");
    if (test_0() != 0) {
        return -1;
    }

    printf("Running copy elision test 1\n");
    if (test_1() != 0) {
        return -1;
    }

    printf("Running copy elision test 2\n");
    if (test_2() != 0) {
        return -1;
    }

    printf("Running copy elision test 3\n");
    if (test_3() != 0) {
        return -1;
    }

    printf("Running copy elision test 4\n");
    if (test_4() != 0) {
        return -1;
    }

    printf("Running copy elision test 5\n");
    if (test_5() != 0) {
        return -1;
    }

    printf("Running copy elision test 6\n");
    if (test_6() != 0) {
        return -1;
    }*/

    /*printf("Running copy elision test 7\n");
    if (test_7() != 0) {
        return -1;
    }*/

    printf("Running copy elision test 8\n");
    if (test_8() != 0) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
