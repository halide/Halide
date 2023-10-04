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

    printf("Success!\n");

    return 0;
}
