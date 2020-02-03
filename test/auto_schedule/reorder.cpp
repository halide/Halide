#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Tools;

using std::vector;

double run_test_1(bool auto_schedule) {
    Var x("x"), y("y"), dx("dx"), dy("dy"), c("c");

    Func f("f");
    f(x, y, dx, dy) = x + y + dx + dy;

    int search_area = 7;
    RDom dom(-search_area / 2, search_area, -search_area / 2, search_area, "dom");

    // If 'f' is inlined into 'r', the only storage layout that the auto scheduler
    // needs to care about is that of 'r'.
    Func r("r");
    r(x, y, c) += f(x, y + 1, dom.x, dom.y) * f(x, y - 1, dom.x, dom.y) * c;

    Target target = get_jit_target_from_environment();
    Pipeline p(r);

    if (auto_schedule) {
        // Provide estimates on the pipeline output
        r.set_estimates({{0, 1024}, {0, 1024}, {0, 3}});
        // Auto-schedule the pipeline
        p.auto_schedule(target);
    } else {
        /*
        r.update(0).fuse(c, y, par).parallel(par).reorder(x, dom.x, dom.y).vectorize(x, 4);
        r.fuse(c, y, par).parallel(par).vectorize(x, 4); */

        // The sequential schedule in this case seems to perform best which is
        // odd have to investigate this further.
    }

    r.print_loop_nest();

    // Run the schedule
    Buffer<int> out(1024, 1024, 3);
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
    });

    return t * 1000;
}

double run_test_2(bool auto_schedule) {
    Var x("x"), y("y"), z("z"), c("c");

    int W = 1024;
    int H = 1920;
    Buffer<uint8_t> left_im(W, H, 3);
    Buffer<uint8_t> right_im(W, H, 3);

    for (int y = 0; y < left_im.height(); y++) {
        for (int x = 0; x < left_im.width(); x++) {
            for (int c = 0; c < 3; c++) {
                left_im(x, y, c) = rand() & 0xfff;
                right_im(x, y, c) = rand() & 0xfff;
            }
        }
    }

    Func left = BoundaryConditions::repeat_edge(left_im);
    Func right = BoundaryConditions::repeat_edge(right_im);

    Func diff;
    diff(x, y, z, c) = min(absd(left(x, y, c), right(x + 2 * z, y, c)),
                           absd(left(x, y, c), right(x + 2 * z + 1, y, c)));

    Target target = get_jit_target_from_environment();
    Pipeline p(diff);

    if (auto_schedule) {
        // Provide estimates on the pipeline output
        diff.set_estimates({{0, left_im.width()}, {0, left_im.height()}, {0, 32}, {0, 3}});
        // Auto-schedule the pipeline
        p.auto_schedule(target);
    } else {
        Var t("t");
        diff.reorder(c, z).fuse(c, z, t).parallel(t).vectorize(x, 16);
    }

    diff.print_loop_nest();

    // Run the schedule
    Buffer<uint8_t> out(left_im.width(), left_im.height(), 32, 3);
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
    });

    return t * 1000;
}

double run_test_3(bool auto_schedule) {
    Buffer<uint8_t> im(1024, 1028, 14, 14);

    Var x("x"), y("y"), dx("dx"), dy("dy"), c("c");

    Func f("f");
    f(x, y, dx, dy) = im(x, y, dx, dy);

    int search_area = 7;
    RDom dom(-search_area / 2, search_area, -search_area / 2, search_area, "dom");

    Func r("r");
    r(x, y, c) += f(x, y + 1, search_area / 2 + dom.x, search_area / 2 + dom.y) *
                  f(x, y + 2, search_area / 2 + dom.x, search_area / 2 + dom.y) * c;

    Target target = get_jit_target_from_environment();
    Pipeline p(r);

    if (auto_schedule) {
        // Provide estimates on the pipeline output
        r.set_estimates({{0, 1024}, {0, 1024}, {0, 3}});
        // Auto-schedule the pipeline
        p.auto_schedule(target);
    } else {
        Var par("par");
        r.update(0).fuse(c, y, par).parallel(par).reorder(x, dom.x, dom.y).vectorize(x, 4);
        r.fuse(c, y, par).parallel(par).vectorize(x, 4);
    }

    r.print_loop_nest();

    // Run the schedule
    Buffer<int> out(1024, 1024, 3);
    double t = benchmark(3, 10, [&]() {
        p.realize(out);
    });

    return t * 1000;
}

int main(int argc, char **argv) {
    const double slowdown_factor = 6.0;

    {
        std::cout << "Test 1:" << std::endl;
        double manual_time = run_test_1(false);
        double auto_time = run_test_1(true);

        std::cout << "======================" << std::endl;
        std::cout << "Manual time: " << manual_time << "ms" << std::endl;
        std::cout << "Auto time: " << auto_time << "ms" << std::endl;
        std::cout << "======================" << std::endl;

        if (auto_time > manual_time * slowdown_factor) {
            printf("Auto-scheduler is much much slower than it should be.\n");
            return -1;
        }
    }

    {
        std::cout << "Test 2:" << std::endl;
        double manual_time = run_test_2(false);
        double auto_time = run_test_2(true);

        std::cout << "======================" << std::endl;
        std::cout << "Manual time: " << manual_time << "ms" << std::endl;
        std::cout << "Auto time: " << auto_time << "ms" << std::endl;
        std::cout << "======================" << std::endl;

        if (auto_time > manual_time * slowdown_factor) {
            printf("Auto-scheduler is much much slower than it should be.\n");
            return -1;
        }
    }

    {
        std::cout << "Test 3:" << std::endl;
        double manual_time = run_test_3(false);
        double auto_time = run_test_3(true);

        std::cout << "======================" << std::endl;
        std::cout << "Manual time: " << manual_time << "ms" << std::endl;
        std::cout << "Auto time: " << auto_time << "ms" << std::endl;
        std::cout << "======================" << std::endl;

        if (auto_time > manual_time * slowdown_factor) {
            printf("Auto-scheduler is much much slower than it should be.\n");
            return -1;
        }
    }
    return 0;
}
