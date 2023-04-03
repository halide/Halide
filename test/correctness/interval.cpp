#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

void check(Interval result, Interval expected, int line) {
    if (!(equal(result.min, expected.min) &&
          equal(result.max, expected.max))) {
        std::cerr << "Interval test on line " << line << " failed\n"
                  << "  Expected [" << expected.min << ", " << expected.max << "]\n"
                  << "  Got      [" << result.min << ", " << result.max << "]\n";
        exit(1);
    }
}

int main(int argc, char **argv) {
    Interval e = Interval::everything();
    Interval n = Interval::nothing();
    Expr pos_inf = Interval::pos_inf();
    Expr neg_inf = Interval::neg_inf();
    Expr x = Variable::make(Int(32), "x");
    Interval xp{x, pos_inf};
    Interval xn{neg_inf, x};
    Interval xx{x, x};

    assert(e.is_everything());
    assert(!e.has_upper_bound());
    assert(!e.has_lower_bound());
    assert(!e.is_empty());
    assert(!e.is_bounded());
    assert(!e.is_single_point());

    assert(!n.is_everything());
    assert(!n.has_upper_bound());
    assert(!n.has_lower_bound());
    assert(n.is_empty());
    assert(!n.is_bounded());
    assert(!n.is_single_point());

    assert(!xp.is_everything());
    assert(!xp.has_upper_bound());
    assert(xp.has_lower_bound());
    assert(!xp.is_empty());
    assert(!xp.is_bounded());
    assert(!xp.is_single_point());

    assert(!xn.is_everything());
    assert(xn.has_upper_bound());
    assert(!xn.has_lower_bound());
    assert(!xn.is_empty());
    assert(!xn.is_bounded());
    assert(!xn.is_single_point());

    assert(!xx.is_everything());
    assert(xx.has_upper_bound());
    assert(xx.has_lower_bound());
    assert(!xx.is_empty());
    assert(xx.is_bounded());
    assert(xx.is_single_point());

    check(Interval::make_union(xp, xn), e, __LINE__);
    check(Interval::make_union(e, xn), e, __LINE__);
    check(Interval::make_union(xn, e), e, __LINE__);
    check(Interval::make_union(xn, n), xn, __LINE__);
    check(Interval::make_union(n, xp), xp, __LINE__);
    check(Interval::make_union(xp, xp), xp, __LINE__);

    check(Interval::make_intersection(xp, xn), Interval::single_point(x), __LINE__);
    check(Interval::make_intersection(e, xn), xn, __LINE__);
    check(Interval::make_intersection(xn, e), xn, __LINE__);
    check(Interval::make_intersection(xn, n), n, __LINE__);
    check(Interval::make_intersection(n, xp), n, __LINE__);
    check(Interval::make_intersection(xp, xp), xp, __LINE__);

    check(Interval::make_union({3, pos_inf}, {5, pos_inf}), {3, pos_inf}, __LINE__);
    check(Interval::make_intersection({3, pos_inf}, {5, pos_inf}), {5, pos_inf}, __LINE__);

    check(Interval::make_union({neg_inf, 3}, {neg_inf, 5}), {neg_inf, 5}, __LINE__);
    check(Interval::make_intersection({neg_inf, 3}, {neg_inf, 5}), {neg_inf, 3}, __LINE__);

    check(Interval::make_union({3, 4}, {9, 10}), {3, 10}, __LINE__);
    check(Interval::make_intersection({3, 4}, {9, 10}), {9, 4}, __LINE__);

    check(Interval::make_union({3, 9}, {4, 10}), {3, 10}, __LINE__);
    check(Interval::make_intersection({3, 9}, {4, 10}), {4, 9}, __LINE__);

    printf("Success!\n");
    return 0;
}
