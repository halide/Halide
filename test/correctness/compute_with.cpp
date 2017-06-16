#include "Halide.h"
#include "test/common/check_call_graphs.h"

#include <stdio.h>
#include <map>

using std::map;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

int split_test() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);
        im_ref = h.realize(200, 200);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);

        f.compute_root();
        g.compute_root();

        Var xo("xo"), xi("xi");
        f.split(x, xo, xi, 7);
        g.split(x, xo, xi, 7);
        g.compute_with(f, xo, AlignStrategy::AlignEnd);
        im = h.realize(200, 200);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int fuse_updates_test() {
    int size = 100;
    int split_size = 7;
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        g(x, y) = x - y;
        h(x, y) = x * y;
        f(x) = x;
        RDom r1(0, size/2, 0, size/2);
        r1.where(r1.x*r1.x + r1.y*r1.y < 5000);
        f(x) += g(r1.x, r1.y) * h(r1.x, r1.y);
        RDom r2(0, size, 0, size);
        f(x) -= h(r2.x, r2.y);
        im_ref = f.realize(size);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        g(x, y) = x - y;
        h(x, y) = x * y;
        f(x) = x;
        RDom r1(0, size/2, 0, size/2);
        r1.where(r1.x*r1.x + r1.y*r1.y < 5000);
        f(x) += g(r1.x, r1.y) * h(r1.x, r1.y);
        RDom r2(0, size, 0, size);
        f(x) -= h(r2.x, r2.y);

        Var xo("xo"), xi("xi");
        f.split(x, xo, xi, split_size, TailStrategy::GuardWithIf);
        f.update(0).split(x, xo, xi, split_size, TailStrategy::GuardWithIf);
        f.update(1).split(x, xo, xi, split_size, TailStrategy::GuardWithIf);
        f.update(0).compute_with(f, xo);
        f.update(1).compute_with(f.update(0), xo);

        g.compute_at(f, xo);
        h.compute_at(f, xi);

        im = f.realize(size);
    }

    auto func = [im_ref](int x) {
        return im_ref(x);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int fuse_test() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y"), z("z");
        Func f("f"), g("g"), h("h");

        f(x, y, z) = x + y + z;
        g(x, y, z) = x - y + z;
        h(x, y, z) = f(x + 2, y - 1, z + 3) + g(x - 5, y - 6, z + 2);
        im_ref = h.realize(100, 100, 100);
    }

    {
        Var x("x"), y("y"), z("z"), t("t");
        Func f("f"), g("g"), h("h");

        f(x, y, z) = x + y + z;
        g(x, y, z) = x - y + z;
        h(x, y, z) = f(x + 2, y - 1, z + 3) + g(x - 5, y - 6, z + 2);

        f.compute_root();
        g.compute_root();

        f.fuse(x, y, t).parallel(t);
        g.fuse(x, y, t).parallel(t);
        g.compute_with(f, t);
        im = h.realize(100, 100, 100);
    }

    auto func = [im_ref](int x, int y, int z) {
        return im_ref(x, y, z);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int multiple_fuse_group_test() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h"), p("p"), q("q");

        f(x, y) = x + y;
        f(x, y) += y;
        g(x, y) = 10;
        g(x, y) += x - y;
        h(x, y) = 0;
        RDom r(0, 39, 50, 77);
        h(r.x, r.y) -= r.x + r.y;
        h(r.x, r.y) += r.x * r.x;
        h(x, y) += f(x, y) + g(x, y);
        p(x, y) = x + 2;
        q(x, y) = h(x, y) + 2 + p(x, y);
        im_ref = q.realize(200, 200);
    }

    {
        Var x("x"), y("y"), t("t");
        Func f("f"), g("g"), h("h"), p("p"), q("q");

        f(x, y) = x + y;
        f(x, y) += y;
        g(x, y) = 10;
        g(x, y) += x - y;
        h(x, y) = 0;
        RDom r(0, 39, 50, 77);
        h(r.x, r.y) -= r.x + r.y;
        h(r.x, r.y) += r.x * r.x;
        h(x, y) += f(x, y) + g(x, y);
        p(x, y) = x + 2;
        q(x, y) = h(x, y) + 2 + p(x, y);

        f.compute_root();
        g.compute_root();
        h.compute_root();
        p.compute_root();

        p.fuse(x, y, t).parallel(t);
        h.fuse(x, y, t).parallel(t);
        h.compute_with(p, t);

        h.update(1).compute_with(h.update(0), r.x);
        g.update(0).compute_with(g, x);
        f.update(0).compute_with(g, y);
        f.compute_with(g, x);

        im = q.realize(200, 200);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int multiple_outputs_test() {
    const int f_size = 3;
    const int g_size = 4;
    Buffer<int> f_im(f_size, f_size), g_im(g_size, g_size);
    Buffer<int> f_im_ref(f_size, f_size), g_im_ref(g_size, g_size);

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), input("q");

        input(x, y) = x + y + 1;
        f(x, y) = 100 - input(x, y);
        g(x, y) = x + input(x, y);
        f.realize(f_im_ref);
        g.realize(g_im_ref);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), input("input");

        input(x, y) = x + y + 1;
        f(x, y) = 100 - input(x, y);
        g(x, y) = x + input(x, y);

        input.compute_at(f, y);
        g.compute_with(f, y);

        Pipeline({f, g}).realize({f_im, g_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return -1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return -1;
    }

    return 0;
}

