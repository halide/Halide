#include "Halide.h"
#include "check_call_graphs.h"
#include "test_sharding.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <vector>

namespace {

using namespace Halide;

int fib_fold2() {
    Func f(Int(32), "f"), g("g");
    Var x("x"), y("y");
    f(x, y) = select(x <= 1, x + y, likely(f(x - 1, y) + f(x - 2, y)));
    g(x, y) = f(x, y);
    f.compute_at(g, x).store_root().fold_storage(x, 2);
    g.bound(x, 0, 30).bound(y, 0, 8);

    Buffer<int> im = g.realize({30, 8});
    return check_image(im, [](int x, int y) {
        if (x <= 1) return x + y;
        int a = y, b = 1 + y;
        for (int i = 2; i <= x; i++) {
            int c = b + a;
            a = b;
            b = c;
        }
        return b;
    });
}

int multi_inner_injective_fold1() {
    Func f(Int(32), "f"), g("g");
    Var x("x"), y("y"), z("z");
    f(x, y, z) = select(x <= 0, y + z, likely(f(x - 1, y, z) + 1));
    g(x, y, z) = f(x, y, z);
    g.reorder(z, y, x);
    f.compute_at(g, x).store_root().fold_storage(x, 1);
    g.bound(x, 0, 16).bound(y, 0, 4).bound(z, 0, 4);

    Buffer<int> im = g.realize({16, 4, 4});
    return check_image(im, [](int x, int y, int z) { return x + y + z; });
}

int consumer_strided_nonfold_fold1() {
    Func f(Int(32), "f"), g("g");
    Var x("x"), y("y");
    f(x, y) = select(x <= 0, y, likely(f(x - 1, y) + 1));  // f(x, y) = x + y
    g(x, y) = f(x, 2 * y);
    f.compute_at(g, x).store_root().fold_storage(x, 1);
    g.bound(x, 0, 64).bound(y, 0, 8);

    Buffer<int> im = g.realize({64, 8});
    return check_image(im, [](int x, int y) { return x + 2 * y; });
}

int box_filter_chained_inductive_fold() {
    const int W = 10, H = 10, R = 4;
    Var x("x"), y("y");

    Func input("input");
    input(x, y) = x + y;

    Func rowsum(Int(32), "rowsum");
    rowsum(x, y) = select(x <= 0, input(0, y),
                          likely(rowsum(x - 1, y) + input(x, y)));

    Func sat(Int(32), "sat");
    sat(x, y) = select(y < 0, 0,
                       likely(sat(x, y - 1) + rowsum(x, y)));

    Func sat_clamped("sat_clamped");
    sat_clamped(x, y) = select(x < 0 || y < 0, 0,
                               sat(clamp(x, 0, W - 1), clamp(y, 0, H - 1)));

    Func box("box");
    box(x, y) = sat_clamped(x + R, y + R)
        - sat_clamped(x - R - 1, y + R)
        - sat_clamped(x + R, y - R - 1)
        + sat_clamped(x - R - 1, y - R - 1);

    box.bound(x, 0, W).bound(y, 0, H);

    sat.store_root().compute_at(box, y).fold_storage(y, 2 * R + 2);
    rowsum.store_at(box, y).compute_at(sat, x).fold_storage(x, 1);

    Buffer<int> result = box.realize({W, H});

    return check_image(result, [&](int x, int y) {
        int expected = 0;
        for (int j = std::max(0, y - R); j <= std::min(H - 1, y + R); j++) {
            for (int i = std::max(0, x - R); i <= std::min(W - 1, x + R); i++) {
                expected += i + j;
            }
        }
        return expected;
    });
}

}  // namespace

int main(int argc, char **argv) {
    struct Task {
        std::string desc;
        std::function<int()> fn;
    };

    std::vector<Task> tasks = {
        {"fibonacci, fold_storage 2", fib_fold2},
        {"loop-reordered recurrence, fold_storage 1", multi_inner_injective_fold1},
        {"strided non-fold consumer access, fold_storage 1", consumer_strided_nonfold_fold1},
        {"box filter, fold_storage 1", box_filter_chained_inductive_fold},
    };

    using Sharder = Halide::Internal::Test::Sharder;
    Sharder sharder;
    for (size_t t = 0; t < tasks.size(); t++) {
        if (!sharder.should_run(t)) continue;
        const auto &task = tasks.at(t);
        std::cout << task.desc << "\n";
        if (task.fn() != 0) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
