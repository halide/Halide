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

    printf("Success!\n");
    return 0;
}