int multiple_outputs_test_with_update() {
    const int f_size = 3;
    const int g_size = 4;
    Buffer<int> f_im(f_size, f_size), g_im(g_size, g_size);
    Buffer<int> f_im_ref(f_size, f_size), g_im_ref(g_size, g_size);

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), input("q");

        input(x, y) = x + y + 1;
        f(x, y) = 10;
        f(x, y) += 100 - input(x, y);
        g(x, y) = x + input(x, y);
        f.realize(f_im_ref);
        g.realize(g_im_ref);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), input("input");

        input(x, y) = x + y + 1;
        f(x, y) = 10;
        f(x, y) += 100 - input(x, y);
        g(x, y) = x + input(x, y);

        input.compute_at(f, y);
        f.update(0).compute_with(f, x);
        g.compute_with(f, y);

        Pipeline({f, g}).realize({f_im, g_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return -1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return -1;
    }

    return 0;
}

int skip_test_1() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1);
        im_ref = h.realize(200, 200);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1);

        f.compute_root();
        g.compute_root();
        g.compute_with(f, x);
        im = h.realize(200, 200);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int skip_test_2() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = g(x - 1, y + 1);
        im_ref = h.realize(200, 200);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = g(x - 1, y + 1);

        f.compute_root();
        g.compute_root();
        g.compute_with(f, x);
        im = h.realize(200, 200);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int fuse_compute_at_test() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h"), p("p"), q("q"), r("r");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);
        p(x, y) = h(x, y) + 2;
        q(x, y) = x * y;
        r(x, y) = p(x, y - 1) + q(x - 1, y);
        im_ref = r.realize(200, 200);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h"), p("p"), q("q"), r("r");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);
        p(x, y) = h(x, y) + 2;
        q(x, y) = x * y;
        r(x, y) = p(x, y - 1) + q(x - 1, y);

        f.compute_at(h, y);
        g.compute_at(h, y);
        h.compute_at(p, y);
        p.compute_root();
        q.compute_root();
        q.compute_with(p, x);

        Var xo("xo"), xi("xi");
        f.split(x, xo, xi, 7);
        g.split(x, xo, xi, 7);
        g.compute_with(f, xo);
        im = r.realize(200, 200);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int double_split_fuse_test() {
    Buffer<int> im_ref, im;
    {
        Func f("f"), g("g");
        Var x("x"), y("y"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");

        f(x, y) = x + y;
        f(x, y) += 2;
        g(x, y) = f(x, y) + 10;
        im_ref = g.realize(200, 200);
    }

    {
        Func f("f"), g("g");
        Var x("x"), y("y"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi"), t("t");

        f(x, y) = x + y;
        f(x, y) += 2;
        g(x, y) = f(x, y) + 10;

        f.split(x, xo, xi, 37, TailStrategy::GuardWithIf);
        f.update(0).split(x, xo, xi, 37, TailStrategy::GuardWithIf);
        f.split(xo, xoo, xoi, 5, TailStrategy::GuardWithIf);
        f.update(0).split(xo, xoo, xoi, 5, TailStrategy::GuardWithIf);
        f.fuse(xoi, xi, t);
        f.update(0).fuse(xoi, xi, t);
        f.compute_at(g, y);
        f.update(0).compute_with(f, t);

        im = g.realize(200, 200);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int rowsum_test() {
    const int size = 100;
    Buffer<int> rowsum_im(size), g_im(size, size);
    Buffer<int> rowsum_im_ref(size), g_im_ref(size, size);

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), rowsum("rowsum");

        f(x, y) = x + y;
        g(x, y) = f(x, y);
        RDom r(0, 100);
        rowsum(y) += f(r, y);

        g.realize(g_im_ref);
        rowsum.realize(rowsum_im_ref);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), rowsum("rowsum");

        f(x, y) = x + y;
        g(x, y) = f(x, y);
        RDom r(0, 100);
        rowsum(y) += f(r, y);

        rowsum.compute_with(g, y);
        rowsum.update(0).compute_with(rowsum, y);

        Pipeline({g, rowsum}).realize({g_im, rowsum_im});
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return -1;
    }

    auto rowsum_func = [rowsum_im_ref](int x, int y) {
        return rowsum_im_ref(x, y);
    };
    if (check_image(rowsum_im, rowsum_func)) {
        return -1;
    }

    return 0;
}

int rgb_yuv420_test() {
    // Somewhat approximating the behavior of rgb -> yuv420 (downsample by half in the u and v channels)
    const int size = 1024;
    Buffer<int> y_im(size, size), u_im(size/2, size/2), v_im(size/2, size/2);
    Buffer<int> y_im_ref(size, size), u_im_ref(size/2, size/2), v_im_ref(size/2, size/2);

    // Compute a random image
    Buffer<int> input(size, size, 3);
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            for (int c = 0; c < 3; c++) {
                input(x, y, c) = (rand() & 0x000000ff);
            }
        }
    }

    {
        Var x("x"), y("y"), z("z");
        Func y_part("y_part"), u_part("u_part"), v_part("v_part"), rgb("rgb"), rgb_x("rgb_x");

        Func clamped = BoundaryConditions::repeat_edge(input);
        rgb_x(x, y, z) = (clamped(x - 1, y, z) + 2*clamped(x, y, z) + clamped(x + 1, y, z));
        rgb(x, y, z) = (rgb_x(x, y - 1, z) + 2*rgb_x(x, y, z) + rgb_x(x, y - 1, z))/16;

        y_part(x, y) = ((66 * input(x, y, 0) + 129 * input(x, y, 1) +  25 * input(x, y, 2) + 128) >> 8) +  16;
        u_part(x, y) = (( -38 * rgb(2*x, 2*y, 0) -  74 * rgb(2*x, 2*y, 1) + 112 * rgb(2*x, 2*y, 2) + 128) >> 8) + 128;
        v_part(x, y) = (( 112 * rgb(2*x, 2*y, 0) -  94 * rgb(2*x, 2*y, 1) -  18 * rgb(2*x, 2*y, 2) + 128) >> 8) + 128;

        Pipeline({y_part, u_part, v_part}).realize({y_im_ref, u_im_ref, v_im_ref});
    }

    {
        Var x("x"), y("y"), z("z");
        Func y_part("y_part"), u_part("u_part"), v_part("v_part"), rgb("rgb"), rgb_x("rgb_x");

        Func clamped = BoundaryConditions::repeat_edge(input);
        rgb_x(x, y, z) = (clamped(x - 1, y, z) + 2*clamped(x, y, z) + clamped(x + 1, y, z));
        rgb(x, y, z) = (rgb_x(x, y - 1, z) + 2*rgb_x(x, y, z) + rgb_x(x, y - 1, z))/16;

        y_part(x, y) = ((66 * input(x, y, 0) + 129 * input(x, y, 1) +  25 * input(x, y, 2) + 128) >> 8) +  16;
        u_part(x, y) = (( -38 * rgb(2*x, 2*y, 0) -  74 * rgb(2*x, 2*y, 1) + 112 * rgb(2*x, 2*y, 2) + 128) >> 8) + 128;
        v_part(x, y) = (( 112 * rgb(2*x, 2*y, 0) -  94 * rgb(2*x, 2*y, 1) -  18 * rgb(2*x, 2*y, 2) + 128) >> 8) + 128;

        Var xi("xi"), yi("yi");
        y_part.tile(x, y, xi, yi, 16, 2, TailStrategy::RoundUp);
        u_part.tile(x, y, xi, yi, 8, 1, TailStrategy::RoundUp);
        v_part.tile(x, y, xi, yi, 8, 1, TailStrategy::RoundUp);

        y_part.unroll(yi);
        y_part.vectorize(xi);
        u_part.vectorize(xi);
        v_part.vectorize(xi);

        u_part.compute_with(y_part, x);
        v_part.compute_with(u_part, x);

        Expr width = v_part.output_buffer().width();
        Expr height = v_part.output_buffer().height();
        width = (width/8)*8;

        u_part.bound(x, 0, width).bound(y, 0, height);
        v_part.bound(x, 0, width).bound(y, 0, height);
        y_part.bound(x, 0, 2*width).bound(y, 0, 2*height);

        rgb_x.fold_storage(y, 4);
        rgb_x.store_root();
        rgb_x.compute_at(y_part, y).vectorize(x, 8);
        rgb.compute_at(y_part, y).vectorize(x, 8);

        Pipeline({y_part, u_part, v_part}).realize({y_im, u_im, v_im});
    }

    auto y_func = [y_im_ref](int x, int y) {
        return y_im_ref(x, y);
    };
    if (check_image(y_im, y_func)) {
        return -1;
    }

    auto u_func = [u_im_ref](int x, int y) {
        return u_im_ref(x, y);
    };
    if (check_image(u_im, u_func)) {
        return -1;
    }

    auto v_func = [v_im_ref](int x, int y) {
        return v_im_ref(x, y);
    };
    if (check_image(v_im, v_func)) {
        return -1;
    }

    return 0;
}

