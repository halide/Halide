#include "Halide.h"
#include <functional>
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {
typedef std::function<int(int, int, int)> FuncChecker;

void check_image(const Realization &r, const std::vector<FuncChecker> &funcs) {
    for (size_t idx = 0; idx < funcs.size(); idx++) {
        FuncChecker correct = funcs[idx];
        const Buffer<int> &im = r[idx];
        for (int z = 0; z < im.channels(); z++) {
            for (int y = 0; y < im.height(); y++) {
                for (int x = 0; x < im.width(); x++) {
                    ASSERT_EQ(im(x, y, z), correct(x, y, z))
                        << "im[" << idx << "](" << x << ", " << y << ", " << z << ")";
                }
            }
        }
    }
}
}  // namespace

TEST(ImplicitArgsTestsTest, BasicImplicitArgs) {
    Var x("x"), y("y");
    Func f("f"), h("h");

    h(x, y) = x + y;
    h.compute_root();

    f(x, _) = h(_) + 2;  // This means f(x, _0, _1) = h(_0, _1) + 2

    Realization result = f.realize({100, 100, 100});
    auto func = [](int x, int y, int z) {
        return y + z + 2;
    };
    check_image(result, {func});
}

TEST(ImplicitArgsTestsTest, UpdateWithImplicitArgs) {
    Var x("x"), y("y"), z("z");
    Func f("f"), g("g"), h("h");

    h(x, y) = x + y;
    h.compute_root();

    f(x, y, z) = x;
    f.compute_root();

    RDom r(0, 2);
    g(x, _) = h(_) + 1;                  // This means g(x, _0, _1) = h(_0, _1) + 1
    g(clamp(f(r.x, _), 0, 50), _) += 2;  // This means g(f(r.x, _0, _1), _0, _1) += 2

    Realization result = g.realize({100, 100, 100});
    auto func = [](int x, int y, int z) {
        return (x == 0) || (x == 1) ? y + z + 3 : y + z + 1;
    };
    check_image(result, {func});
}

TEST(ImplicitArgsTestsTest, MultipleUpdatesWithImplicitArgs) {
    Var x("x"), y("y");
    Func f("f"), g("g"), h("h");

    h(x, y) = x + y;
    h.compute_root();

    g(x) = x + 2;
    g.compute_root();

    f(x, _) = h(_) + 3;      // This means f(x, _0, _1) = h(_0, _1) + 3
    f(x, _) += h(_) * g(_);  // This means f(x, _0, _1) += h(_0, _1) * g(_0)

    Realization result = f.realize({100, 100, 100});
    auto func = [](int x, int y, int z) {
        return (y + z + 3) + (y + z) * (y + 2);
    };
    check_image(result, {func});
}

TEST(ImplicitArgsTestsTest, UpdateOnlyWithImplicitArgs) {
    Var x("x"), y("y");
    Func f("f"), h("h");

    h(x, y) = x + y;
    h.compute_root();

    // This is equivalent to:
    //   f(x, _0, _1) = 0
    //   f(x, _0, _1) += h(_0, _1) + 2
    f(x, _) += h(_) + 2;

    Realization result = f.realize({100, 100, 100});
    auto func = [](int x, int y, int z) {
        return y + z + 2;
    };
    check_image(result, {func});
}

TEST(ImplicitArgsTestsTest, PureImplicitArgs) {
    Var x("x"), y("y");
    Func f("f"), g("g"), h("h");

    h(x, y) = x + y;
    h.compute_root();

    g(x) = x + 2;
    g.compute_root();

    // This is equivalent to:
    //   f(_0, _1) = 0
    //   f(_0, _1) += h(_0, _1)*g(_0) + 3
    f(_) += h(_) * g(_) + 3;

    Realization result = f.realize({100, 100});
    auto func = [](int x, int y, int z) {
        return (x + y) * (x + 2) + 3;
    };
    check_image(result, {func});
}

