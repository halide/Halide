#include "Halide.h"
#include "check_call_graphs.h"
#include "test_sharding.h"

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
    f(x, y) = select(x <= 0, y, likely(f(x - 1, y) + 1));  // f(x,y) = x + y
    g(x, y) = f(x, 2 * y);
    f.compute_at(g, x).store_root().fold_storage(x, 1);
    g.bound(x, 0, 64).bound(y, 0, 8);

    Buffer<int> im = g.realize({64, 8});
    return check_image(im, [](int x, int y) { return x + 2 * y; });
}

}  // namespace

int main(int argc, char **argv) {
    struct Task {
        std::string desc;
        std::function<int()> fn;
    };

    std::vector<Task> tasks = {
        {"multi-lag (x-1, x-2), fold_storage 2", fib_fold2},
        {"multi-inner-dim recurrence, fold_storage 1", multi_inner_injective_fold1},
        {"strided non-fold consumer access, fold_storage 1", consumer_strided_nonfold_fold1},
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