int vectorize_test() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);
        im_ref = h.realize(200, 200);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);

        f.compute_root();
        g.compute_root();

        Var xo("xo"), xi("xi");
        f.split(x, xo, xi, 7);
        g.split(x, xo, xi, 7);
        f.vectorize(xi);
        g.vectorize(xi);
        g.compute_with(f, xi);
        im = h.realize(200, 200);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int some_are_skipped_test() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h"), p("p");

        f(x, y) = x + y;
        g(x, y) = x - y;
        p(x, y) = x * y;
        h(x, y) = f(x, y) + g(x + 2, y - 2);
        h(x, y) += f(x - 1, y + 1) + p(x, y);
        im_ref = h.realize(200, 200);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h"), p("p");

        f(x, y) = x + y;
        g(x, y) = x - y;
        p(x, y) = x * y;
        h(x, y) = f(x, y) + g(x + 2, y - 2);
        h(x, y) += f(x - 1, y + 1) + p(x, y);

        f.compute_at(h, y);
        g.compute_at(h, y);
        p.compute_at(h, y);

        p.compute_with(f, x);
        g.compute_with(f, x);
        im = h.realize(200, 200);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int multiple_outputs_on_gpu_test() {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("No GPU feature enabled in target. Skipping test\n");
        return 0;
    }

    const int f_size = 550;
    const int g_size = 110;
    Buffer<int> f_im(f_size, f_size), g_im(g_size, g_size);
    Buffer<int> f_im_ref(f_size, f_size), g_im_ref(g_size, g_size);

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), input("q");

        input(x, y) = x + y + 1;
        f(x, y) = 100 - input(x, y);
        g(x, y) = x + input(x, y);
        f.realize(f_im_ref);
        g.realize(g_im_ref);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), input("input");

        input(x, y) = x + y + 1;
        f(x, y) = 100 - input(x, y);
        g(x, y) = x + input(x, y);

        input.compute_root();
        f.compute_root().gpu_tile(x, y, 8, 8);
        g.compute_root().gpu_tile(x, y, 8, 8);

        g.compute_with(f, Var::gpu_blocks());

        Realization r(f_im, g_im);
        Pipeline({f, g}).realize(r);
        r[0].copy_to_host();
        r[1].copy_to_host();
    }

    std::cout << "Checking f output\n";
    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return -1;
    }

    std::cout << "Checking g output\n";
    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return -1;
    }

    return 0;
}

