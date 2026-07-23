#include "Halide.h"
#include "check_call_graphs.h"
#include "test_sharding.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <map>
#include <vector>

namespace {

using std::map;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

int simple_inductive_test() {
    Func g(Int(32), "g"), h("h");
    Var x("x"), y("y");

    g(x, y) = select(x <= 0, 0, likely(g(max(0, x - 1), y) + x + y));

    h(x, y) = g(x + 5, y) / 4;

    g.compute_at(h, x).store_at(h, y);

    h.bound(x, 0, 600).bound(y, 0, 5);

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
        int result = 0;
        for (int a = 0; a <= x; a++) {
            for (int b = 0; b <= y; b++) {
                result += a;
            }
        }
        return result;
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
    f(x, y) += x;
    g(x, y) = select(y <= 0, f(x, 0), f(x, y) + g(x, y - 1));
    h(x, y) = g(x, y);
    h.bound(x, 0, 80).bound(y, 0, 80);
    f.compute_at(h, x);
    Buffer<int> im = h.realize({80, 80});
    auto func = [](int x, int y) {
        int result = 0;
        for (int a = 0; a <= y; a++) {
            result += 2 * x + a;
        }
        return result;
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
    f(x, y) += x;
    g(x, y) = select(y <= 0, f(x, 0), f(x, y) + g(x, y - 1)) + select(y <= 3, f(x, 0), f(x, y) + g(x, y - 1));
    h(x, y) = g(x, y);
    h.bound(x, 0, 80).bound(y, 0, 20);
    f.compute_at(h, x);
    Buffer<int> im = h.realize({80, 20});
    auto func = [](int x, int y) {
        std::vector<int> result;

        for (int a = 0; a <= y; a++) {
            if (a <= 0) {
                result.emplace_back(4 * x);
            } else if (a <= 3) {
                result.emplace_back(2 * x + (2 * x + a) + result[a - 1]);
            } else {
                result.emplace_back(2 * x + a + result[a - 1] + (2 * x + a) + result[a - 1]);
            }
        }
        return result[y];
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

int inductive_update_rdom_test() {
    Func g(Int(32), "g"), h("h");
    Var x("x"), y("y");

    const int Y = 10;
    RDom r(0, Y, 0, Y, "r");

    g(x, y) = Expr(std::numeric_limits<int>::min());
    g(x, r.x) = select(x <= 0, r.x, likely(max(g(x, r.x), g(x - 1, r.y) + x + r.y)));

    h(x, y) = g(x, y);

    h.bound(x, 0, 20).bound(y, 0, Y);
    g.compute_at(h, x).store_root();

    Buffer<int> im = h.realize({20, Y});

    std::vector<std::vector<int>> rows(20, std::vector<int>(Y));
    for (int yy = 0; yy < Y; yy++) {
        rows[0][yy] = yy;
    }
    for (int xx = 1; xx < 20; xx++) {
        int best = rows[xx - 1][0] + xx + 0;
        for (int ry = 1; ry < Y; ry++) {
            best = std::max(best, rows[xx - 1][ry] + xx + ry);
        }
        for (int yy = 0; yy < Y; yy++) {
            rows[xx][yy] = best;
        }
    }
    auto func = [&](int x, int y) {
        return rows[x][y];
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int tuple_test() {
    Func g = Func(std::vector<Type>{Int(32), Int(32)}, "g");
    Func h("h");
    Var x("x"), y("y");

    g(x, y) = Tuple(select(x <= 0, 0, g(max(0, x - 1), y)[0] + x + y), x - y);
    h(x, y) = g(10, y)[0] / 4 + g(10, y)[1] + x;

    g.compute_root();

    Buffer<int> im = h.realize({10, 10});
    auto func = [](int x, int y) {
        return (y * (5 + 5) + (5 + 5) * (5 + 6) / 2) / 4 + 10 - y + x;
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int tuple_test_2() {
    Func g = Func(std::vector<Type>{Int(32), Int(32)}, "g");
    Func h("h");
    Var x("x"), y("y");

    g(x, y) = Tuple(select(x <= 0, 0, g(max(0, x - 1), y)[0] + x + y), select(y <= 0, 0, g(x, max(0, y - 1))[1] + x + y));
    h(x, y) = g(x, y)[0] + g(x, y)[1];

    g.compute_root();

    Buffer<int> im = h.realize({10, 10});
    auto func = [](int x, int y) {
        return y * x + x * (x + 1) / 2 + x * y + y * (y + 1) / 2;
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
        {"inductive update rdom test", inductive_update_rdom_test},
        {"type declaration test", type_declare_test},
        {"tuple test", tuple_test},
        {"tuple test 2", tuple_test_2},
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