TEST(ImplicitArgsTestsTest, TupleWithImplicitArgs) {
    Var x("x"), y("y");
    Func f("f"), h("h");

    h(x, y) = x + y;
    h.compute_root();

    // This means f(x, _0, _1) = {h(_0, _1) + 2, x + 2}
    f(x, _) = Tuple(h(_) + 2, x + 2);

    Realization result = f.realize({100, 100, 100});
    auto func1 = [](int x, int y, int z) {
        return y + z + 2;
    };
    auto func2 = [](int x, int y, int z) {
        return x + 2;
    };
    check_image(result, {func1, func2});
}

TEST(ImplicitArgsTestsTest, TupleUpdateWithImplicitArgs) {
    Var x("x"), y("y"), z("z");
    Func f("f"), g("g"), h("h");

    h(x, y) = x + y;
    h.compute_root();

    f(x, y, z) = x;
    f.compute_root();

    RDom r(0, 2);
    // This means g(x, _0, _1) = {h(_0, _1) + 1}
    g(x, _) = Tuple(h(_) + 1);
    // This means g(f(r.x, _0, _1), _0, _1) += {2}
    g(clamp(f(r.x, _), 0, 50), _) += Tuple(2);

    Realization result = g.realize({100, 100, 100});
    auto func = [](int x, int y, int z) {
        return (x == 0) || (x == 1) ? y + z + 3 : y + z + 1;
    };
    check_image(result, {func});
}

TEST(ImplicitArgsTestsTest, TupleMultiplyWithImplicitArgs) {
    Var x("x"), y("y");
    Func f("f"), h("h");

    h(x, y) = x + y;
    h.compute_root();

    // This is equivalent to:
    //   f(x, _0, _1) = {1, 1}
    //   f(x, _0, _1) += {h(_0, _1)[0] + 2, h(_0, _1)[1] * 3}
    f(x, _) *= Tuple(h(_) + 2, h(_) * 3);

    Realization result = f.realize({100, 100, 100});
    auto func1 = [](int x, int y, int z) {
        return y + z + 2;
    };
    auto func2 = [](int x, int y, int z) {
        return (y + z) * 3;
    };
    check_image(result, {func1, func2});
}

TEST(ImplicitArgsTestsTest, ComplexTupleWithImplicitArgs) {
    Var x("x"), y("y");
    Func f("f"), g("g"), h("h");

    h(x, y) = Tuple(x + y, x - y);
    h.compute_root();

    g(x) = Tuple(x + 2, x - 2);
    g.compute_root();

    // This means f(x, _0, _1) = {h(_0, _1)[0] + 3, h(_0, _1)[1] + 4}
    f(x, _) = Tuple(h(_)[0] + 3, h(_)[1] + 4);
    // This means f(x, _0, _1) += {h(_0, _1)[0]*g(_0)[0], h(_0, _1)[1]*g(_0)[1]}
    f(x, _) += Tuple(h(_)[0] * g(_)[0], h(_)[1] * g(_)[1]);

    Realization result = f.realize({100, 100, 100});
    auto func1 = [](int x, int y, int z) {
        return (y + z + 3) + (y + z) * (y + 2);
    };
    auto func2 = [](int x, int y, int z) {
        return (y - z + 4) + (y - z) * (y - 2);
    };
    check_image(result, {func1, func2});
}

TEST(ImplicitArgsTestsTest, PureTupleWithImplicitArgs) {
    Var x("x"), y("y");
    Func f("f"), g("g"), h("h");

    h(x, y) = Tuple(x + y, x - y);
    h.compute_root();

    g(x) = Tuple(x + 2, x - 2);
    g.compute_root();

    // This is equivalent to:
    //   f(_0, _1) = 0
    //   f(_0, _1) += {h(_0, _1)[0]*g(_0)[0] + 3, h(_0, _1)[1]*g(_0)[1] + 4}
    f(_) += Tuple(h(_)[0] * g(_)[0] + 3, h(_)[1] * g(_)[1] + 4);

    Realization result = f.realize({100, 100});
    auto func1 = [](int x, int y, int z) {
        return (x + y) * (x + 2) + 3;
    };
    auto func2 = [](int x, int y, int z) {
        return (x - y) * (x - 2) + 4;
    };
    check_image(result, {func1, func2});
}