int mixed_tile_factor_test() {
    const int size = 512;
    Buffer<int> f_im(size, size), g_im(size/2, size/2), h_im(size/2, size/2);
    Buffer<int> f_im_ref(size, size), g_im_ref(size/2, size/2), h_im_ref(size/2, size/2);

    // Compute a random image
    Buffer<int> A(size, size, 3);
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            for (int c = 0; c < 3; c++) {
                A(x, y, c) = (rand() & 0x000000ff);
            }
        }
    }

    {
        Var x("x"), y("y"), z("z");
        Func f("f_ref"), g("g_ref"), h("h_ref"), input("A_ref");

        input(x, y, z) = 2*A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2*x, 2*y, 1) + 2 * input(2*x, 2*y, 2);
        h(x, y) = input(2*x, 2*y, 2) + 3 * input(2*x, 2*y, 1);

        Pipeline({f, g, h}).realize({f_im_ref, g_im_ref, h_im_ref});
    }

    {
        Var x("x"), y("y"), z("z");
        Func f("f"), g("g"), h("h"), input("A");

        input(x, y, z) = 2*A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2*x, 2*y, 1) + 2 * input(2*x, 2*y, 2);
        h(x, y) = input(2*x, 2*y, 2) + 3 * input(2*x, 2*y, 1);

        Var xi("xi"), yi("yi");
        f.tile(x, y, xi, yi, 32, 16, TailStrategy::ShiftInwards);
        g.tile(x, y, xi, yi, 7, 9, TailStrategy::GuardWithIf);
        h.tile(x, y, xi, yi, 4, 16, TailStrategy::RoundUp);

        g.compute_with(f, yi);
        h.compute_with(g, yi);

        input.store_root();
        input.compute_at(f, y).vectorize(x, 8);

        Pipeline({f, g, h}).realize({f_im, g_im, h_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return -1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return -1;
    }

    auto h_func = [h_im_ref](int x, int y) {
        return h_im_ref(x, y);
    };
    if (check_image(h_im, h_func)) {
        return -1;
    }

    return 0;
}

