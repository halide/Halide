#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {
void check(const Interval &result, const Interval &expected, int line) {
    EXPECT_TRUE(equal(result.min, expected.min) && equal(result.max, expected.max))
        << "Interval test on line " << line << " failed\n"
        << "  Expected [" << expected.min << ", " << expected.max << "]\n"
        << "  Got      [" << result.min << ", " << result.max << "]";
}
}  // namespace

TEST(IntervalTest, BasicsAndOperations) {
    Interval e = Interval::everything();
    Interval n = Interval::nothing();
    Expr pos_inf = Interval::pos_inf();
    Expr neg_inf = Interval::neg_inf();
    Expr x = Variable::make(Int(32), "x");
    Interval xp{x, pos_inf};
    Interval xn{neg_inf, x};
    Interval xx{x, x};

    EXPECT_TRUE(e.is_everything());
    EXPECT_FALSE(e.has_upper_bound());
    EXPECT_FALSE(e.has_lower_bound());
    EXPECT_FALSE(e.is_empty());
    EXPECT_FALSE(e.is_bounded());
    EXPECT_FALSE(e.is_single_point());

    EXPECT_FALSE(n.is_everything());
    EXPECT_FALSE(n.has_upper_bound());
    EXPECT_FALSE(n.has_lower_bound());
    EXPECT_TRUE(n.is_empty());
    EXPECT_FALSE(n.is_bounded());
    EXPECT_FALSE(n.is_single_point());

    EXPECT_FALSE(xp.is_everything());
    EXPECT_FALSE(xp.has_upper_bound());
    EXPECT_TRUE(xp.has_lower_bound());
    EXPECT_FALSE(xp.is_empty());
    EXPECT_FALSE(xp.is_bounded());
    EXPECT_FALSE(xp.is_single_point());

    EXPECT_FALSE(xn.is_everything());
    EXPECT_TRUE(xn.has_upper_bound());
    EXPECT_FALSE(xn.has_lower_bound());
    EXPECT_FALSE(xn.is_empty());
    EXPECT_FALSE(xn.is_bounded());
    EXPECT_FALSE(xn.is_single_point());

    EXPECT_FALSE(xx.is_everything());
    EXPECT_TRUE(xx.has_upper_bound());
    EXPECT_TRUE(xx.has_lower_bound());
    EXPECT_FALSE(xx.is_empty());
    EXPECT_TRUE(xx.is_bounded());
    EXPECT_TRUE(xx.is_single_point());

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
}
