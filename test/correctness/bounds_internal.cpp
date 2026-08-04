#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

using std::map;
using std::string;
using std::vector;

namespace {

// Bounds.cpp has a private overload of simplify() for Interval that isn't
// part of the public API; reimplement the small piece these tests need.
Interval simplify(const Interval &i) {
    Interval result;
    result.min = simplify(i.min);
    if (i.is_single_point()) {
        result.max = result.min;
    } else {
        result.max = simplify(i.max);
    }
    return result;
}

void check(const Scope<Interval> &scope, const Expr &e, const Expr &correct_min, const Expr &correct_max) {
    FuncValueBounds fb;
    Interval result = bounds_of_expr_in_scope(e, scope, fb);
    result = simplify(result);
    if (!equal(result.min, correct_min)) {
        internal_error << "In bounds of " << e << ":\n"
                       << "Incorrect min: " << result.min << "\n"
                       << "Should have been: " << correct_min << "\n";
    }
    if (!equal(result.max, correct_max)) {
        internal_error << "In bounds of " << e << ":\n"
                       << "Incorrect max: " << result.max << "\n"
                       << "Should have been: " << correct_max << "\n";
    }
}

void check_constant_bound(const Scope<Interval> &scope, const Expr &e, const Expr &correct_min, const Expr &correct_max) {
    FuncValueBounds fb;
    Interval result = bounds_of_expr_in_scope(e, scope, fb, true);
    result = simplify(result);
    if (!equal(result.min, correct_min)) {
        internal_error << "In find constant bound of " << e << ":\n"
                       << "Incorrect min constant bound: " << result.min << "\n"
                       << "Should have been: " << correct_min << "\n";
    }
    if (!equal(result.max, correct_max)) {
        internal_error << "In find constant bound of " << e << ":\n"
                       << "Incorrect max constant bound: " << result.max << "\n"
                       << "Should have been: " << correct_max << "\n";
    }
}

void check_constant_bound(const Expr &e, const Expr &correct_min, const Expr &correct_max) {
    Scope<Interval> scope;
    check_constant_bound(scope, e, correct_min, correct_max);
}

void constant_bound_test() {
    using namespace ConciseCasts;

    {
        Param<int16_t> a;
        Param<uint16_t> b;
        check_constant_bound(a >> b, i16(-32768), i16(32767));
    }

    {
        Param<int> x("x"), y("y");
        x.set_range(10, 20);
        y.set_range(5, 30);
        check_constant_bound(clamp(x, 5, 30), 10, 20);
        check_constant_bound(clamp(x, 15, 30), 15, 20);
        check_constant_bound(clamp(x, 15, 17), 15, 17);
        check_constant_bound(clamp(x, 5, 15), 10, 15);

        check_constant_bound(x + y, 15, 50);
        check_constant_bound(x - y, -20, 15);
        check_constant_bound(x * y, 50, 600);
        check_constant_bound(x / y, 0, 4);

        check_constant_bound(select(x > 4, 3 * x - y / 2, max(x + y + 2, x - 20)), 15, 58);
        check_constant_bound(select(x < 4, 3 * x - y / 2, max(x + y + 2, x - 20)), 17, 52);
        check_constant_bound(select(x >= 11, 3 * x - y / 2, max(x + y + 2, x - 20)), 15, 58);
    }

    {
        Param<uint8_t> x("x"), y("y");
        x.set_range(Expr((uint8_t)10), Expr((uint8_t)20));
        y.set_range(Expr((uint8_t)5), Expr((uint8_t)30));
        check_constant_bound(clamp(x, 5, 30), Expr((uint8_t)10), Expr((uint8_t)20));
        check_constant_bound(clamp(x, 15, 30), Expr((uint8_t)15), Expr((uint8_t)20));
        check_constant_bound(clamp(x, 15, 17), Expr((uint8_t)15), Expr((uint8_t)17));
        check_constant_bound(clamp(x, 5, 15), Expr((uint8_t)10), Expr((uint8_t)15));

        check_constant_bound(x + y, Expr((uint8_t)15), Expr((uint8_t)50));
        check_constant_bound(x / y, Expr((uint8_t)0), Expr((uint8_t)4));

        check_constant_bound(select(x > 4, 3 * x - y / 2, max(x + y + 2, x + 20)),
                             Expr((uint8_t)15), Expr((uint8_t)58));
        check_constant_bound(select(x < 4, 3 * x - y / 2, max(x + y + 2, x + 20)),
                             Expr((uint8_t)30), Expr((uint8_t)52));
        check_constant_bound(select(x >= 11, 3 * x - y / 2, max(x + y + 2, x + 20)),
                             Expr((uint8_t)15), Expr((uint8_t)58));

        // These two overflow
        check_constant_bound(x - y, Expr((uint8_t)0), Expr((uint8_t)255));
        check_constant_bound(x * y, Expr((uint8_t)0), Expr((uint8_t)255));

        check_constant_bound(absd(x, y), Expr((uint8_t)0), Expr((uint8_t)20));
        check_constant_bound(absd(cast<int16_t>(x), cast<int16_t>(y)), Expr((uint16_t)0), Expr((uint16_t)20));
    }

    {
        Param<float> x("x"), y("y");
        x.set_range(Expr((float)10), Expr((float)20));
        y.set_range(Expr((float)5), Expr((float)30));

        check_constant_bound(absd(x, y), Expr((float)0), Expr((float)20));
    }

    {
        Param<int8_t> i("i"), x("x"), y("y"), d("d");
        Expr cl = i16(i);
        Expr cr1 = i16(x);
        Expr cr2 = i16(y);
        Expr fraction = (d & (int16_t)((1 << 7) - 1));
        Expr cr = i16((((cr2 - cr1) * fraction) >> 7) + cr1);

        check_constant_bound(absd(cr, cl), Expr((uint16_t)0), Expr((uint16_t)509));
        check_constant_bound(i16(absd(cr, cl)), Expr((int16_t)0), Expr((int16_t)509));
    }

    check_constant_bound(Load::make(Int(32), "buf", 0, Buffer<>(), Parameter(), const_true(), ModulusRemainder()) * 20,
                         Interval::neg_inf(), Interval::pos_inf());

    {
        // Ensure that unnecessary integer overflow doesn't happen
        // in cases involving unsigned integer math
        Param<uint16_t> e1("e1");      // range 0..0xffff, type=uint16
        Expr e2 = cast<uint32_t>(e1);  // range 0..0xffff, type=uint32
        Expr e3 = e2 * e2;             // range 0..0xfffe0001, type=uint32
        check_constant_bound(e3, Expr((uint32_t)0), Expr((uint32_t)0xfffe0001));
    }

    {
        RDom r(0, 4);

        // bounds of an expression with impure >= 32 bit expr will be unbounded
        Expr e32 = sum(cast<int32_t>(r.x));
        check_constant_bound(e32, Interval::neg_inf(), Interval::pos_inf());

        // bounds of an expression with impure < 32 bit expr will be bounds-of-type
        Expr e16 = sum(cast<int16_t>(r.x));
        check_constant_bound(e16, Int(16).min(), Int(16).max());
    }

    {
        Param<int32_t> x("x"), y("y");
        x.set_range(2, 10);

        check_constant_bound(count_leading_zeros(x), i32(28), i32(30));
        check_constant_bound(count_leading_zeros(cast<int16_t>(x)), i16(12), i16(14));

        check_constant_bound(count_leading_zeros(y), i32(0), i32(32));
        check_constant_bound(count_leading_zeros(cast<int16_t>(y)), i16(0), i16(16));
    }
}

void boxes_touched_test() {
    Type t = Int(32);
    Expr x = Variable::make(t, "x");
    Expr y = Variable::make(t, "y");
    Expr z = Variable::make(t, "z");
    Expr w = Variable::make(t, "w");

    Scope<Interval> scope;
    scope.push("y", Interval(Expr(0), Expr(10)));

    Stmt stmt = Provide::make("f", {10}, {x, y, z, w}, const_true());
    stmt = IfThenElse::make(y > 4, stmt, Stmt());
    stmt = IfThenElse::make(z > 18, stmt, Stmt());
    stmt = LetStmt::make("w", z + 3, stmt);
    stmt = LetStmt::make("z", x + 2, stmt);
    stmt = LetStmt::make("x", y + 10, stmt);

    Box expected({Interval(15, 20), Interval(5, 10), Interval(19, 22), Interval(22, 25)});
    Box result = box_provided(stmt, "f", scope);
    internal_assert(expected.size() == result.size())
        << "Expect dim size of " << expected.size()
        << ", got " << result.size() << " instead\n";
    for (size_t i = 0; i < result.size(); ++i) {
        const Interval &correct = expected[i];
        Interval b = result[i];
        b = simplify(b);
        if (!equal(correct.min, b.min)) {
            internal_error << "In bounds of dim " << i << ":\n"
                           << "Incorrect min: " << b.min << "\n"
                           << "Should have been: " << correct.min << "\n";
        }
        if (!equal(correct.max, b.max)) {
            internal_error << "In bounds of dim " << i << ":\n"
                           << "Incorrect max: " << b.max << "\n"
                           << "Should have been: " << correct.max << "\n";
        }
    }
}

}  // namespace

