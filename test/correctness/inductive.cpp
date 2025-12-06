#include "Halide.h"
#include "check_call_graphs.h"
#include "test_sharding.h"

#include <cstdio>
#include <map>

namespace {

using std::map;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

int simple_inductive_test() {
    Func g("g"), h("h");
    Var x("x"), y("y");

    // g(x, y) = x + y;
    // g(r.x, r.y) = g(r.x, r.y);
    g(x, y) = select(x <= 0, 0, g(max(0, x - 1), y) + x + y);

    h(x, y) = g(x + 5, y) / 4;

    g.compute_at(h, x).store_at(h, y);

    Buffer<int> im = h.realize({600, 5});
    auto func = [](int x, int y) {
        return (y * (x + 5) + (x + 5) * (x + 6) / 2) / 4;
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int reorder_test() {
    Func g("g"), h("h");
    Var x("x"), y("y");

    Var xi("xi"), xii("xii"), xo("xo");

    // g(x, y) = x + y;
    // g(r.x, r.y) = g(r.x, r.y);
    g(x, y) = select(x <= 0, 0, g(max(0, x - 1), y) + x + y);

    h(x, y) = g(x + 5, y) / 4;
    h.split(x, xo, xi, 24).reorder(xi, y, xo);

    g.compute_at(h, xo).store_root();

    g.split(x, xi, xii, 5).reorder(xii, y, xi).vectorize(y, 8);

    Buffer<int> im = h.realize({80, 80});
    auto func = [](int x, int y) {
        return (y * (x + 5) + (x + 5) * (x + 6) / 2) / 4;
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int summed_area_table() {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");
    f(x, y) = x + y;
    g(x, y) = f(x, y) + select(x <= 0, 0, g(x - 1, y)) + select(y <= 0, 0, g(x, y - 1)) - select(x <= 0 || y <= 0, 0, g(x - 1, y - 1));
    h(x, y) = g(x, y) / 8;
    g.compute_at(h, x).store_root();

    Buffer<int> im = h.realize({80, 80});
    auto func = [](int x, int y) {
        return (x * (x + 1) / 2 * (y + 1) + y * (y + 1) / 2 * (x + 1)) / 8;
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int large_baseline() {
    Func g("g"), h("h");
    Var x("x"), y("y");

    // g(x, y) = x + y;
    // g(r.x, r.y) = g(r.x, r.y);
    g(x, y) = select(x <= 8, (y * x + x * (x + 1) / 2) - 1, g(x - 1, y) + x + y);

    h(x, y) = g(x + 5, y) / 4;

    g.compute_at(h, x).store_at(h, y);

    Buffer<int> im = h.realize({80, 80});
    auto func = [](int x, int y) {
        return (y * (x + 5) + (x + 5) * (x + 6) / 2 - 1) / 4;
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int fibonacci() {
    Func g("g"), h("h");
    Var x("x"), y("y");

    // g(x, y) = x + y;
    // g(r.x, r.y) = g(r.x, r.y);
    g(x, y) = select(x <= 1, 1, g(x - 1, y) + g(x - 2, y));
    h(x, y) = g(x, y);

    h.bound(x, 0, 80);
    Buffer<int> im = h.realize({80, 80});
    auto func = [](int x, int y) {
        int a = 1;
        int b = 1;
        for (int i = 2; i <= x; i++) {
            int c = a + b;
            b = a;
            a = c;
        }
        return a;
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int sum_2d_test() {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");
    f(x, y) = select(x <= 0, 0, x + f(x - 1, y));
    g(x, y) = select(y <= 0, f(x, 0), f(x, y) + g(x, y - 1));
    h(x, y) = g(x, y);
    h.bound(x, 0, 80).bound(y, 0, 80).vectorize(x, 8);
    g.compute_at(h, x).store_root().vectorize(x, 8);
    f.compute_at(h, x).store_root();
    Buffer<int> im = h.realize({80, 80});
    auto func = [](int x, int y) {
        int ans = 0;
        for (int a = 0; a <= x; a++) {
            for (int b = 0; b <= y; b++) {
                ans += a;
            }
        }
        return ans;
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int sum_1d_test() {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");
    f(x, y) = x + y;
    f(x, y) += x;  // select(x<=0, 0, x+f(x-1,y));
    g(x, y) = select(y <= 0, f(x, 0), f(x, y) + g(x, y - 1));
    h(x, y) = g(x, y);
    h.bound(x, 0, 80).bound(y, 0, 80);
    // stress-testing bounds inference for dependent non-inlined funcs
    f.compute_at(h, x);
    Buffer<int> im = h.realize({80, 80});
    auto func = [](int x, int y) {
        int ans = 0;
        for (int a = 0; a <= y; a++) {
            ans += 2 * x + a;
        }
        return ans;
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int multi_baseline_test() {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");
    f(x, y) = x + y;
    f(x, y) += x;  // select(x<=0, 0, x+f(x-1,y));
    g(x, y) = select(y <= 0, f(x, 0), f(x, y) + g(x, y - 1)) + select(y <= 3, f(x, 0), f(x, y) + g(x, y - 1));
    h(x, y) = g(x, y);
    h.bound(x, 0, 80).bound(y, 0, 20);
    f.compute_at(h, x);
    Buffer<int> im = h.realize({80, 20});
    auto func = [](int x, int y) {
        std::vector<int> ans;

        for (int a = 0; a <= y; a++) {
            if (a <= 0) {
                ans.emplace_back(4 * x);
            } else if (a <= 3) {
                ans.emplace_back(2 * x + (2 * x + a) + ans[a - 1]);
            } else {
                ans.emplace_back(2 * x + a + ans[a - 1] + (2 * x + a) + ans[a - 1]);
            }
        }
        return ans[y];
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int type_declare_test() {
    Func g = Func(Int(32), "g");
    Func h("h");
    Var x("x"), y("y");

    g(x, y) = select(x <= 0, 0, 1 + g(max(0, x - 1), y) + x + 2);

    h(x, y) = g(x + 5, y) / 4;

    g.compute_at(h, x).store_at(h, y);

    Buffer<int> im = h.realize({600, 5});
    auto func = [](int x, int y) {
        return (3 * (x + 5) + (x + 5) * (x + 6) / 2) / 4;
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    struct Task {
        std::string desc;
        std::function<int()> fn;
    };

    std::vector<Task> tasks = {
        {"simple inductive test", simple_inductive_test},
        {"reordering test", reorder_test},
        {"summed area table test", summed_area_table},
        {"large baseline test", large_baseline},
        {"fibonacci test", fibonacci},
        {"2d sum test", sum_2d_test},
        {"1d sum test", sum_1d_test},
        {"multi-baseline test", multi_baseline_test},
        {"type declaration test", type_declare_test},
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