int multi_tile_mixed_tile_factor_test() {
    const int size = 512;
    Buffer<int> f_im(size, size), g_im(size/2, size/2), h_im(size/2, size/2);
    Buffer<int> f_im_ref(size, size), g_im_ref(size/2, size/2), h_im_ref(size/2, size/2);

    // Compute a random image
    Buffer<int> A(size, size, 3);
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            for (int c = 0; c < 3; c++) {
                A(x, y, c) = (rand() & 0x000000ff);
            }
        }
    }

    {
        Var x("x"), y("y"), z("z");
        Func f("f_ref"), g("g_ref"), h("h_ref"), input("A_ref");

        input(x, y, z) = 2*A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2*x, 2*y, 1) + 2 * input(2*x, 2*y, 2);
        h(x, y) = input(2*x, 2*y, 2) + 3 * input(2*x, 2*y, 1);

        Pipeline({f, g, h}).realize({f_im_ref, g_im_ref, h_im_ref});
    }

    {
        Var x("x"), y("y"), z("z");
        Func f("f"), g("g"), h("h"), input("A");

        input(x, y, z) = 2*A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2*x, 2*y, 1) + 2 * input(2*x, 2*y, 2);
        h(x, y) = input(2*x, 2*y, 2) + 3 * input(2*x, 2*y, 1);

        Var xi("xi"), yi("yi");
        f.tile(x, y, xi, yi, 32, 16, TailStrategy::ShiftInwards);
        g.tile(x, y, xi, yi, 7, 9, TailStrategy::GuardWithIf);
        h.tile(x, y, xi, yi, 4, 16, TailStrategy::RoundUp);

        Var xii("xii"), yii("yii");
        f.tile(xi, yi, xii, yii, 8, 8, TailStrategy::ShiftInwards);
        g.tile(xi, yi, xii, yii, 16, 8, TailStrategy::GuardWithIf);
        h.tile(xi, yi, xii, yii, 4, 16, TailStrategy::GuardWithIf);

        g.compute_with(f, yii);
        h.compute_with(g, yii);

        input.store_root();
        input.compute_at(f, y).vectorize(x, 8);

        Pipeline({f, g, h}).realize({f_im, g_im, h_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return -1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return -1;
    }

    auto h_func = [h_im_ref](int x, int y) {
        return h_im_ref(x, y);
    };
    if (check_image(h_im, h_func)) {
        return -1;
    }

    return 0;
}

