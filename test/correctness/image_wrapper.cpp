#include "Halide.h"
#include "check_call_graphs.h"
#include <gtest/gtest.h>

#include <cstdio>
#include <map>

namespace {

using std::map;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

namespace {
class ImageWrapperTest : public ::testing::Test {};
}  // namespace

TEST_F(ImageWrapperTest, CallingWrapperNoOp) {
    Var x("x"), y("y");

    {
        ImageParam img(Int(32), 2);
        Func f("f");
        f(x, y) = img(x, y);

        // Calling wrap on the same ImageParam for the same Func multiple times should
        // return the same wrapper
        Func wrapper = img.in(f);
        for (int i = 0; i < 5; ++i) {
            Func temp = img.in(f);
            EXPECT_EQ(wrapper.name(), temp.name());
        }
    }

    {
        ImageParam img(Int(32), 2);
        Func f("f");
        f(x, y) = img(x, y);

        // Should return the same global wrapper
        Func wrapper1 = img.in();
        Func wrapper2 = img.in();
        EXPECT_EQ(wrapper1.name(), wrapper2.name());
    }

    {
        ImageParam img(Int(32), 2);
        Func e("e"), f("f"), g("g"), h("h");
        e(x, y) = img(x, y);
        f(x, y) = img(x, y);
        g(x, y) = img(x, y);
        h(x, y) = img(x, y);

        Func wrapper1 = img.in({e, f, g});
        Func wrapper2 = img.in({g, f, e});
        EXPECT_EQ(wrapper1.name(), wrapper2.name());
    }
}

TEST_F(ImageWrapperTest, FuncWrapper) {
    Func source("source"), g("g");
    Var x("x"), y("y");

    source(x) = x;
    ImageParam img(Int(32), 1, "img");
    Buffer<int> buf = source.realize({200});
    img.set(buf);

    g(x, y) = img(x);

    Func wrapper = img.in(g).compute_root();
    Func img_f = img;
    img_f.compute_root();

    // Check the call graphs.
    // Expect 'g' to call 'wrapper', 'wrapper' to call 'img_f', 'img_f' to call 'img'
    CallGraphs expected = {
        {g.name(), {wrapper.name()}},
        {wrapper.name(), {img_f.name()}},
        {img_f.name(), {img.name()}},
    };
    EXPECT_EQ(check_call_graphs(g, expected), 0);

    Buffer<int> im = g.realize({200, 200});
    EXPECT_EQ(check_image(im, [](int x, int y) { return x; }), 0);
}

TEST_F(ImageWrapperTest, MultipleFuncsSharingWrapper) {
    Func source("source"), g1("g1"), g2("g2"), g3("g3");
    Var x("x"), y("y");

    source(x) = x;
    ImageParam img(Int(32), 1, "img");
    Buffer<int> buf = source.realize({200});
    img.set(buf);

    g1(x, y) = img(x);
    g2(x, y) = img(x);
    g3(x, y) = img(x);

    Func im_wrapper = img.in({g1, g2}).compute_root();
    Func img_f = img;
    img_f.compute_root();

    // Check the call graphs.
    // Expect 'g1' and 'g2' to call 'im_wrapper', 'g3' to call 'img_f',
    // im_wrapper' to call 'img_f', 'img_f' to call 'img'
    Pipeline p({g1, g2, g3});
    CallGraphs expected = {
        {g1.name(), {im_wrapper.name()}},
        {g2.name(), {im_wrapper.name()}},
        {g3.name(), {img_f.name()}},
        {im_wrapper.name(), {img_f.name()}},
        {img_f.name(), {img.name()}},
    };
    EXPECT_EQ(check_call_graphs(p, expected), 0);

    Realization r = p.realize({200, 200});
    Buffer<int> img1 = r[0];
    Buffer<int> img2 = r[1];
    Buffer<int> img3 = r[2];
    auto func = [](int x, int y) { return x; };
    EXPECT_EQ(check_image(img1, func), 0);
    EXPECT_EQ(check_image(img2, func), 0);
    EXPECT_EQ(check_image(img3, func), 0);
}

TEST_F(ImageWrapperTest, GlobalWrapper) {
    Func source("source"), g("g"), h("h"), i("i");
    Var x("x"), y("y");

    source(x, y) = x + y;
    ImageParam img(Int(32), 2, "img");
    Buffer<int> buf = source.realize({200, 200});
    img.set(buf);

    g(x, y) = img(x, y);
    h(x, y) = g(x, y) + img(x, y);

    Var xi("xi"), yi("yi"), t("t");
    Func wrapper = img.in();
    Func img_f = img;
    img_f.compute_root();
    h.compute_root().tile(x, y, xi, yi, 16, 16).fuse(x, y, t).parallel(t);
    g.compute_at(h, yi);
    wrapper.compute_at(h, yi).tile(_0, _1, xi, yi, 8, 8).fuse(xi, yi, t).vectorize(t, 4);

    // Check the call graphs.
    // Expect 'g' to call 'wrapper', 'wrapper' to call 'img_f', 'img_f' to call 'img',
    // 'h' to call 'wrapper' and 'g'
    CallGraphs expected = {
        {h.name(), {g.name(), wrapper.name()}},
        {g.name(), {wrapper.name()}},
        {wrapper.name(), {img_f.name()}},
        {img_f.name(), {img.name()}},
    };
    EXPECT_EQ(check_call_graphs(h, expected), 0);

    Buffer<int> im = h.realize({200, 200});
    EXPECT_EQ(check_image(im, [](int x, int y) { return 2 * (x + y); }), 0);
}

TEST_F(ImageWrapperTest, UpdateDefinedAfterWrapper) {
    Func source("source"), g("g");
    Var x("x"), y("y");

    source(x, y) = x + y;
    ImageParam img(Int(32), 2, "img");
    Buffer<int> buf = source.realize({200, 200});
    img.set(buf);

    g(x, y) = img(x, y);

    Func wrapper = img.in(g);

    // Update of 'g' is defined after img.in(g) is called. g's updates should
    // still call img's wrapper.
    RDom r(0, 100, 0, 100);
    r.where(r.x < r.y);
    g(r.x, r.y) += 2 * img(r.x, r.y);

    Param<bool> param;

    Var xi("xi");
    RVar rxo("rxo"), rxi("rxi");
    g.specialize(param).vectorize(x, 8).unroll(x, 2).split(x, x, xi, 4).parallel(x);
    g.update(0).split(r.x, rxo, rxi, 2).unroll(rxi);
    Func img_f = img;
    img_f.compute_root();
    wrapper.compute_root().vectorize(_0, 8).unroll(_0, 2).split(_0, _0, xi, 4).parallel(_0);

    // Check the call graphs.
    // Expect initialization of 'g' to call 'wrapper' and its update to call
    // 'wrapper' and 'g', wrapper' to call 'img_f', 'img_f' to call 'img'
    CallGraphs expected = {
        {g.name(), {wrapper.name(), g.name()}},
        {wrapper.name(), {img_f.name()}},
        {img_f.name(), {img.name()}},
    };
    EXPECT_EQ(check_call_graphs(g, expected), 0);

    for (bool param_value : {false, true}) {
        param.set(param_value);
        Buffer<int> im = g.realize({200, 200});
        EXPECT_EQ(check_image(im, [](int x, int y) {
                      return 0 <= x && x <= 99 && 0 <= y && y <= 99 && x < y ? 3 * (x + y) : x + y;
                  }), 0);
    }
}

TEST_F(ImageWrapperTest, RdomWrapper) {
    Func source("source"), g("g"), result("result");
    Var x("x"), y("y");

    constexpr int W = 32;
    constexpr int H = 32;

    source(x, y) = x + y;
    ImageParam img(Int(32), 2, "img");
    Buffer<int> buf = source.realize({W, H});
    img.set(buf);

    g(x, y) = 10;
    g(x, y) += 2 * img(x, x);
    RDom r(0, W, 0, H);
    g(r.x, r.y) += 3 * img(r.y, r.y);

    // Make a global wrapper on 'g', so that we can schedule initialization
    // and the update on the same compute level at the global wrapper
    Func wrapper = g.in().compute_root();
    g.compute_at(wrapper, x);
    Func img_f = img;
    img_f.compute_root();

    // Check the call graphs.
    // Expect 'wrapper' to call 'g', initialization of 'g' to call nothing
    // and its update to call 'img_f' and 'g', 'img_f' to call 'img'
    CallGraphs expected = {
        {g.name(), {img_f.name(), g.name()}},
        {wrapper.name(), {g.name()}},
        {img_f.name(), {img.name()}},
    };
    EXPECT_EQ(check_call_graphs(wrapper, expected), 0);

    Buffer<int> im = wrapper.realize({W, H});
    EXPECT_EQ(check_image(im, [](int x, int y) { return 4 * x + 6 * y + 10; }), 0);
}

TEST_F(ImageWrapperTest, GlobalAndCustomWrapper) {
    Func source("source"), g("g"), result("result");
    Var x("x"), y("y");

    source(x) = x;
    ImageParam img(Int(32), 1, "img");
    Buffer<int> buf = source.realize({200});
    img.set(buf);

    g(x, y) = img(x);
    result(x, y) = img(x) + g(x, y);

    Func img_in_g = img.in(g).compute_at(g, x);
    Func img_wrapper = img.in().compute_at(result, y);
    Func img_f = img;
    img_f.compute_root();
    g.compute_at(result, y);

    // Check the call graphs.
    // Expect 'result' to call 'g' and 'img_wrapper', 'g' to call 'img_in_g',
    // 'img_wrapper' to call 'f', img_in_g' to call 'img_f', 'f' to call 'img'
    CallGraphs expected = {
        {result.name(), {g.name(), img_wrapper.name()}},
        {g.name(), {img_in_g.name()}},
        {img_wrapper.name(), {img_f.name()}},
        {img_in_g.name(), {img_f.name()}},
        {img_f.name(), {img.name()}},
    };
    EXPECT_EQ(check_call_graphs(result, expected), 0);

    Buffer<int> im = result.realize({200, 200});
    EXPECT_EQ(check_image(im, [](int x, int y) { return 2 * x; }), 0);
}

TEST_F(ImageWrapperTest, WrapperDependOnMutatedFunc) {
    Func source("sourceo"), f("f"), g("g"), h("h");
    Var x("x"), y("y");

    source(x, y) = x + y;
    ImageParam img(Int(32), 2, "img");
    Buffer<int> buf = source.realize({200, 200});
    img.set(buf);

    f(x, y) = img(x, y);
    g(x, y) = f(x, y);
    h(x, y) = g(x, y);

    Var xo("xo"), xi("xi");
    Func img_f = img;
    img_f.compute_root();
    f.compute_at(g, y).vectorize(x, 8);
    g.compute_root();
    Func img_in_f = img.in(f);
    Func g_in_h = g.in(h).compute_root();
    g_in_h.compute_at(h, y).vectorize(x, 8);
    img_in_f.compute_at(f, y).split(_0, xo, xi, 8);

    // Check the call graphs.
    // Expect 'h' to call 'g_in_h', 'g_in_h' to call 'g', 'g' to call 'f',
    // 'f' to call 'img_in_f', img_in_f' to call 'img_f', 'img_f' to call 'img'
    CallGraphs expected = {
        {h.name(), {g_in_h.name()}},
        {g_in_h.name(), {g.name()}},
        {g.name(), {f.name()}},
        {f.name(), {img_in_f.name()}},
        {img_in_f.name(), {img_f.name()}},
        {img_f.name(), {img.name()}},
    };
    EXPECT_EQ(check_call_graphs(h, expected), 0);

    Buffer<int> im = h.realize({200, 200});
    EXPECT_EQ(check_image(im, [](int x, int y) { return x + y; }), 0);
}

TEST_F(ImageWrapperTest, WrapperOnWrapper) {
    Func source("source"), g("g"), h("h");
    Var x("x"), y("y");

    source(x, y) = x + y;
    ImageParam img(Int(32), 2, "img");
    Buffer<int> buf = source.realize({200, 200});
    img.set(buf);

    g(x, y) = img(x, y) + img(x, y);
    Func img_in_g = img.in(g).compute_root();
    Func img_in_img_in_g = img.in(img_in_g).compute_root();
    h(x, y) = g(x, y) + img(x, y) + img_in_img_in_g(x, y);

    Func img_f = img;
    img_f.compute_root();
    g.compute_root();
    Func img_in_h = img.in(h).compute_root();
    Func g_in_h = g.in(h).compute_root();

    // Check the call graphs.
    CallGraphs expected = {
        {h.name(), {img_in_h.name(), g_in_h.name(), img_in_img_in_g.name()}},
        {img_in_h.name(), {img_f.name()}},
        {g_in_h.name(), {g.name()}},
        {g.name(), {img_in_g.name()}},
        {img_in_g.name(), {img_in_img_in_g.name()}},
        {img_in_img_in_g.name(), {img_f.name()}},
        {img_f.name(), {img.name()}},
    };
    EXPECT_EQ(check_call_graphs(h, expected), 0);

    Buffer<int> im = h.realize({200, 200});
    EXPECT_EQ(check_image(im, [](int x, int y) { return 4 * (x + y); }), 0);
}

TEST_F(ImageWrapperTest, WrapperOnRdomPredicate) {
    Func source("source"), g("g"), h("h");
    Var x("x"), y("y");

    source(x, y) = x + y;
    ImageParam img(Int(32), 2, "img");
    Buffer<int> buf = source.realize({200, 200});
    img.set(buf);

    g(x, y) = 10;
    h(x, y) = 5;

    RDom r(0, 100, 0, 100);
    r.where(img(r.x, r.y) + h(r.x, r.y) < 50);
    g(r.x, r.y) += h(r.x, r.y);

    Func h_wrapper = h.in().store_root().compute_at(g, r.y);
    Func img_in_g = img.in(g).compute_at(g, r.x);
    Func img_f = img;
    img_f.compute_root();
    h.compute_root();

    // Check the call graphs.
    // Expect 'g' to call nothing, update of 'g' to call 'g', img_in_g', and 'h_wrapper',
    // 'img_in_g' to call 'img_f', 'img_f' to call 'img', 'h_wrapper' to call 'h',
    // 'h' to call nothing
    CallGraphs expected = {
        {g.name(), {g.name(), img_in_g.name(), h_wrapper.name()}},
        {img_in_g.name(), {img_f.name()}},
        {img_f.name(), {img.name()}},
        {h_wrapper.name(), {h.name()}},
        {h.name(), {}},
    };
    EXPECT_EQ(check_call_graphs(g, expected), 0);

    Buffer<int> im = g.realize({200, 200});
    EXPECT_EQ(check_image(im, [](int x, int y) {
                  return 0 <= x && x <= 99 && 0 <= y && y <= 99 && x + y + 5 < 50 ? 15 : 10;
              }), 0);
}

TEST_F(ImageWrapperTest, TwoFoldWrapper) {
    Func source("source"), img_in_output_in_output, img_in_output, output("output");
    Var x("x"), y("y");

    source(x, y) = 2 * x + 3 * y;
    ImageParam img(Int(32), 2, "img");
    Buffer<int> buf = source.realize({1024, 1024});
    img.set(buf);

    Func img_f = img;
    img_f.compute_root();

    output(x, y) = img(y, x);

    Var xi("xi"), yi("yi");
    output.tile(x, y, xi, yi, 8, 8);

    img_in_output = img.in(output).compute_at(output, x).vectorize(_0).unroll(_1);
    img_in_output_in_output = img_in_output.in(output).compute_at(output, x).unroll(_0).unroll(_1);

    // Check the call graphs.
    CallGraphs expected = {
        {output.name(), {img_in_output_in_output.name()}},
        {img_in_output_in_output.name(), {img_in_output.name()}},
        {img_in_output.name(), {img_f.name()}},
        {img_f.name(), {img.name()}},
    };
    EXPECT_EQ(check_call_graphs(output, expected), 0);

    Buffer<int> im = output.realize({1024, 1024});
    EXPECT_EQ(check_image(im, [](int x, int y) { return 3 * x + 2 * y; }), 0);
}

TEST_F(ImageWrapperTest, MultiFoldsWrapper) {
    Func source("source"), img_in_g_in_g, img_in_g, img_in_g_in_g_in_h, img_in_g_in_g_in_h_in_h, g("g"), h("h");
    Var x("x"), y("y");

    source(x, y) = 2 * x + 3 * y;
    ImageParam img(Int(32), 2, "img");
    Buffer<int> buf = source.realize({1024, 1024});
    img.set(buf);

    Func img_f = img;
    img_f.compute_root();

    g(x, y) = img(y, x);

    Var xi("xi"), yi("yi");
    g.compute_root().tile(x, y, xi, yi, 8, 8);

    img_in_g = img.in(g).compute_root().tile(_0, _1, xi, yi, 8, 8).vectorize(xi).unroll(yi);
    img_in_g_in_g = img_in_g.in(g).compute_root().tile(_0, _1, xi, yi, 8, 8).unroll(xi).unroll(yi);

    h(x, y) = img_in_g_in_g(y, x);
    img_in_g_in_g_in_h = img_in_g_in_g.in(h).compute_at(h, x).vectorize(_0).unroll(_1);
    img_in_g_in_g_in_h_in_h = img_in_g_in_g_in_h.in(h).compute_at(h, x).unroll(_0).unroll(_1);
    h.compute_root().tile(x, y, xi, yi, 8, 8);

    Pipeline p({g, h});
    CallGraphs expected = {
        {g.name(), {img_in_g_in_g.name()}},
        {img_in_g_in_g.name(), {img_in_g.name()}},
        {img_in_g.name(), {img_f.name()}},
        {img_f.name(), {img.name()}},
        {h.name(), {img_in_g_in_g_in_h_in_h.name()}},
        {img_in_g_in_g_in_h_in_h.name(), {img_in_g_in_g_in_h.name()}},
        {img_in_g_in_g_in_h.name(), {img_in_g_in_g.name()}},
    };
    EXPECT_EQ(check_call_graphs(p, expected), 0);

    Realization r = p.realize({1024, 1024});
    Buffer<int> img_g = r[0];
    Buffer<int> img_h = r[1];
    auto func = [](int x, int y) { return 3 * x + 2 * y; };
    EXPECT_EQ(check_image(img_g, func), 0);
    EXPECT_EQ(check_image(img_h, func), 0);
}

}  // namespace