int main() {
    using namespace Halide::ConciseCasts;

    constant_bound_test();

    Scope<Interval> scope;
    Var x("x"), y("y");
    scope.push("x", Interval(Expr(0), Expr(10)));

    check(scope, x, 0, 10);
    check(scope, x + 1, 1, 11);
    check(scope, (x + 1) * 2, 2, 22);
    check(scope, x * x, 0, 100);
    check(scope, 5 - x, -5, 5);
    check(scope, x * (5 - x), -50, 50);  // We don't expect bounds analysis to understand correlated terms
    check(scope, Select::make(x < 4, x, x + 100), 0, 110);
    check(scope, x + y, y, y + 10);
    check(scope, x * y, min(y, 0) * 10, max(y, 0) * 10);
    check(scope, x / (x + y), -10, 10);
    check(scope, 11 / (x + 1), 1, 11);
    check(scope, Load::make(Int(8), "buf", x, Buffer<>(), Parameter(), const_true(), ModulusRemainder()),
          i8(-128), i8(127));
    check(scope, y + (Let::make("y", x + 3, y - x + 10)), y + 3, y + 23);  // Once again, we don't know that y is correlated with x
    check(scope, clamp(1000 / (x - 2), x - 10, x + 10), -10, 20);
    check(scope, cast<uint16_t>(x / 2), u16(0), u16(5));
    check(scope, cast<uint16_t>((x + 10) / 2), u16(5), u16(10));
    check(scope, x < 20, make_bool(true), make_bool(true));
    check(scope, x < 5, make_bool(false), make_bool(true));
    check(scope, Broadcast::make(x >= 11, 3), make_bool(false), make_bool(false));
    check(scope, Ramp::make(x + 5, 1, 5) > Broadcast::make(2, 5), make_bool(true), make_bool(true));

    check(scope, print(x, y), 0, 10);
    check(scope, print_when(x > y, x, y), 0, 10);

    check(scope, select(y == 5, 0, 3), select(y == 5, 0, 3), select(y == 5, 0, 3));
    check(scope, select(y == 5, x, -3 * x + 8), select(y == 5, 0, -22), select(y == 5, 10, 8));
    check(scope, select(y == x, x, -3 * x + 8), -22, select(y <= 10 && 0 <= y, 10, 8));

    check(scope, cast<int32_t>(abs(cast<int16_t>(x ^ y))), 0, 32768);
    check(scope, cast<float>(x), 0.0f, 10.0f);

    check(scope, cast<int32_t>(abs(cast<float>(x))), 0, 10);
    check(scope, abs(2 + x), u32(2), u32(12));
    check(scope, abs(x - 11), u32(1), u32(11));
    check(scope, abs(x - 5), u32(0), u32(5));
    check(scope, abs(2 + cast<float>(x)), 2.f, 12.f);
    check(scope, abs(cast<float>(x) - 11), 1.f, 11.f);
    check(scope, abs(cast<float>(x) - 5), 0.f, 5.f);
    check(scope, abs(2 + cast<int8_t>(x)), u8(2), u8(12));
    check(scope, abs(cast<int8_t>(x) - 11), u8(1), u8(11));
    check(scope, abs(cast<int8_t>(x) - 5), u8(0), u8(5));
    scope.push("x", Interval(123, Interval::pos_inf()));
    check(scope, abs(x), u32(123), Interval::pos_inf());
    scope.pop("x");
    scope.push("x", Interval(Interval::neg_inf(), -123));
    check(scope, abs(x), u32(123), Interval::pos_inf());
    scope.pop("x");

    // Check some vectors
    check(scope, Ramp::make(x * 2, 5, 5), 0, 40);
    check(scope, Broadcast::make(x * 2, 5), 0, 20);
    check(scope, Broadcast::make(3, 4), 3, 3);

    // Check some operations that may overflow
    check(scope, (cast<uint8_t>(x) + 250), u8(0), u8(255));
    check(scope, (cast<uint8_t>(x) + 10) * 20, u8(0), u8(255));
    check(scope, (cast<uint8_t>(x) + 10) * (cast<uint8_t>(x) + 5), u8(0), u8(255));
    check(scope, (cast<uint8_t>(x) + 10) - (cast<uint8_t>(x) + 5), u8(0), u8(255));

    // Check some operations that we should be able to prove do not overflow
    check(scope, (cast<uint8_t>(x) + 240), u8(240), u8(250));
    check(scope, (cast<uint8_t>(x) + 10) * 10, u8(100), u8(200));
    check(scope, (cast<uint8_t>(x) + 10) * (cast<uint8_t>(x)), u8(0), u8(200));
    check(scope, (cast<uint8_t>(x) + 20) - (cast<uint8_t>(x) + 5), u8(5), u8(25));

    // Check div/mod by unbounded unknowns. div and mod can only ever
    // make things smaller in magnitude.
    scope.push("x", Interval::everything());
    check(scope, -3 / x, -3, 3);
    check(scope, 3 / x, -3, 3);
    check(scope, y / x, -cast<int>(abs(y)), cast<int>(abs(y)));
    check(scope, -3 % x, 0, Interval::pos_inf());
    check(scope, 3 % x, 0, 3);
    // Mod can't make values negative
    check(scope, y % x, 0, Interval::pos_inf());
    // Mod can't make positive values larger
    check(scope, max(y, 0) % x, 0, max(y, 0));
    scope.pop("x");

    // Check some bitwise ops.
    check(scope, (cast<uint8_t>(x) & make_const(UInt(8), 7)), u8(0), u8(7));
    check(scope, (make_const(UInt(8), 3) & make_const(UInt(8), 2)), u8(2), u8(2));
    check(scope, (make_one(UInt(8)) | make_const(UInt(8), 2)), u8(3), u8(3));
    check(scope, (make_const(UInt(8), 3) ^ make_const(UInt(8), 2)), u8(1), u8(1));
    check(scope, (~make_const(UInt(8), 3)), u8(0xfc), u8(0xfc));
    check(scope, cast<uint8_t>(x + 5) & cast<uint8_t>(x + 3), u8(0), u8(13));
    check(scope, cast<int8_t>(x - 5) & cast<int8_t>(x + 3), i8(0), i8(13));
    check(scope, cast<int8_t>(2 * x - 5) & cast<int8_t>(x - 3), i8(-128), i8(15));
    check(scope, cast<uint8_t>(x + 5) | cast<uint8_t>(x + 3), u8(5), u8(255));
    check(scope, cast<int8_t>(x + 5) | cast<int8_t>(x + 3), i8(3), i8(127));
    check(scope, ~cast<uint8_t>(x), u8(-11), u8(-1));
    check(scope, (cast<uint8_t>(x) >> make_one(UInt(8))), u8(0), u8(5));
    check(scope, (make_const(UInt(8), 10) >> make_one(UInt(8))), u8(5), u8(5));
    check(scope, (cast<uint8_t>(x + 3) << make_one(UInt(8))), u8(6), u8(26));
    check(scope, (cast<uint8_t>(x + 3) << make_const(UInt(8), 7)), u8(0), u8(255));  // Overflows
    check(scope, (make_const(UInt(8), 5) << make_one(UInt(8))), u8(10), u8(10));
    check(scope, (x << 12), 0, 10 << 12);
    check(scope, x & 4095, 0, 10);          // LHS known to be positive
    check(scope, x & 123, 0, 10);           // Doesn't have to be a precise bitmask
    check(scope, (x - 1) & 4095, 0, 4095);  // LHS could be -1

    // Regression tests on shifts (produced by z3).
    {
        ScopedBinding<Interval> xb(scope, "x", Interval(-123, Interval::pos_inf()));
        ScopedBinding<Interval> yb(scope, "y", Interval(-6, 0));
        // -123 << 0 = -123
        check(scope, x << y, -123, Interval::pos_inf());
    }
    {
        ScopedBinding<Interval> xb(scope, "x", Interval(-123, Interval::pos_inf()));
        ScopedBinding<Interval> yb(scope, "y", Interval(-6, Interval::pos_inf()));
        // A negative value can increase in magnitude if the rhs is positive.
        check(scope, x << y, Interval::neg_inf(), Interval::pos_inf());
    }
    {
        ScopedBinding<Interval> xb(scope, "x", Interval(-123, Interval::pos_inf()));
        Var c("c");
        ScopedBinding<Interval> yb(scope, "y", Interval(-6, c));
        // Can't prove anything about the upper bound of y.
        check(scope, x << y, min((-123) << c, -123), Interval::pos_inf());
    }
    {
        ScopedBinding<Interval> xb(scope, "x", Interval(-123, Interval::pos_inf()));
        ScopedBinding<Interval> yb(scope, "y", Interval(-6, 4));
        // -123 << 4 = -1968
        check(scope, x << y, -1968, Interval::pos_inf());
    }
    {
        ScopedBinding<Interval> xb(scope, "x", Interval(24, Interval::pos_inf()));
        ScopedBinding<Interval> yb(scope, "y", Interval(Interval::neg_inf(), -1));
        // Cannot change sign, only can decrease magnitude.
        check(scope, x << y, 0, Interval::pos_inf());
    }
    // Overflow testing (for types with defined overflow).
    {
        Type uint32 = UInt(32);
        Expr a = Variable::make(uint32, "a");
        Expr b = Variable::make(uint32, "b");
        ScopedBinding<Interval> ab(scope, "a", Interval(UIntImm::make(uint32, 0), simplify(uint32.max() / 4 + 2)));
        ScopedBinding<Interval> bb(scope, "b", Interval(UIntImm::make(uint32, 0), uint32.max()));
        // Overflow should be detected
        check(scope, a + b, Interval::neg_inf(), Interval::pos_inf());
        check(scope, a * b, Interval::neg_inf(), Interval::pos_inf());
    }
    {
        Type int16 = Int(16);
        Expr a = Variable::make(int16, "a");
        Expr b = Variable::make(int16, "b");
        ScopedBinding<Interval> ab(scope, "a", Interval(int16.min(), int16.max()));
        ScopedBinding<Interval> bb(scope, "b", Interval(IntImm::make(int16, -4), IntImm::make(int16, -1)));
        check(scope, a * -1, int16.min(), int16.max());
        // int16.min() / -1 should be caught as overflow.
        check(scope, a / -1, int16.min(), int16.max());
        check(scope, a / b, int16.min(), int16.max());
    }
    {
        Expr zero = UIntImm::make(UInt(1), 0);
        Expr one = UIntImm::make(UInt(1), 1);
        check(scope, Ramp::make(zero, one, 3), zero, one);
    }

    // If we clamp something unbounded as one type, the bounds should
    // propagate through casts whenever the cast can be proved to not
    // overflow.
    check(scope,
          cast<uint16_t>(clamp(cast<float>(x ^ y), 0.0f, 4095.0f)),
          u16(0), u16(4095));

    check(scope,
          cast<uint8_t>(clamp(cast<uint16_t>(x ^ y), make_zero(UInt(16)), make_const(UInt(16), 128))),
          u8(0), u8(128));

    Expr u8_1 = cast<uint8_t>(Load::make(Int(8), "buf", x, Buffer<>(), Parameter(), const_true(), ModulusRemainder()));
    Expr u8_2 = cast<uint8_t>(Load::make(Int(8), "buf", x + 17, Buffer<>(), Parameter(), const_true(), ModulusRemainder()));
    check(scope, cast<uint16_t>(u8_1) + cast<uint16_t>(u8_2),
          u16(0), u16(255 * 2));

    check(scope, saturating_cast<uint8_t>(clamp(x, 5, 10)), make_const(UInt(8), 5), make_const(UInt(8), 10));
    {
        scope.push("x", Interval(UInt(32).min(), UInt(32).max()));
        check(scope, saturating_cast<int32_t>(max(cast<uint32_t>(x), make_const(UInt(32), 5))), make_const(Int(32), 5), Int(32).max());
        scope.pop("x");
    }
    {
        Expr z = Variable::make(Float(32), "z");
        scope.push("z", Interval(make_const(Float(32), -1), make_one(Float(32))));
        check(scope, saturating_cast<int32_t>(z), make_const(Int(32), -1), make_one(Int(32)));
        check(scope, saturating_cast<double>(z), make_const(Float(64), -1), make_one(Float(64)));
        check(scope, saturating_cast<float16_t>(z), make_const(Float(16), -1), make_one(Float(16)));
        check(scope, saturating_cast<uint8_t>(z), make_zero(UInt(8)), make_one(UInt(8)));
        scope.pop("z");
    }
    {
        Expr z = Variable::make(UInt(32), "z");
        scope.push("z", Interval(UInt(32).max(), UInt(32).max()));
        check(scope, saturating_cast<int32_t>(z), Int(32).max(), Int(32).max());
        scope.pop("z");
    }

    {
        Scope<Interval> scope;
        Expr x = Variable::make(UInt(16), "x");
        Expr y = Variable::make(UInt(16), "y");
        scope.push("x", Interval(u16(0), u16(10)));
        scope.push("y", Interval(u16(2), u16(4)));

        Expr e = clamp(x / y, u16(0), u16(128));
        check(scope, e, u16(0), u16(5));
        check_constant_bound(scope, e, u16(0), u16(5));
    }

    {
        Param<int16_t> x("x");
        Param<uint16_t> y("y");
        x.set_range(i16(-32), i16(-16));
        y.set_range(i16(0), i16(4));
        check_constant_bound((x >> y), i16(-32), i16(-1));
    }

    {
        Param<uint16_t> x("x"), y("y");
        x.set_range(u16(10), u16(20));
        y.set_range(u16(0), u16(30));
        Scope<Interval> scope;
        scope.push("y", Interval(u16(2), u16(4)));

        check_constant_bound(scope, x + y, u16(12), u16(24));
    }

    {
        Scope<Interval> scope;
        Interval i = Interval::everything();
        i.min = 17;
        internal_assert(i.has_lower_bound());
        internal_assert(!i.has_upper_bound());
        scope.push("y", i);
        Var x("x"), y("y");
        check(scope, select(x == y * 2, y, y - 10),
              7, Interval::pos_inf());
        check(scope, select(x == y * 2, y - 10, y),
              select(x < 34, 17, 7), Interval::pos_inf());
    }

    vector<Expr> input_site_1 = {2 * x};
    vector<Expr> input_site_2 = {2 * x + 1};
    vector<Expr> output_site = {x + 1};

    Buffer<int32_t> in(10);
    in.set_name("input");

    Stmt loop = For::make("x", 3, 12, ForType::Serial, Partition::Auto, DeviceAPI::Host,
                          Provide::make("output",
                                        {Add::make(Call::make(in, input_site_1),
                                                   Call::make(in, input_site_2))},
                                        output_site,
                                        const_true()));

    map<string, Box> r;
    r = boxes_required(loop);
    internal_assert(r.find("output") == r.end());
    internal_assert(r.find("input") != r.end());
    internal_assert(equal(simplify(r["input"][0].min), 6));
    internal_assert(equal(simplify(r["input"][0].max), 25));
    r = boxes_provided(loop);
    internal_assert(r.find("output") != r.end());
    internal_assert(equal(simplify(r["output"][0].min), 4));
    internal_assert(equal(simplify(r["output"][0].max), 13));

    Box r2({Interval(Expr(5), Expr(19))});
    merge_boxes(r2, r["output"]);
    internal_assert(equal(simplify(r2[0].min), 4));
    internal_assert(equal(simplify(r2[0].max), 19));

    boxes_touched_test();

    // Check a deeply-nested bitwise expr to ensure it doesn't take n^2 time
    // (this clause took ~30s on a typical laptop before the fix, ~10ms after)
    {
        Expr a = Variable::make(UInt(16), "t42");
        Expr b = Variable::make(UInt(16), "t43");
        Expr c = Variable::make(UInt(16), "t44");
        Expr d = Variable::make(Int(32), "d");
        Expr x = Variable::make(Int(32), "x");
        Expr y = Variable::make(Int(32), "y");
        Expr e1 = select(c >= Expr((uint16_t)128), c - Expr((uint16_t)128), c);
        Expr e2 = Let::make("t44", (((((((((((((((((u16(0) << u16(1)) | u16((u8(d) & u8(1)))) << u16(1)) | u16(((u8(d) >> u8(1)) & u8(1)))) << u16(1)) | (u16(x) & u16(1))) << u16(1)) | (u16(y) & u16(1))) << u16(1)) | (a & u16(1))) << u16(1)) | (b & u16(1))) << u16(1)) | ((a >> u16(1)) & u16(1))) << u16(1)) | ((b >> u16(1)) & u16(1))) >> u16(1)), e1);
        Expr e3 = Let::make("t43", u16(y) >> u16(1), e2);
        Expr e4 = Let::make("t42", u16(x) >> u16(1), e3);

        check_constant_bound(e4, u16(0), u16(65535));
    }

    // Test case from https://github.com/halide/Halide/pull/7377
    {
        Var x;
        Expr e = Load::make(Int(32), "buf", max(x, -x), Buffer<>{}, Parameter{}, const_true(), ModulusRemainder{});
        e = Let::make(x.name(), 37, e);
        Scope<Interval> scope;
        scope.push("y", {0, 100});
        Interval in = bounds_of_expr_in_scope(e, scope);
        internal_assert(in.is_single_point());
    }

    // Test case from https://github.com/halide/Halide/pull/7379
    {
        Var x;
        Expr e = Load::make(Int(32), "buf", -x / x, Buffer<>{}, Parameter{}, const_true(), ModulusRemainder{});
        e = Let::make(x.name(), 37, e);
        Scope<Interval> scope;
        scope.push("y", {0, 100});
        Interval in = bounds_of_expr_in_scope(e, scope);
        internal_assert(in.is_single_point());
    }

    printf("Success!\n");
    return 0;
}