int only_some_are_tiled_test() {
    const int size = 512;
    Buffer<int> f_im(size, size), g_im(size/2, size/2), h_im(size/2, size/2);
    Buffer<int> f_im_ref(size, size), g_im_ref(size/2, size/2), h_im_ref(size/2, size/2);

    // Compute a random image
    Buffer<int> A(size, size, 3);
    for (int x = 0; x < size; x++) {
        for (int y = 0; y < size; y++) {
            for (int c = 0; c < 3; c++) {
                A(x, y, c) = (rand() & 0x000000ff);
            }
        }
    }

    {
        Var x("x"), y("y"), z("z");
        Func f("f_ref"), g("g_ref"), h("h_ref"), input("A_ref");

        input(x, y, z) = 2*A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2*x, 2*y, 1) + 2 * input(2*x, 2*y, 2);
        h(x, y) = input(2*x, 2*y, 2) + 3 * input(2*x, 2*y, 1);

        Pipeline({f, g, h}).realize({f_im_ref, g_im_ref, h_im_ref});
    }

    {
        Var x("x"), y("y"), z("z");
        Func f("f"), g("g"), h("h"), input("A");

        input(x, y, z) = 2*A(x, y, z) + 3;
        f(x, y) = input(x, y, 0) + 2 * input(x, y, 1);
        g(x, y) = input(2*x, 2*y, 1) + 2 * input(2*x, 2*y, 2);
        h(x, y) = input(2*x, 2*y, 2) + 3 * input(2*x, 2*y, 1);

        Var xi("xi"), yi("yi");
        f.tile(x, y, xi, yi, 32, 16, TailStrategy::ShiftInwards);

        g.compute_with(f, y);
        h.compute_with(g, y);

        input.store_root();
        input.compute_at(f, y).vectorize(x, 8);

        Pipeline({f, g, h}).realize({f_im, g_im, h_im});
    }

    auto f_func = [f_im_ref](int x, int y) {
        return f_im_ref(x, y);
    };
    if (check_image(f_im, f_func)) {
        return -1;
    }

    auto g_func = [g_im_ref](int x, int y) {
        return g_im_ref(x, y);
    };
    if (check_image(g_im, g_func)) {
        return -1;
    }

    auto h_func = [h_im_ref](int x, int y) {
        return h_im_ref(x, y);
    };
    if (check_image(h_im, h_func)) {
        return -1;
    }

    return 0;
}

int with_specialization_test() {
    Buffer<int> im_ref, im;
    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);
        im_ref = h.realize(200, 200);
    }

    {
        Var x("x"), y("y");
        Func f("f"), g("g"), h("h");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = f(x - 1, y + 1) + g(x + 2, y - 2);

        f.compute_root();
        g.compute_root();

        Param<bool> tile;
        Var xo("xo"), xi("xi");
        f.specialize(tile).split(x, xo, xi, 7);

        g.compute_with(f, y, AlignStrategy::AlignEnd);

        tile.set(true);
        im = h.realize(200, 200);
    }

    auto func = [im_ref](int x, int y) {
        return im_ref(x, y);
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    printf("Running split reorder test\n");
    if (split_test() != 0) {
        return -1;
    }

    printf("Running fuse test\n");
    if (fuse_test() != 0) {
        return -1;
    }

    printf("Running multiple fuse group test\n");
    if (multiple_fuse_group_test() != 0) {
        return -1;
    }

    printf("Running multiple outputs test\n");
    if (multiple_outputs_test() != 0) {
        return -1;
    }

    printf("Running multiple outputs with update test\n");
    if (multiple_outputs_test_with_update() != 0) {
        return -1;
    }

    printf("Running fuse updates test\n");
    if (fuse_updates_test() != 0) {
        return -1;
    }

    printf("Running skip test 1\n");
    if (skip_test_1() != 0) {
        return -1;
    }

    printf("Running skip test 2\n");
    if (skip_test_2() != 0) {
        return -1;
    }

    printf("Running fuse compute at test\n");
    if (fuse_compute_at_test() != 0) {
        return -1;
    }

    printf("Running double split fuse test\n");
    if (double_split_fuse_test() != 0) {
        return -1;
    }

    printf("Running rowsum test\n");
    if (rowsum_test() != 0) {
        return -1;
    }

    printf("Running rgb to yuv420 test\n");
    if (rgb_yuv420_test() != 0) {
        return -1;
    }

    printf("Running vectorize test\n");
    if (vectorize_test() != 0) {
        return -1;
    }

    printf("Running some are skipped test\n");
    if (some_are_skipped_test() != 0) {
        return -1;
    }

    printf("Running multiple outputs on gpu test\n");
    if (multiple_outputs_on_gpu_test() != 0) {
        return -1;
    }

    printf("Running mixed tile factor test\n");
    if (mixed_tile_factor_test() != 0) {
        return -1;
    }

    printf("Running multi tile mixed tile factor test\n");
    if (multi_tile_mixed_tile_factor_test() != 0) {
        return -1;
    }

    printf("Running only some are tiled test\n");
    if (only_some_are_tiled_test() != 0) {
        return -1;
    }

    printf("Running with specialization test\n");
    if (with_specialization_test() != 0) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
