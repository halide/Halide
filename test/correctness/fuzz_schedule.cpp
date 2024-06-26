#include "Halide.h"

using namespace Halide;

void check_blur_output(const Buffer<int> &out, const Buffer<int> &correct) {
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            if (out(x, y) != correct(x, y)) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct(x, y));
                exit(1);
            }
        }
    }
}

int main(int argc, char **argv) {
    // This test is for schedules that crash the compiler found via fuzzing that
    // are hard to otherwise reproduce. We don't need to check the output.

    Buffer<int> correct;
    {
        // An unscheduled instance to act as a reference output
        Func input("input");
        Func local_sum("local_sum");
        Func blurry("blurry");
        Var x("x"), y("y");
        input(x, y) = 2 * x + 5 * y;
        RDom r(-2, 5, -2, 5);
        local_sum(x, y) = 0;
        local_sum(x, y) += input(x + r.x, y + r.y);
        blurry(x, y) = cast<int32_t>(local_sum(x, y) / 25);
        correct = blurry.realize({32, 32});
    }

    // https://github.com/halide/Halide/issues/7851
    {
        Func input("input");
        Func local_sum("local_sum");
        Func blurry("blurry");
        Var x("x"), y("y");
        input(x, y) = 2 * x + 5 * y;
        RDom r(-2, 5, -2, 5);
        local_sum(x, y) = 0;
        local_sum(x, y) += input(x + r.x, y + r.y);
        blurry(x, y) = cast<int32_t>(local_sum(x, y) / 25);
        Var yo("yo"), yi("yi"), xo("xo"), xi("xi"), yo_x_f("yo_x_f"), yo_x_fo("yo_x_fo"), yo_x_fi("yo_x_fi");
        blurry.split(y, yo, yi, 2, TailStrategy::RoundUp).fuse(yo, x, yo_x_f).vectorize(yi).split(yo_x_f, yo_x_fo, yo_x_fi, 2, TailStrategy::Predicate).reorder(yo_x_fo, yo_x_fi, yi);
        input.split(y, yo, yi, 2, TailStrategy::PredicateStores).fuse(yo, x, yo_x_f).vectorize(yi).split(yo_x_f, yo_x_fo, yo_x_fi, 2, TailStrategy::Predicate).reorder(yo_x_fo, yo_x_fi, yi);
        blurry.store_root();
        input.compute_at(blurry, yi);
        Pipeline p({blurry});
        Buffer<int> buf = p.realize({32, 32});
        check_blur_output(buf, correct);
    }

    // https://github.com/halide/Halide/issues/7873
    {
        Func input("input");
        Func local_sum("local_sum");
        Func blurry("blurry");
        Var x("x"), y("y");
        RVar yryf;
        input(x, y) = 2 * x + 5 * y;
        RDom r(-2, 5, -2, 5, "rdom_r");
        local_sum(x, y) = 0;
        local_sum(x, y) += input(x + r.x, y + r.y);
        blurry(x, y) = cast<int32_t>(local_sum(x, y) / 25);
        Var xo, xi;
        local_sum.split(x, xo, xi, 4, TailStrategy::PredicateStores);
        local_sum.update(0).unscheduled();
        Pipeline p({blurry});
        Buffer<int> buf = p.realize({32, 32});
        check_blur_output(buf, correct);
    }

    // https://github.com/halide/Halide/issues/7872
    {
        Func input("input");
        Func local_sum("local_sum");
        Func blurry("blurry");
        Var x("x"), y("y");
        RVar yryf;
        input(x, y) = 2 * x + 5 * y;
        RDom r(-2, 5, -2, 5, "rdom_r");
        local_sum(x, y) = 0;
        local_sum(x, y) += input(x + r.x, y + r.y);
        blurry(x, y) = cast<int32_t>(local_sum(x, y) / 25);
        Var xo, xi;
        blurry.split(x, xo, xi, 2, TailStrategy::GuardWithIf);
        local_sum.store_at(blurry, y).compute_at(blurry, xi);
        Pipeline p({blurry});
        Buffer<int> buf = p.realize({32, 32});
        check_blur_output(buf, correct);
    }

    // https://github.com/halide/Halide/issues/7891
    {
        Func input("input");
        Func local_sum("local_sum");
        Func blurry("blurry");
        Var x("x"), y("y");
        input(x, y) = 2 * x + 5 * y;
        RDom r(-2, 5, -2, 5);
        local_sum(x, y) = 0;
        local_sum(x, y) += input(x + r.x, y + r.y);
        blurry(x, y) = cast<int32_t>(local_sum(x, y) / 25);
        Var yo, yi, xo, xi, xio, xii, xiio, xiii;
        blurry.split(y, yo, yi, 4, TailStrategy::Auto)
            .split(x, xo, xi, 1, TailStrategy::Auto)
            .split(xi, xio, xii, 4, TailStrategy::GuardWithIf)
            .split(xii, xiio, xiii, 1, TailStrategy::RoundUp);
        local_sum.compute_at(blurry, xiio);
        input.compute_at(blurry, xiio);
        input.store_root();
        Pipeline p({blurry});
        Buffer<int> buf = p.realize({32, 32});
        check_blur_output(buf, correct);
    }

    // https://github.com/halide/Halide/issues/7892
    {
        Func input("input");
        Func local_sum("local_sum");
        Func blurry("blurry");
        Var x("x"), y("y");
        RVar yryf;
        input(x, y) = 2 * x + 5 * y;
        RDom r(-2, 5, -2, 5, "rdom_r");
        local_sum(x, y) = 0;
        local_sum(x, y) += input(x + r.x, y + r.y);
        blurry(x, y) = cast<int32_t>(local_sum(x, y) / 25);
        Var xo, xi, xoo, xoi, yo, yi;
        local_sum.vectorize(x)
            .split(x, xo, xi, 2, TailStrategy::PredicateStores)
            .split(xo, xoo, xoi, 4, TailStrategy::RoundUp)
            .unroll(xoi);
        local_sum.update(0).unscheduled();
        Pipeline p({blurry});
        Buffer<int> buf = p.realize({32, 32});
        check_blur_output(buf, correct);
    }

    // https://github.com/halide/Halide/issues/7906
    {
        Func input("input");
        Func local_sum("local_sum");
        Func blurry("blurry");
        Var x("x"), y("y");
        input(x, y) = 2 * x + 5 * y;
        RDom r(-2, 5, -2, 5);
        local_sum(x, y) = 0;
        local_sum(x, y) += input(x + r.x, y + r.y);
        blurry(x, y) = cast<int32_t>(local_sum(x, y) / 25);
        Var yo, yi, x_yo_f;
        input.vectorize(y).split(y, yo, yi, 2, TailStrategy::ShiftInwards).unroll(x).fuse(x, yo, x_yo_f);
        blurry.compute_root();
        input.compute_at(blurry, x);
        Pipeline p({blurry});
        Buffer<int> buf = p.realize({32, 32});
        check_blur_output(buf, correct);
    }

    // https://github.com/halide/Halide/issues/7909
    {
        Func input("input");
        Func local_sum("local_sum");
        Func blurry("blurry");
        Var x("x"), y("y");
        input(x, y) = 2 * x + 5 * y;
        RDom r(-2, 5, -2, 5);
        local_sum(x, y) = 0;
        local_sum(x, y) += input(x + r.x, y + r.y);
        blurry(x, y) = cast<int32_t>(local_sum(x, y) / 25);
        Var yo, yi;
        blurry.split(y, yo, yi, 1, TailStrategy::Auto);
        local_sum.compute_at(blurry, yo);
        local_sum.store_root();
        input.compute_at(local_sum, x);
        input.store_root();
        Pipeline p({blurry});
        Buffer<int> buf = p.realize({32, 32});
        check_blur_output(buf, correct);
    }

    // https://github.com/halide/Halide/issues/8038
    {
        Func input("input");
        Func local_sum("local_sum");
        Func blurry("blurry");
        Var x("x"), y("y"), yi("yi"), yo("yo"), xi("xi"), xo("xo"), yofxi("yofxi"), yofxio("yofxio"), yofxii("yofxii"), yofxiifyi("yofxiifyi"), yofxioo("yofxioo"), yofxioi("yofxioi");
        input(x, y) = 2 * x + 5 * y;
        RDom r(-2, 5, -2, 5, "rdom_r");
        local_sum(x, y) = 0;
        local_sum(x, y) += input(x + r.x, y + r.y);
        blurry(x, y) = cast<int32_t>(local_sum(x, y) / 25);
        local_sum.split(y, yi, yo, 2, TailStrategy::GuardWithIf).split(x, xi, xo, 5, TailStrategy::Predicate).fuse(yo, xi, yofxi).split(yofxi, yofxio, yofxii, 8, TailStrategy::ShiftInwards).fuse(yofxii, yi, yofxiifyi).split(yofxio, yofxioo, yofxioi, 5, TailStrategy::ShiftInwards).vectorize(yofxiifyi).vectorize(yofxioi);
        local_sum.update(0).unscheduled();
        blurry.split(x, xo, xi, 5, TailStrategy::Auto);
        Pipeline p({blurry});
        auto buf = p.realize({32, 32});
        check_blur_output(buf, correct);
    }

    // https://github.com/halide/Halide/issues/7890
    {
        Func input("input");
        Func local_sum("local_sum");
        Func blurry("blurry");
        Var x("x"), y("y");
        RVar yryf;
        input(x, y) = 2 * x + 5 * y;
        RDom r(-2, 5, -2, 5, "rdom_r");
        local_sum(x, y) = 0;
        local_sum(x, y) += input(x + r.x, y + r.y);
        blurry(x, y) = cast<int32_t>(local_sum(x, y) / 25);

        Var yo, yi, xo, xi, u;
        blurry.split(y, yo, yi, 2, TailStrategy::Auto);
        local_sum.split(x, xo, xi, 4, TailStrategy::Auto);
        local_sum.update(0).split(x, xo, xi, 1, TailStrategy::Auto);
        local_sum.update(0).rfactor(r.x, u);
        blurry.store_root();
        local_sum.compute_root();
        Pipeline p({blurry});
        auto buf = p.realize({32, 32});
        check_blur_output(buf, correct);
    }

    // https://github.com/halide/Halide/issues/8054
    {
        ImageParam input(Float(32), 2, "input");
        const float r_sigma = 0.1;
        const int s_sigma = 8;
        Func bilateral_grid{"bilateral_grid"};

        Var x("x"), y("y"), z("z"), c("c");

        // Add a boundary condition
        Func clamped = Halide::BoundaryConditions::repeat_edge(input);

        // Construct the bilateral grid
        RDom r(0, s_sigma, 0, s_sigma);
        Expr val = clamped(x * s_sigma + r.x - s_sigma / 2, y * s_sigma + r.y - s_sigma / 2);
        val = clamp(val, 0.0f, 1.0f);

        Expr zi = cast<int>(val * (1.0f / r_sigma) + 0.5f);

        Func histogram("histogram");
        histogram(x, y, z, c) = 0.0f;
        histogram(x, y, zi, c) += mux(c, {val, 1.0f});

        // Blur the grid using a five-tap filter
        Func blurx("blurx"), blury("blury"), blurz("blurz");
        blurz(x, y, z, c) = (histogram(x, y, z - 2, c) +
                             histogram(x, y, z - 1, c) * 4 +
                             histogram(x, y, z, c) * 6 +
                             histogram(x, y, z + 1, c) * 4 +
                             histogram(x, y, z + 2, c));
        blurx(x, y, z, c) = (blurz(x - 2, y, z, c) +
                             blurz(x - 1, y, z, c) * 4 +
                             blurz(x, y, z, c) * 6 +
                             blurz(x + 1, y, z, c) * 4 +
                             blurz(x + 2, y, z, c));
        blury(x, y, z, c) = (blurx(x, y - 2, z, c) +
                             blurx(x, y - 1, z, c) * 4 +
                             blurx(x, y, z, c) * 6 +
                             blurx(x, y + 1, z, c) * 4 +
                             blurx(x, y + 2, z, c));

        // Take trilinear samples to compute the output
        val = clamp(input(x, y), 0.0f, 1.0f);
        Expr zv = val * (1.0f / r_sigma);
        zi = cast<int>(zv);
        Expr zf = zv - zi;
        Expr xf = cast<float>(x % s_sigma) / s_sigma;
        Expr yf = cast<float>(y % s_sigma) / s_sigma;
        Expr xi = x / s_sigma;
        Expr yi = y / s_sigma;
        Func interpolated("interpolated");
        interpolated(x, y, c) =
            lerp(lerp(lerp(blury(xi, yi, zi, c), blury(xi + 1, yi, zi, c), xf),
                      lerp(blury(xi, yi + 1, zi, c), blury(xi + 1, yi + 1, zi, c), xf), yf),
                 lerp(lerp(blury(xi, yi, zi + 1, c), blury(xi + 1, yi, zi + 1, c), xf),
                      lerp(blury(xi, yi + 1, zi + 1, c), blury(xi + 1, yi + 1, zi + 1, c), xf), yf),
                 zf);

        // Normalize
        bilateral_grid(x, y) = interpolated(x, y, 0) / interpolated(x, y, 1);
        Pipeline p({bilateral_grid});

        Var v6, zo, vzi;

        blury.compute_root().split(x, x, v6, 6, TailStrategy::GuardWithIf).split(z, zo, vzi, 8, TailStrategy::GuardWithIf).reorder(y, x, c, vzi, zo, v6).vectorize(vzi).vectorize(v6);
        p.compile_to_module({input}, "bilateral_grid", {Target("host")});
    }

    printf("Success!\n");
    return 0;
}
