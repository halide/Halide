#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

#define internal_assert _halide_user_assert

// Helper to wrap an expression in a statement using the expression
// that won't be simplified away.
Stmt not_no_op(Expr x) {
    x = Call::make(x.type(), "not_no_op", {x}, Call::Extern);
    return Evaluate::make(x);
}

void check_is_sio(const Expr &e) {
    Expr simpler = simplify(e);
    if (!Call::as_intrinsic(simpler, {Call::signed_integer_overflow})) {
        std::cerr
            << "\nSimplification failure:\n"
            << "Input: " << e << "\n"
            << "Output: " << simpler << "\n"
            << "Expected output: signed_integer_overflow(n)\n";
        abort();
    }
}

void check(const Expr &a, const Expr &b, const Scope<ModulusRemainder> &alignment = Scope<ModulusRemainder>()) {
    Expr simpler = simplify(a, true, Scope<Interval>(), alignment);
    if (!equal(simpler, b)) {
        std::cerr
            << "\nSimplification failure:\n"
            << "Input: " << a << "\n"
            << "Output: " << simpler << "\n"
            << "Expected output: " << b << "\n";
        abort();
    }
}

void check(const Stmt &a, const Stmt &b) {
    Stmt simpler = simplify(a);
    if (!equal(simpler, b)) {
        std::cerr
            << "\nSimplification failure:\n"
            << "Input:\n"
            << a << "\n"
            << "Output:\n"
            << simpler << "\n"
            << "Expected output:\n"
            << b << "\n";
        abort();
    }
}

void check_in_bounds(const Expr &a, const Expr &b, const Scope<Interval> &bi) {
    Expr simpler = simplify(a, true, bi);
    if (!equal(simpler, b)) {
        std::cerr
            << "\nSimplification failure:\n"
            << "Input: " << a << "\n"
            << "Output: " << simpler << "\n"
            << "Expected output: " << b << "\n";
        abort();
    }
}

// Helper functions to use in the tests below
Expr interleave_vectors(const std::vector<Expr> &e) {
    return Shuffle::make_interleave(e);
}

Expr concat_vectors(const std::vector<Expr> &e) {
    return Shuffle::make_concat(e);
}

Expr slice(const Expr &e, int begin, int stride, int w) {
    return Shuffle::make_slice(e, begin, stride, w);
}

Expr ramp(const Expr &base, const Expr &stride, int w) {
    return Ramp::make(base, stride, w);
}

Expr broadcast(const Expr &base, int w) {
    return Broadcast::make(base, w);
}

void check_casts() {
    Expr x = Var("x"), y = Var("y");

    check(cast(Int(32), cast(Int(32), x)), x);
    check(cast(Float(32), 3), 3.0f);
    check(cast(Int(32), 5.0f), 5);

    check(cast(Int(32), cast(Int(8), 3)), 3);
    check(cast(Int(32), cast(Int(8), 1232)), -48);

    // Check redundant casts
    check(cast(Float(32), cast(Float(64), x)), cast(Float(32), x));
    check(cast(Int(16), cast(Int(32), x)), cast(Int(16), x));
    check(cast(Int(16), cast(UInt(32), x)), cast(Int(16), x));
    check(cast(UInt(16), cast(Int(32), x)), cast(UInt(16), x));
    check(cast(UInt(16), cast(UInt(32), x)), cast(UInt(16), x));

    // Check evaluation of constant expressions involving casts
    check(cast(UInt(16), 53) + cast(UInt(16), 87), make_const(UInt(16), 140));
    check(cast(Int(8), 127) + cast(Int(8), 1), make_const(Int(8), -128));
    check(cast(UInt(16), -1) - cast(UInt(16), 1), make_const(UInt(16), 65534));
    check(cast(Int(16), 4) * cast(Int(16), -5), make_const(Int(16), -20));
    check(cast(Int(16), 16) / cast(Int(16), 4), make_const(Int(16), 4));
    check(cast(Int(16), 23) % cast(Int(16), 5), make_const(Int(16), 3));
    check(min(cast(Int(16), 30000), cast(Int(16), -123)), make_const(Int(16), -123));
    check(max(cast(Int(16), 30000), cast(Int(16), 65000)), make_const(Int(16), 30000));
    check(cast(UInt(16), -1) == cast(UInt(16), 65535), const_true());
    check(cast(UInt(16), 65) == cast(UInt(16), 66), const_false());
    check(cast(UInt(16), -1) < cast(UInt(16), 65535), const_false());
    check(cast(UInt(16), 65) < cast(UInt(16), 66), const_true());
    check(cast(UInt(16), 123.4f), make_const(UInt(16), 123));
    check(cast(Float(32), cast(UInt(16), 123456.0f)), 57920.0f);
    // Specific checks for 32 bit unsigned expressions - ensure simplifications are actually unsigned.
    // 4000000000 (4 billion) is less than 2^32 but more than 2^31.  As an int, it is negative.
    check(cast(UInt(32), (int)4000000000UL) + cast(UInt(32), 5), make_const(UInt(32), (int)4000000005UL));
    check(make_const(UInt(32, 4), (int)4000000000UL) - make_const(UInt(32, 4), 5), make_const(UInt(32, 4), (int)3999999995UL));
    check(cast(UInt(32), (int)4000000000UL) / cast(UInt(32), 5), make_const(UInt(32), 800000000));
    check(cast(UInt(32), 800000000) * cast(UInt(32), 5), make_const(UInt(32), (int)4000000000UL));
    check(make_const(UInt(32, 2), (int)4000000023UL) % make_const(UInt(32, 2), 100), make_const(UInt(32, 2), 23));
    check(min(cast(UInt(32), (int)4000000023UL), cast(UInt(32), 1000)), make_const(UInt(32), (int)1000));
    check(max(cast(UInt(32), (int)4000000023UL), cast(UInt(32), 1000)), make_const(UInt(32), (int)4000000023UL));
    check(cast(UInt(32), (int)4000000023UL) < cast(UInt(32), 1000), const_false());
    check(make_const(UInt(32, 3), (int)4000000023UL) == make_const(UInt(32, 3), 1000), const_false(3));

    check(cast(Float(64), 0.5f), Expr(0.5));
    check((x - cast(Float(64), 0.5f)) * (x - cast(Float(64), 0.5f)),
          (x + Expr(-0.5)) * (x + Expr(-0.5)));

    check(cast(Int(64, 3), ramp(5.5f, 2.0f, 3)),
          cast(Int(64, 3), ramp(5.5f, 2.0f, 3)));
    check(cast(Int(64, 3), ramp(x, 2, 3)),
          ramp(cast(Int(64), x), cast(Int(64), 2), 3));

    // We do not currently expect cancellations to occur through casts
    // check(cast(Int(64), x + 1) - cast(Int(64), x), cast(Int(64), 1));
    // check(cast(Int(64), 1 + x) - cast(Int(64), x), cast(Int(64), 1));

    // But only when overflow is undefined for the type
    check(cast(UInt(8), x + 1) - cast(UInt(8), x),
          cast(UInt(8), x + 1) - cast(UInt(8), x));

    // Overflow is well-defined for ints < 32 bits
    check(cast(Int(8), make_const(UInt(8), 128)), make_const(Int(8), -128));

    // Check that chains of widening casts don't lose the distinction
    // between zero-extending and sign-extending.
    check(cast(UInt(64), cast(UInt(32), cast(Int(8), -1))),
          UIntImm::make(UInt(64), 0xffffffffULL));

    // It's a good idea to pull widening casts outside of shuffles
    // when the shuffle reduces the lane count (e.g. a slice_vector).
    Expr some_vector = ramp(y, 2, 8) * ramp(x, 1, 8);
    check(slice(cast(UInt(64, 8), some_vector), 2, 1, 3),
          cast(UInt(64, 3), slice(some_vector, 2, 1, 3)));

    std::vector<int> indices(18);
    for (int i = 0; i < 18; i++) {
        indices[i] = i & 3;
    }
    check(Shuffle::make({cast(UInt(64, 8), some_vector)}, indices),
          Shuffle::make({cast(UInt(64, 8), some_vector)}, indices));

    // Interleaving simplifications can result in slices.
    Expr var_vector = Variable::make(Int(32, 12), "v");
    Expr even = Shuffle::make_slice(var_vector, 0, 2, 4);
    Expr odd = Shuffle::make_slice(var_vector, 1, 2, 4);
    check(Shuffle::make_interleave({even, odd}), Shuffle::make_slice(var_vector, 0, 1, 8));
}

void check_algebra() {
    Expr x = Var("x"), y = Var("y"), z = Var("z"), w = Var("w"), v = Var("v");
    Expr xf = cast<float>(x);
    Expr yf = cast<float>(y);
    Expr t = const_true(), f = const_false();

    check(3 + x, x + 3);
    check(x + 0, x);
    check(0 + x, x);
    check(Expr(ramp(x, 2, 3)) + Expr(ramp(y, 4, 3)), ramp(x + y, 6, 3));
    check(Expr(broadcast(4.0f, 5)) + Expr(ramp(3.25f, 4.5f, 5)), ramp(7.25f, 4.5f, 5));
    check(Expr(ramp(3.25f, 4.5f, 5)) + Expr(broadcast(4.0f, 5)), ramp(7.25f, 4.5f, 5));
    check(Expr(broadcast(3, 3)) + Expr(broadcast(1, 3)), broadcast(4, 3));
    check((x + 3) + 4, x + 7);
    check(4 + (3 + x), x + 7);
    check((x + 3) + y, (x + y) + 3);
    check(y + (x + 3), (x + y) + 3);
    check((3 - x) + x, 3);
    check(x + (3 - x), 3);
    check(x * y + x * z, (y + z) * x);
    check(x * y + z * x, (y + z) * x);
    check(y * x + x * z, (y + z) * x);
    check(y * x + z * x, (y + z) * x);

    check(x - 0, x);
    check((x / y) - (x / y), 0);
    check(x - 2, x + (-2));
    check(Expr(ramp(x, 2, 3)) - Expr(ramp(y, 4, 3)), ramp(x - y, -2, 3));
    check(Expr(broadcast(4.0f, 5)) - Expr(ramp(3.25f, 4.5f, 5)), ramp(0.75f, -4.5f, 5));
    check(Expr(ramp(3.25f, 4.5f, 5)) - Expr(broadcast(4.0f, 5)), ramp(-0.75f, 4.5f, 5));
    check(Expr(broadcast(3, 3)) - Expr(broadcast(1, 3)), broadcast(2, 3));
    check((x + y) - x, y);
    check((x + y) - y, x);
    check(x - (x + y), 0 - y);
    check(x - (y + x), 0 - y);
    check((x + 3) - 2, x + 1);
    check((x + 3) - y, (x - y) + 3);
    check((x - 3) - y, (x - y) + (-3));
    check(x - (y - 2), (x - y) + 2);
    check(3 - (y - 2), 5 - y);
    check(x - (0 - y), x + y);
    check(x + (0 - y), x - y);
    check((0 - x) + y, y - x);
    check(x * y - x * z, (y - z) * x);
    check(x * y - z * x, (y - z) * x);
    check(y * x - x * z, (y - z) * x);
    check(y * x - z * x, (y - z) * x);

    check((x * 8) - (y * 4), (x * 2 - y) * 4);
    check((x * 4) - (y * 8), (x - y * 2) * 4);

    check((x * 2) % 6, (x % 3) * 2);

    check(x - (x / 8) * 8, x % 8);
    check((x / 8) * 8 - x, -(x % 8));
    check((x / 8) * 8 < x + y, 0 < x % 8 + y);
    check((x / 8) * 8 < x - y, y < x % 8);
    check((x / 8) * 8 < x, x % 8 != 0);
    check(((x + 3) / 8) * 8 < x + y, 3 < (x + 3) % 8 + y);
    check(((x + 3) / 8) * 8 < x - y, y < (x + 3) % 8 + (-3));
    check(((x + 3) / 8) * 8 < x, 3 < (x + 3) % 8);

    check(x * 0, 0);
    check(0 * x, 0);
    check(x * 1, x);
    check(1 * x, x);
    check(Expr(2.0f) * 4.0f, 8.0f);
    check(Expr(2) * 4, 8);
    check((3 * x) * 4, x * 12);
    check(4 * (3 + x), x * 4 + 12);
    check(Expr(broadcast(4.0f, 5)) * Expr(ramp(3.0f, 4.0f, 5)), ramp(12.0f, 16.0f, 5));
    check(Expr(ramp(3.0f, 4.0f, 5)) * Expr(broadcast(2.0f, 5)), ramp(6.0f, 8.0f, 5));
    check(Expr(broadcast(3, 3)) * Expr(broadcast(2, 3)), broadcast(6, 3));

    check(x * y + x, (y + 1) * x);
    check(x * y - x, (y + -1) * x);
    check(x + x * y, (y + 1) * x);
    check(x - x * y, (1 - y) * x);
    check(x * y + y, (x + 1) * y);
    check(x * y - y, (x + -1) * y);
    check(y + x * y, (x + 1) * y);
    check(y - x * y, (1 - x) * y);

    check(0 / max(x, 1), 0);
    check(x / 1, x);
    check(max(x, 1) / (max(x, 1)), 1);
    check(min(x, -1) / (min(x, -1)), 1);
    check((x * 2 + 1) / (x * 2 + 1), 1);
    check((-1) / (x * 2 + 1), select(x < 0, 1, -1));
    check(Expr(7) / 3, 2);
    check(Expr(6.0f) / 2.0f, 3.0f);
    check((x / 3) / 4, x / 12);
    check((x * 4) / 2, x * 2);
    check((x * 2) / 4, x / 2);
    check((x * (-4)) / 2, x * (-2));
    check((x * 4 + y) / 2, y / 2 + x * 2);
    check((y + x * 4) / 2, y / 2 + x * 2);
    check((x * 2 - y) / 2, (0 - y) / 2 + x);
    check((x * -2 - y) / 2, (0 - y) / 2 - x);
    check((y - x * 4) / 2, y / 2 - x * 2);
    check((x + 3) / 2 + 7, (x + 17) / 2);
    check((x / 2 + 3) / 5, (x + 6) / 10);
    check((x + (y + 3) / 5) + 5, (y + 28) / 5 + x);
    check((x + 8) / 2, x / 2 + 4);
    check((x - y) * -2, (y - x) * 2);
    check((xf - yf) * -2.0f, (yf - xf) * 2.0f);

    check(x * 3 + y * 9, (y * 3 + x) * 3);
    check(x * 9 + y * 3, (x * 3 + y) * 3);

    // Pull terms that are a multiple of the divisor out of a ternary expression
    check(((x * 4 + y) + z) / 2, (y + z) / 2 + x * 2);
    check(((x * 4 - y) + z) / 2, (z - y) / 2 + x * 2);
    check(((x * 4 + y) - z) / 2, (y - z) / 2 + x * 2);
    check(((x * 2 - y) - z) / 2, (0 - y - z) / 2 + x);
    check(((x * -2 - y) - z) / 2, (0 - y - z) / 2 - x);
    check((x + (y * 4 + z)) / 2, (x + z) / 2 + y * 2);
    check(((x + y * 4) + z) / 2, (x + z) / 2 + y * 2);
    check((x + (y * 4 - z)) / 2, (x - z) / 2 + y * 2);
    check((x - (y * 4 + z)) / 2, (x - z) / 2 + y * -2);
    check((x - (y * 4 - z)) / 2, (x + z) / 2 - y * 2);

    // Pull out the gcd of the numerator and divisor
    check((x * 3 + 5) / 9, (x + 1) / 3);

    // Cancellations in integer divisions.
    check((7 * y) / 7, y);
    check((y * 7) / 7, y);
    check((7 * y + z) / 7, z / 7 + y);
    check((y * 7 + z) / 7, z / 7 + y);
    check((z + 7 * y) / 7, z / 7 + y);
    check((z + y * 7) / 7, z / 7 + y);
    check((7 * y - z) / 7, (-z) / 7 + y);
    check((y * 7 - z) / 7, (-z) / 7 + y);
    check((z - 7 * y) / 7, z / 7 - y);
    check((z - y * 7) / 7, z / 7 - y);

    check((7 + y) / 7, y / 7 + 1);
    check((y + 7) / 7, y / 7 + 1);
    check((7 - y) / 7, (-y) / 7 + 1);
    check((y - 7) / 7, y / 7 + (-1));

    Scope<ModulusRemainder> alignment;
    alignment.push("x", ModulusRemainder(2, 0));
    check((x + 0) / 2, x / 2, alignment);
    check((x + 1) / 2, x / 2, alignment);
    check((x + 2) / 2, x / 2 + 1, alignment);
    check((x + 3) / 2, x / 2 + 1, alignment);
    alignment.pop("x");
    alignment.push("x", ModulusRemainder(2, 1));
    check((x + 0) / 2, x / 2, alignment);
    check((x + 1) / 2, x / 2 + 1, alignment);
    check((x + 2) / 2, x / 2 + 1, alignment);
    check((x + 3) / 2, x / 2 + 2, alignment);
    alignment.pop("x");
    alignment.push("x", ModulusRemainder(3, 0));
    check((x + 0) / 3, x / 3, alignment);
    check((x + 1) / 3, x / 3, alignment);
    check((x + 2) / 3, x / 3, alignment);
    check((x + 3) / 3, x / 3 + 1, alignment);
    check((x + 4) / 3, x / 3 + 1, alignment);
    check((x + 5) / 3, x / 3 + 1, alignment);
    alignment.pop("x");
    alignment.push("x", ModulusRemainder(3, 1));
    check((x + 0) / 3, x / 3, alignment);
    check((x + 1) / 3, x / 3, alignment);
    check((x + 2) / 3, x / 3 + 1, alignment);
    check((x + 3) / 3, x / 3 + 1, alignment);
    check((x + 4) / 3, x / 3 + 1, alignment);
    check((x + 5) / 3, x / 3 + 2, alignment);
    alignment.pop("x");
    alignment.push("x", ModulusRemainder(3, 2));
    check((x + 0) / 3, x / 3, alignment);
    check((x + 1) / 3, x / 3 + 1, alignment);
    check((x + 2) / 3, x / 3 + 1, alignment);
    check((x + 3) / 3, x / 3 + 1, alignment);
    check((x + 4) / 3, x / 3 + 2, alignment);
    check((x + 5) / 3, x / 3 + 2, alignment);
    alignment.pop("x");
    alignment.push("x", ModulusRemainder(4, 0));
    check((x + 0) / 2, x / 2, alignment);
    check((x + 1) / 2, x / 2, alignment);
    check((x + 2) / 2, x / 2 + 1, alignment);
    check((x + 3) / 2, x / 2 + 1, alignment);
    alignment.pop("x");
    alignment.push("x", ModulusRemainder(4, 1));
    check((x + 0) / 2, x / 2, alignment);
    check((x + 1) / 2, x / 2 + 1, alignment);
    check((x + 2) / 2, x / 2 + 1, alignment);
    check((x + 3) / 2, x / 2 + 2, alignment);
    alignment.pop("x");
    alignment.push("x", ModulusRemainder(2, 0));
    check((x + 0) / 3, x / 3, alignment);
    check((x + 1) / 3, (x + 1) / 3, alignment);
    check((x + 2) / 3, (x + 2) / 3, alignment);
    check((x + 3) / 3, x / 3 + 1, alignment);
    check((x + 4) / 3, (x + 4) / 3, alignment);
    check((x + 5) / 3, (x + 5) / 3, alignment);
    alignment.pop("x");

    check(((7 + y) + z) / 7, (y + z) / 7 + 1);
    check(((y + 7) + z) / 7, (y + z) / 7 + 1);
    check((y + (7 + z)) / 7, (y + z) / 7 + 1);
    check((y + (z + 7)) / 7, (y + z) / 7 + 1);

    check(xf / 4.0f, xf * 0.25f);

    // Some quaternary rules with cancellations
    check((x + y) - (y + z), x - z);
    check((x + y) - (y + z), x - z);
    check((y + x) - (y + z), x - z);
    check((y + x) - (y + z), x - z);

    check((x - y) - (z - y), x - z);
    check((y - z) - (y - x), x - z);

    check(((x + y) + z) - x, y + z);
    check(((x + y) + z) - y, x + z);
    check((x + (y + z)) - y, x + z);
    check((x + (y + z)) - z, x + y);

    check((x * 8) % 4, 0);
    check((x * 8 + y) % 4, y % 4);
    check((y + 8) % 4, y % 4);
    check((y + x * 8) % 4, y % 4);
    check((y * 16 - 13) % 2, 1);
    check((x * y) % 1, 0);

    check((y * 16 - 13) % 2, 1);
    check((y - 8) % 4, y % 4);
    check((y - x * 8) % 4, y % 4);
    check((x * 8 - y) % 4, (-y) % 4);

    // Check an optimization important for fusing dimensions
    check((x / 3) * 3 + x % 3, x);
    check(x % 3 + (x / 3) * 3, x);

    check(((x / 3) * 3 + y) + x % 3, x + y);
    check(((x / 3) + y) * 3 + x % 3, y * 3 + x);
    check((x % 3 + y) + (x / 3) * 3, x + y);

    check((y + x % 3) + (x / 3) * 3, x + y);
    check((y + (x / 3 * 3)) + x % 3, x + y);
    check((y + (x / 3)) * 3 + x % 3, y * 3 + x);

    check(x / 2 + x % 2, (x + 1) / 2);
    check(x % 2 + x / 2, (x + 1) / 2);
    check(((x + 1) / 2) * 2 - x, x % 2);
    check(((x + 2) / 3) * 3 - x, (-x) % 3);
    check(x - ((x + 1) / 2) * 2, ((x + 1) % 2 + -1));
    check(x - ((x + 2) / 3) * 3, ((x + 2) % 3 + -2));
    check((x % 2 + 4) / 2, 2);
    check((x % 2 + 5) / 2, x % 2 + 2);

    // Almost-cancellations through integer divisions. These rules all
    // deduplicate x and wrap it in a modulo operator, neutering it
    // for the purposes of bounds inference. Patterns below look
    // confusing, but were brute-force tested.
    check((x + 17) / 3 - (x + 7) / 3, ((x + 1) % 3 + 10) / 3);
    check((x + 17) / 3 - (x + y) / 3, (19 - y - (x + 2) % 3) / 3);
    check((x + y) / 3 - (x + 7) / 3, ((x + 1) % 3 + y + -7) / 3);
    check(x / 3 - (x + y) / 3, (2 - y - x % 3) / 3);
    check((x + y) / 3 - x / 3, (x % 3 + y) / 3);
    check(x / 3 - (x + 7) / 3, (-5 - x % 3) / 3);
    check((x + 17) / 3 - x / 3, (x % 3 + 17) / 3);
    check((x + 17) / 3 - (x - y) / 3, (y - (x + 2) % 3 + 19) / 3);
    check((x - y) / 3 - (x + 7) / 3, ((x + 1) % 3 - y + (-7)) / 3);
    check(x / 3 - (x - y) / 3, (y - x % 3 + 2) / 3);
    check((x - y) / 3 - x / 3, (x % 3 - y) / 3);

    // Check some specific expressions involving div and mod
    check(Expr(23) / 4, Expr(5));
    check(Expr(-23) / 4, Expr(-6));
    check(Expr(-23) / -4, Expr(6));
    check(Expr(23) / -4, Expr(-5));
    check(Expr(-2000000000) / 1000000001, Expr(-2));
    check(Expr(23) % 4, Expr(3));
    check(Expr(-23) % 4, Expr(1));
    check(Expr(-23) % -4, Expr(1));
    check(Expr(23) % -4, Expr(3));
    check(Expr(-2000000000) % 1000000001, Expr(2));

    check(Expr(3) + Expr(8), 11);
    check(Expr(3.25f) + Expr(7.75f), 11.0f);

    check(Expr(7) % 2, 1);
    check(Expr(7.25f) % 2.0f, 1.25f);
    check(Expr(-7.25f) % 2.0f, 0.75f);
    check(Expr(-7.25f) % -2.0f, -1.25f);
    check(Expr(7.25f) % -2.0f, -0.75f);

    check(2 * x + (2 * x + y) / 5, (x * 12 + y) / 5);
    check(x + (x - y) / 4, (x * 5 - y) / 4);
    check((x + z) + (y + (x + z)) / 3, ((x + z) * 4 + y) / 3);
    check(x + ((y + w) - x) / 2, ((w + y) + x) / 2);
    check((x + y) / 3 + x, (x * 4 + y) / 3);
    check((x - y) / 4 + x, (x * 5 - y) / 4);
    check((y + x) / 3 + x, (x * 4 + y) / 3);
    check((y - x) / 3 + x, (x * 2 + y) / 3);
    check(1 + (1 + y) / 2, (y + 3) / 2);
    check((y + 1) / 2 + 1, (y + 3) / 2);
    check((0 - y) / 5 + 1, (0 - y) / 5 + 1);

    check(x - (x + y) / 3, (x * 2 - y + 2) / 3);
    check((w + x) - ((w + x) - y * z) / 3, ((w + x) * 2 + y * z + 2) / 3);
    check(x - (y + x) / 2, (x - y + 1) / 2);
    check(x - (y - x) / 6, (x * 7 - y + 5) / 6);
    check(x - (x + y) / -3, x - (x + y) / -3);
    check((w + x) - ((w + x) - y * z) / -3, (w + x) - ((w + x) - y * z) / -3);
    check(x - (y + x) / -2, x - (x + y) / -2);
    check(x - (y - x) / -6, x - (y - x) / -6);
    check((x + y) / 3 - x, (x * -2 + y) / 3);
    check((x * y - w) / 4 - x * y, (x * y * (-3) - w) / 4);
    check((y + x) / 5 - x, (x * -4 + y) / 5);
    check((y - x) / 6 - x, (y - x * 7) / 6);
    check(1 - (1 + y) / 2 - 1, (0 - y) / 2);
    check(1 - (-y + 1) / 2 - 1, y / 2);
    check(1 - (0 - y) / 5, (y + 9) / 5);

    // Div/mod can't make things larger
    check(5 / x < 6, const_true());
    check(5 / x > -6, const_true());
    check(5 / x < 5, 5 / x < 5);
    check(5 / x > -5, -5 < 5 / x);
    check(5 % x < 6, const_true());
    check(5 % x < 5, 5 % x < 5);
    check(5 % x >= 0, const_true());
    check(5 % x > 0, 5 % x != 0);

    // Test case with most negative 32-bit number, as constant to check that it is not negated.
    check(((x * (int32_t)0x80000000) + (z * (int32_t)0x80000000 + y)),
          ((x * (int32_t)0x80000000) + (z * (int32_t)0x80000000 + y)));

    // Use a require with no error message to test chains of reasoning
    auto require = [](Expr cond, Expr val) {
        return Internal::Call::make(val.type(),
                                    Internal::Call::require,
                                    {cond, val, 0},
                                    Internal::Call::PureIntrinsic);
    };

    check(require(2 < x && x < 4, x),
          require(2 < x && x < 4, 3));

    check(require(2 < x && x < 5 && x % 4 == 0, x),
          require(2 < x && x < 5 && x % 4 == 0, 4));

    check(require(x % 4 == 3, x % 2),
          require(x % 4 == 3, 1));

    // Check modulo of expressions that are not-obviously a multiple of something
    check(max(min(x * 8, 32), y * 16) % 4 == 0, const_true());
    check(select(x > 4, x * 9 + 1, y * 6 - 2) % 3 == 1, const_true());
    check(max(32, x * 4) % 16 < 13, const_true());  // After the %16 the max value is 12, not 15, due to alignment

    Expr complex_cond = ((10 < y) && (y % 17 == 4) && (y < 30) && (x == y * 16 + 3));
    // The condition is enough to imply that y == 21, x == 339
    check(require(complex_cond, select(x % 2 == 0, 1237, y)),
          require(complex_cond, 21));
}

void check_vectors() {
    Expr x = Var("x"), y = Var("y"), z = Var("z");

    check(Expr(broadcast(y, 4)) / Expr(broadcast(x, 4)),
          Expr(broadcast(y / x, 4)));
    check(Expr(ramp(x, 4, 4)) / 2, ramp(x / 2, 2, 4));
    check(Expr(ramp(x, -4, 7)) / 2, ramp(x / 2, -2, 7));
    check(Expr(ramp(x, 4, 5)) / -2, ramp(x / -2, -2, 5));
    check(Expr(ramp(x, -8, 5)) / -2, ramp(x / -2, 4, 5));

    check(Expr(ramp(4 * x, 1, 4)) / 4, broadcast(x, 4));
    check(Expr(ramp(x * 4, 1, 3)) / 4, broadcast(x, 3));
    check(Expr(ramp(x * 8, 2, 4)) / 8, broadcast(x, 4));
    check(Expr(ramp(x * 8, 3, 3)) / 8, broadcast(x, 3));
    check(Expr(ramp(0, 1, 8)) % 16, Expr(ramp(0, 1, 8)));
    check(Expr(ramp(8, 1, 8)) % 16, Expr(ramp(8, 1, 8)));
    check(Expr(ramp(9, 1, 8)) % 16, Expr(ramp(9, 1, 8)) % 16);
    check(Expr(ramp(16, 1, 8)) % 16, Expr(ramp(0, 1, 8)));
    check(Expr(ramp(0, 1, 8)) % 8, Expr(ramp(0, 1, 8)));
    check(Expr(ramp(x * 8 + 17, 1, 4)) % 8, Expr(ramp(1, 1, 4)));
    check(Expr(ramp(x * 8 + 17, 1, 8)) % 8, Expr(ramp(1, 1, 8) % 8));

    check(Expr(broadcast(x, 4)) % Expr(broadcast(y, 4)),
          Expr(broadcast(x % y, 4)));
    check(Expr(ramp(x, 2, 4)) % (broadcast(2, 4)),
          broadcast(x % 2, 4));
    check(Expr(ramp(2 * x + 1, 4, 4)) % (broadcast(2, 4)),
          broadcast(1, 4));

    check(max(broadcast(24, 2), broadcast(x, 2) % ramp(-8, -33, 2)),
          max(broadcast(x, 2) % ramp(-8, -33, 2), broadcast(24, 2)));
    check(max(broadcast(41, 2), broadcast(x, 2) % ramp(-8, -33, 2)),
          broadcast(41, 2));

    check(ramp(0, 1, 4) == broadcast(2, 4),
          ramp(-2, 1, 4) == broadcast(0, 4));

    check(ramp(broadcast(0, 6), broadcast(6, 6), 4) + broadcast(ramp(0, 1, 3), 8) +
              broadcast(ramp(broadcast(0, 3), broadcast(3, 3), 2), 4),
          ramp(0, 1, 24));

    // Any linear combination of simple ramps and broadcasts should
    // reduce to a single ramp or broadcast.
    std::mt19937 rng(0);
    for (int i = 0; i < 50; i++) {
        std::vector<Expr> leaves =
            {ramp(x, 1, 4),
             ramp(x, y, 4),
             ramp(z, x, 4),
             broadcast(x, 4),
             broadcast(y, 4),
             broadcast(z, 4)};
        while (leaves.size() > 1) {
            int idx1 = rng() % (int)leaves.size();
            int idx2 = 0;
            do {
                idx2 = rng() % (int)leaves.size();
            } while (idx2 == idx1);

            switch (rng() % 4) {
            case 0:
                leaves[idx1] += leaves[idx2];
                break;
            case 1:
                leaves[idx1] -= leaves[idx2];
                break;
            case 2:
                leaves[idx1] += (int)(rng() % 8) * leaves[idx2];
                break;
            case 3:
                leaves[idx1] -= (int)(rng() % 8) * leaves[idx2];
                break;
            }
            std::swap(leaves[idx2], leaves.back());
            leaves.pop_back();
        }
        Expr simpler = simplify(leaves[0]);
        if (!simpler.as<Ramp>() && !simpler.as<Broadcast>()) {
            std::cerr << "A linear combination of ramps and broadcasts should be a single ramp or broadcast:\n"
                      << simpler << "\n";
            abort();
        }
    }

    {
        Expr test = select(ramp(const_true(), const_true(), 2),
                           ramp(const_false(), const_true(), 2),
                           broadcast(const_false(), 2)) ==
                    broadcast(const_false(), 2);
        Expr expected = !(ramp(const_true(), const_true(), 2) &&
                          ramp(const_false(), const_true(), 2));
        check(test, expected);
    }

    {
        Expr test = select(ramp(const_true(), const_true(), 2),
                           broadcast(const_true(), 2),
                           ramp(const_false(), const_true(), 2)) ==
                    broadcast(const_false(), 2);
        Expr expected = !(ramp(const_true(), const_true(), 2) ||
                          ramp(const_false(), const_true(), 2));
        check(test, expected);
    }

    // Collapse some vector interleaves
    check(interleave_vectors({ramp(x, 2, 4), ramp(x + 1, 2, 4)}), ramp(x, 1, 8));
    check(interleave_vectors({ramp(x, 4, 4), ramp(x + 2, 4, 4)}), ramp(x, 2, 8));
    check(interleave_vectors({ramp(x - y, 2 * y, 4), ramp(x, 2 * y, 4)}), ramp(x - y, y, 8));
    check(interleave_vectors({ramp(x, 3, 4), ramp(x + 1, 3, 4), ramp(x + 2, 3, 4)}), ramp(x, 1, 12));
    {
        Expr vec = ramp(x, 1, 16);
        check(interleave_vectors({slice(vec, 0, 2, 8), slice(vec, 1, 2, 8)}), vec);
        check(interleave_vectors({slice(vec, 0, 4, 4), slice(vec, 1, 4, 4), slice(vec, 2, 4, 4), slice(vec, 3, 4, 4)}), vec);
    }

    // Collapse some vector concats
    check(concat_vectors({ramp(x, 2, 4), ramp(x + 8, 2, 4)}), ramp(x, 2, 8));
    check(concat_vectors({ramp(x, 3, 2), ramp(x + 6, 3, 2), ramp(x + 12, 3, 2)}), ramp(x, 3, 6));

    // Now some ones that can't work
    {
        Expr e = interleave_vectors({ramp(x, 2, 4), ramp(x, 2, 4)});
        check(e, e);
        e = interleave_vectors({ramp(x, 2, 4), ramp(x + 2, 2, 4)});
        check(e, e);
        e = interleave_vectors({ramp(x, 3, 4), ramp(x + 1, 3, 4)});
        check(e, e);
        e = interleave_vectors({ramp(x, 2, 4), ramp(y + 1, 2, 4)});
        check(e, e);
        e = interleave_vectors({ramp(x, 2, 4), ramp(x + 1, 3, 4)});
        check(e, e);

        e = concat_vectors({ramp(x, 1, 4), ramp(x + 4, 2, 4)});
        check(e, e);
        e = concat_vectors({ramp(x, 1, 4), ramp(x + 8, 1, 4)});
        check(e, e);
        e = concat_vectors({ramp(x, 1, 4), ramp(y + 4, 1, 4)});
        check(e, e);
    }

    // Now check that an interleave of some collapsible loads collapses into a single dense load
    {
        Expr load1 = Load::make(Float(32, 4), "buf", ramp(x, 2, 4), Buffer<>(), Parameter(), const_true(4), ModulusRemainder());
        Expr load2 = Load::make(Float(32, 4), "buf", ramp(x + 1, 2, 4), Buffer<>(), Parameter(), const_true(4), ModulusRemainder());
        Expr load12 = Load::make(Float(32, 8), "buf", ramp(x, 1, 8), Buffer<>(), Parameter(), const_true(8), ModulusRemainder());
        check(interleave_vectors({load1, load2}), load12);

        // They don't collapse in the other order
        Expr e = interleave_vectors({load2, load1});
        check(e, e);

        // Or if the buffers are different
        Expr load3 = Load::make(Float(32, 4), "buf2", ramp(x + 1, 2, 4), Buffer<>(), Parameter(), const_true(4), ModulusRemainder());
        e = interleave_vectors({load1, load3});
        check(e, e);
    }

    // Check that concatenated loads of adjacent scalars collapse into a vector load.
    {
        int lanes = 4;
        std::vector<Expr> loads;
        for (int i = 0; i < lanes; i++) {
            loads.push_back(Load::make(Float(32), "buf", 4 * x + i, Buffer<>(), Parameter(), const_true(), ModulusRemainder()));
        }

        check(concat_vectors(loads), Load::make(Float(32, lanes), "buf", ramp(x * 4, 1, lanes), Buffer<>(), Parameter(), const_true(lanes), ModulusRemainder(4, 0)));
    }

    // Check that concatenated loads of adjacent vectors collapse into a vector load, with appropriate alignment.
    {
        int lanes = 4;
        int vectors = 4;
        std::vector<Expr> loads;
        for (int i = 0; i < vectors; i++) {
            loads.push_back(Load::make(Float(32, lanes), "buf", ramp(i * lanes, 1, lanes), Buffer<>(), Parameter(), const_true(lanes), ModulusRemainder(4, 0)));
        }

        check(concat_vectors(loads), Load::make(Float(32, lanes * vectors), "buf", ramp(0, 1, lanes * vectors), Buffer<>(), Parameter(), const_true(vectors * lanes), ModulusRemainder(0, 0)));
    }

    {
        Expr vx = Variable::make(Int(32, 32), "x");
        Expr vy = Variable::make(Int(32, 32), "y");
        Expr vz = Variable::make(Int(32, 8), "z");
        Expr vw = Variable::make(Int(32, 16), "w");
        // Check that vector slices are hoisted.
        check(slice(vx, 0, 2, 8) + slice(vy, 0, 2, 8), slice(vx + vy, 0, 2, 8));
        check(slice(vx, 0, 2, 8) + (slice(vy, 0, 2, 8) + vz), slice(vx + vy, 0, 2, 8) + vz);
        check(slice(vx, 0, 2, 8) + (vz + slice(vy, 0, 2, 8)), slice(vx + vy, 0, 2, 8) + vz);
        // Check that degenerate vector slices are not hoisted.
        check(slice(vx, 0, 2, 1) + slice(vy, 0, 2, 1), slice(vx, 0, 2, 1) + slice(vy, 0, 2, 1));
        check(slice(vx, 0, 2, 1) + (slice(vy, 0, 2, 1) + z), slice(vx, 0, 2, 1) + (slice(vy, 0, 2, 1) + z));
        // Check slices are only hoisted when the lanes of the sliced vectors match.
        check(slice(vx, 0, 2, 8) + slice(vw, 0, 2, 8), slice(vx, 0, 2, 8) + slice(vw, 0, 2, 8));
        check(slice(vx, 0, 2, 8) + (slice(vw, 0, 2, 8) + vz), slice(vx, 0, 2, 8) + (slice(vw, 0, 2, 8) + vz));
    }

    {
        // A predicated store with a provably-false predicate.
        Expr pred = ramp(x * y + x * z, 2, 8) > 2;
        Expr index = ramp(x + y, 1, 8);
        Expr value = Load::make(index.type(), "f", index, Buffer<>(), Parameter(), const_true(index.type().lanes()), ModulusRemainder());
        Stmt stmt = Store::make("f", value, index, Parameter(), pred, ModulusRemainder());
        check(stmt, Evaluate::make(0));
    }

    auto make_allocation = [](const char *name, Type t, Stmt body) {
        return Allocate::make(name, t.element_of(), MemoryType::Stack, {t.lanes()}, const_true(), body);
    };

    {
        // A store completely out of bounds.
        Expr index = ramp(-8, 1, 8);
        Expr value = Broadcast::make(0, 8);
        Stmt stmt = Store::make("f", value, index, Parameter(), const_true(8), ModulusRemainder(8, 0));
        stmt = make_allocation("f", value.type(), stmt);
        check(stmt, Evaluate::make(unreachable()));
    }

    {
        // A store with one lane in bounds at the min.
        Expr index = ramp(-7, 1, 8);
        Expr value = Broadcast::make(0, 8);
        Stmt stmt = Store::make("f", value, index, Parameter(), const_true(8), ModulusRemainder(0, -7));
        stmt = make_allocation("f", value.type(), stmt);
        check(stmt, stmt);
    }

    {
        // A store with one lane in bounds at the max.
        Expr index = ramp(7, 1, 8);
        Expr value = Broadcast::make(0, 8);
        Stmt stmt = Store::make("f", value, index, Parameter(), const_true(8), ModulusRemainder(0, 7));
        stmt = make_allocation("f", value.type(), stmt);
        check(stmt, stmt);
    }

    {
        // A store completely out of bounds.
        Expr index = ramp(8, 1, 8);
        Expr value = Broadcast::make(0, 8);
        Stmt stmt = Store::make("f", value, index, Parameter(), const_true(8), ModulusRemainder(8, 0));
        stmt = make_allocation("f", value.type(), stmt);
        check(stmt, Evaluate::make(unreachable()));
    }

    Expr bool_vector = Variable::make(Bool(4), "bool_vector");
    Expr int_vector = Variable::make(Int(32, 4), "int_vector");
    check(VectorReduce::make(VectorReduce::And, Broadcast::make(bool_vector, 4), 1),
          VectorReduce::make(VectorReduce::And, bool_vector, 1));
    check(VectorReduce::make(VectorReduce::Or, Broadcast::make(bool_vector, 4), 2),
          VectorReduce::make(VectorReduce::Or, bool_vector, 2));
    check(VectorReduce::make(VectorReduce::Min, Broadcast::make(int_vector, 4), 4),
          int_vector);
    check(VectorReduce::make(VectorReduce::Max, Broadcast::make(int_vector, 4), 8),
          VectorReduce::make(VectorReduce::Max, Broadcast::make(int_vector, 4), 8));
}

void check_bounds() {
    Expr x = Var("x"), y = Var("y"), z = Var("z"), w = Var("w");

    check(min(Expr(7), 3), 3);
    check(min(Expr(4.25f), 1.25f), 1.25f);
    check(min(broadcast(x, 4), broadcast(y, 4)),
          broadcast(min(x, y), 4));
    check(min(x, x + 3), x);
    check(min(x + 4, x), x);
    check(min(x - 1, x + 2), x + (-1));
    check(min(7, min(x, 3)), min(x, 3));
    check(min(min(x, y), x), min(x, y));
    check(min(min(x, y), y), min(x, y));
    check(min(x, min(x, y)), min(x, y));
    check(min(y, min(x, y)), min(x, y));

    check(min(min(x, y) + 1, x), min(y + 1, x));
    check(min(min(x, y) - (-1), x), min(y + 1, x));
    check(min(min(x, y) + (-1), x), min(x, y) + (-1));
    check(min(min(x, y) - 1, x), min(x, y) + (-1));

    check(min(min(y, x) + 1, x), min(y + 1, x));
    check(min(min(y, x) - (-1), x), min(y + 1, x));
    check(min(min(y, x) + (-1), x), min(x, y) + (-1));
    check(min(min(y, x) - 1, x), min(x, y) + (-1));

    check(max(max(x, y) - 1, x), max(y + (-1), x));
    check(max(max(x, y) + (-1), x), max(y + (-1), x));
    check(max(max(x, y) + 1, x), max(x, y) + 1);
    check(max(max(x, y) - (-1), x), max(x, y) + 1);

    check(max(max(y, x) - 1, x), max(y + (-1), x));
    check(max(max(y, x) + (-1), x), max(y + (-1), x));
    check(max(max(y, x) + 1, x), max(x, y) + 1);
    check(max(max(y, x) - (-1), x), max(x, y) + 1);

    check(min(x, min(x, y) + 1), min(y + 1, x));
    check(min(x, min(x, y) - (-1)), min(y + 1, x));
    check(min(x, min(x, y) + (-1)), min(x, y) + (-1));
    check(min(x, min(x, y) - 1), min(x, y) + (-1));

    check(min(x, min(y, x) + 1), min(y + 1, x));
    check(min(x, min(y, x) - (-1)), min(y + 1, x));
    check(min(x, min(y, x) + (-1)), min(x, y) + (-1));
    check(min(x, min(y, x) - 1), min(x, y) + (-1));

    check(max(x, max(x, y) - 1), max(y + (-1), x));
    check(max(x, max(x, y) + (-1)), max(y + (-1), x));
    check(max(x, max(x, y) + 1), max(x, y) + 1);
    check(max(x, max(x, y) - (-1)), max(x, y) + 1);

    check(max(x, max(y, x) - 1), max(y + (-1), x));
    check(max(x, max(y, x) + (-1)), max(y + (-1), x));
    check(max(x, max(y, x) + 1), max(x, y) + 1);
    check(max(x, max(y, x) - (-1)), max(x, y) + 1);

    check(max(Expr(7), 3), 7);
    check(max(Expr(4.25f), 1.25f), 4.25f);
    check(max(broadcast(x, 4), broadcast(y, 4)),
          broadcast(max(x, y), 4));
    check(max(x, x + 3), x + 3);
    check(max(x + 4, x), x + 4);
    check(max(x - 1, x + 2), x + 2);
    check(max(7, max(x, 3)), max(x, 7));
    check(max(max(x, y), x), max(x, y));
    check(max(max(x, y), y), max(x, y));
    check(max(x, max(x, y)), max(x, y));
    check(max(y, max(x, y)), max(x, y));

    // Check that simplifier can recognise instances where the extremes of the
    // datatype appear as constants in comparisons, Min and Max expressions.
    // The result of min/max with extreme is known to be either the extreme or
    // the other expression.  The result of < or > comparison is known to be true or false.
    check(x <= Int(32).max(), const_true());
    check(cast(Int(16), x) >= Int(16).min(), const_true());
    check(x < Int(32).min(), const_false());
    check(min(cast(UInt(16), x), cast(UInt(16), 65535)), cast(UInt(16), x));
    check(min(x, Int(32).max()), x);
    check(min(Int(32).min(), x), Int(32).min());
    check(max(cast(Int(8), x), cast(Int(8), -128)), cast(Int(8), x));
    check(max(x, Int(32).min()), x);
    check(max(x, Int(32).max()), Int(32).max());
    // Check that non-extremes do not lead to incorrect simplification
    check(max(cast(Int(8), x), cast(Int(8), -127)), max(cast(Int(8), x), make_const(Int(8), -127)));

    // Some quaternary rules with cancellations
    check((x + y) - (y + z), x - z);
    check((x + y) - (y + z), x - z);
    check((y + x) - (y + z), x - z);
    check((y + x) - (y + z), x - z);

    check((x - y) - (z - y), x - z);
    check((y - z) - (y - x), x - z);

    check((x + 3) / 4 - (x + 2) / 4, ((x + 2) % 4 + 1) / 4);

    check(min(x + y, y + z), min(x, z) + y);
    check(min(y + x, y + z), min(x, z) + y);
    check(min(x + y, y + z), min(x, z) + y);
    check(min(y + x, y + z), min(x, z) + y);

    check(min(x, y) - min(y, x), 0);
    check(max(x, y) - max(y, x), 0);

    check(min(123 - x, 1 - x), 1 - x);
    check(max(123 - x, 1 - x), 123 - x);

    check(min(x * 43, y * 43), min(x, y) * 43);
    check(max(x * 43, y * 43), max(x, y) * 43);
    check(min(x * -43, y * -43), max(x, y) * -43);
    check(max(x * -43, y * -43), min(x, y) * -43);

    check(min(min(x, 4), y), min(min(x, y), 4));
    check(max(max(x, 4), y), max(max(x, y), 4));

    check(min(x * 8, 24), min(x, 3) * 8);
    check(max(x * 8, 24), max(x, 3) * 8);
    check(min(x * -8, 24), max(x, -3) * -8);
    check(max(x * -8, 24), min(x, -3) * -8);

    check(min(clamp(x, -10, 14), clamp(y, -10, 14)), clamp(min(x, y), -10, 14));

    check(min(x / 4, y / 4), min(x, y) / 4);
    check(max(x / 4, y / 4), max(x, y) / 4);

    check(min(x / (-4), y / (-4)), max(x, y) / (-4));
    check(max(x / (-4), y / (-4)), min(x, y) / (-4));

    check(min(x / 4 + 2, y / 4), min(x + 8, y) / 4);
    check(max(x / 4 + 2, y / 4), max(x + 8, y) / 4);
    check(min(x / 4, y / 4 + 2), min(y + 8, x) / 4);
    check(max(x / 4, y / 4 + 2), max(y + 8, x) / 4);
    check(min(x / (-4) + 2, y / (-4)), max(x + -8, y) / (-4));
    check(max(x / (-4) + 2, y / (-4)), min(x + -8, y) / (-4));
    check(min(x / (-4), y / (-4) + 2), max(y + -8, x) / (-4));
    check(max(x / (-4), y / (-4) + 2), min(y + -8, x) / (-4));

    check(min(x * 4 + 8, y * 4), min(x + 2, y) * 4);
    check(max(x * 4 + 8, y * 4), max(x + 2, y) * 4);
    check(min(x * 4, y * 4 + 8), min(y + 2, x) * 4);
    check(max(x * 4, y * 4 + 8), max(y + 2, x) * 4);
    check(min(x * (-4) + 8, y * (-4)), max(x + -2, y) * (-4));
    check(max(x * (-4) + 8, y * (-4)), min(x + -2, y) * (-4));
    check(min(x * (-4), y * (-4) + 8), max(y + -2, x) * (-4));
    check(max(x * (-4), y * (-4) + 8), min(y + -2, x) * (-4));

    // Min and max of clamped expressions
    check(min(clamp(x + 1, y, z), clamp(x - 1, y, z)), clamp(x + (-1), y, z));
    check(max(clamp(x + 1, y, z), clamp(x - 1, y, z)), clamp(x + 1, y, z));

    // Additions that cancel a term inside a min or max
    check(x + min(y - x, z), min(x + z, y));
    check(x + max(y - x, z), max(x + z, y));
    check(min(y + (-2), z) + 2, min(z + 2, y));
    check(max(y + (-2), z) + 2, max(z + 2, y));

    // Min/Max distributive law
    check(max(max(x, y), max(x, z)), max(max(y, z), x));
    check(min(max(x, y), max(x, z)), max(min(y, z), x));
    check(min(min(x, y), min(x, z)), min(min(y, z), x));
    check(max(min(x, y), min(x, z)), min(max(y, z), x));

    // Mins of expressions and rounded up versions of them
    check(min(((x + 7) / 8) * 8, x), x);
    check(min(x, ((x + 7) / 8) * 8), x);
    check(max(((x + 7) / 8) * 8, x), ((x + 7) / 8) * 8);
    check(max(x, ((x + 7) / 8) * 8), ((x + 7) / 8) * 8);

    // And rounded down...
    check(max((x / 8) * 8, x), x);
    check(max(x, (x / 8) * 8), x);
    check(min((x / 8) * 8, x), (x / 8) * 8);
    check(min(x, (x / 8) * 8), (x / 8) * 8);

    // "likely" marks which side of a containing min/max/select is the
    // one to optimize for, so if the min/max/select gets simplified
    // away, the likely should be stripped too.
    check(min(x, likely(x)), x);
    check(min(likely(x), x), x);
    check(max(x, likely(x)), x);
    check(max(likely(x), x), x);
    check(select(x > y, likely(x), x), x);
    check(select(x > y, x, likely(x)), x);
    // Check constant-bounds reasoning works through likelies
    check(min(4, likely(5)), 4);
    check(min(7, likely(5)), 5);
    check(max(4, likely(5)), 5);
    check(max(7, likely(5)), 7);

    check(select(x < y, x + y, x), select(x < y, y, 0) + x);
    check(select(x < y, x, x + y), select(x < y, 0, y) + x);

    check(min(x + 1, y) - min(x, y - 1), 1);
    check(max(x + 1, y) - max(x, y - 1), 1);
    check(min(x + 1, y) - min(y - 1, x), 1);
    check(max(x + 1, y) - max(y - 1, x), 1);

    // min and max on constant ramp v broadcast
    check(max(ramp(0, 1, 8), 0), ramp(0, 1, 8));
    check(min(ramp(0, 1, 8), 7), ramp(0, 1, 8));
    check(max(ramp(0, 1, 8), 7), broadcast(7, 8));
    check(min(ramp(0, 1, 8), 0), broadcast(0, 8));
    check(min(ramp(0, 1, 8), 4), min(ramp(0, 1, 8), 4));

    check(max(ramp(7, -1, 8), 0), ramp(7, -1, 8));
    check(min(ramp(7, -1, 8), 7), ramp(7, -1, 8));
    check(max(ramp(7, -1, 8), 7), broadcast(7, 8));
    check(min(ramp(7, -1, 8), 0), broadcast(0, 8));
    check(min(ramp(7, -1, 8), 4), min(ramp(7, -1, 8), 4));

    check(max(0, ramp(0, 1, 8)), ramp(0, 1, 8));
    check(min(7, ramp(0, 1, 8)), ramp(0, 1, 8));

    check(min(8 - x, 2), 8 - max(x, 6));
    check(max(3, 77 - x), 77 - min(x, 74));
    check(min(max(8 - x, 0), 8), 8 - max(min(x, 8), 0));

    check(x - min(x, 2), max(x, 2) + -2);
    check(x - max(x, 2), min(x, 2) + -2);
    check(min(x, 2) - x, 2 - max(x, 2));
    check(max(x, 2) - x, 2 - min(x, 2));
    check(x - min(2, x), max(x, 2) + -2);
    check(x - max(2, x), min(x, 2) + -2);
    check(min(2, x) - x, 2 - max(x, 2));
    check(max(2, x) - x, 2 - min(x, 2));

    check(max(min(x, y), x), x);
    check(max(min(x, y), y), y);
    check(min(max(x, y), x), x);
    check(min(max(x, y), y), y);
    check(max(min(x, y), x) + y, x + y);

    check(max(min(max(x, y), z), y), max(min(x, z), y));
    check(max(min(z, max(x, y)), y), max(min(x, z), y));
    check(max(y, min(max(x, y), z)), max(min(x, z), y));
    check(max(y, min(z, max(x, y))), max(min(x, z), y));

    check(max(min(max(y, x), z), y), max(min(x, z), y));
    check(max(min(z, max(y, x)), y), max(min(x, z), y));
    check(max(y, min(max(y, x), z)), max(min(x, z), y));
    check(max(y, min(z, max(y, x))), max(min(x, z), y));

    check(min(max(min(x, y), z), y), min(max(x, z), y));
    check(min(max(z, min(x, y)), y), min(max(x, z), y));
    check(min(y, max(min(x, y), z)), min(max(x, z), y));
    check(min(y, max(z, min(x, y))), min(max(x, z), y));

    check(min(max(min(y, x), z), y), min(max(x, z), y));
    check(min(max(z, min(y, x)), y), min(max(x, z), y));
    check(min(y, max(min(y, x), z)), min(max(x, z), y));
    check(min(y, max(z, min(y, x))), min(max(x, z), y));

    check(max(min(x, 5), 1) == 1, x <= 1);
    check(max(min(x, 5), 1) == 3, x == 3);
    check(max(min(x, 5), 1) == 5, 5 <= x);

    check(min((x * 32 + y) * 4, x * 128 + 127), min(y * 4, 127) + x * 128);
    check(min((x * 32 + y) * 4, x * 128 + 4), (min(y, 1) + x * 32) * 4);
    check(min((y + x * 32) * 4, x * 128 + 127), min(y * 4, 127) + x * 128);
    check(min((y + x * 32) * 4, x * 128 + 4), (min(y, 1) + x * 32) * 4);
    check(max((x * 32 + y) * 4, x * 128 + 127), max(y * 4, 127) + x * 128);
    check(max((x * 32 + y) * 4, x * 128 + 4), (max(y, 1) + x * 32) * 4);
    check(max((y + x * 32) * 4, x * 128 + 127), max(y * 4, 127) + x * 128);
    check(max((y + x * 32) * 4, x * 128 + 4), (max(y, 1) + x * 32) * 4);

    check((min(x + y, z) + w) - x, min(z - x, y) + w);
    check(min((x + y) + w, z) - x, min(z - x, w + y));

    check(min(min(x + z, y), w) - x, min(min(w, y) - x, z));
    check(min(min(y, x + z), w) - x, min(min(w, y) - x, z));

    // Two- and three-deep cancellations into min/max nodes
    check((x - min(z, (x + y))), (0 - min(z - x, y)));
    check((x - min(z, (y + x))), (0 - min(z - x, y)));
    check((x - min((x + y), z)), (0 - min(z - x, y)));
    check((x - min((y + x), z)), (0 - min(z - x, y)));
    check((x - min(y, (w + (x + z)))), (0 - min((y - x), (w + z))));
    check((x - min(y, (w + (z + x)))), (0 - min((y - x), (w + z))));
    check((x - min(y, ((x + z) + w))), (0 - min((y - x), (w + z))));
    check((x - min(y, ((z + x) + w))), (0 - min((y - x), (w + z))));
    check((x - min((w + (x + z)), y)), (0 - min((y - x), (w + z))));
    check((x - min((w + (z + x)), y)), (0 - min((y - x), (w + z))));
    check((x - min(((x + z) + w), y)), (0 - min((y - x), (w + z))));
    check((x - min(((z + x) + w), y)), (0 - min((y - x), (w + z))));

    check(min(x + y, z) - x, min(z - x, y));
    check(min(y + x, z) - x, min(z - x, y));
    check(min(z, x + y) - x, min(z - x, y));
    check(min(z, y + x) - x, min(z - x, y));
    check((min(x, (w + (y + z))) - z), min(x - z, w + y));
    check((min(x, (w + (z + y))) - z), min(x - z, w + y));
    check((min(x, ((y + z) + w)) - z), min(x - z, w + y));
    check((min(x, ((z + y) + w)) - z), min(x - z, w + y));
    check((min((w + (y + z)), x) - z), min(x - z, w + y));
    check((min((w + (z + y)), x) - z), min(x - z, w + y));
    check((min(((y + z) + w), x) - z), min(x - z, w + y));
    check((min(((z + y) + w), x) - z), min(x - z, w + y));

    check((x - max(z, (x + y))), (0 - max(z - x, y)));
    check((x - max(z, (y + x))), (0 - max(z - x, y)));
    check((x - max((x + y), z)), (0 - max(z - x, y)));
    check((x - max((y + x), z)), (0 - max(z - x, y)));
    check((x - max(y, (w + (x + z)))), (0 - max((y - x), (w + z))));
    check((x - max(y, (w + (z + x)))), (0 - max((y - x), (w + z))));
    check((x - max(y, ((x + z) + w))), (0 - max((y - x), (w + z))));
    check((x - max(y, ((z + x) + w))), (0 - max((y - x), (w + z))));
    check((x - max((w + (x + z)), y)), (0 - max((y - x), (w + z))));
    check((x - max((w + (z + x)), y)), (0 - max((y - x), (w + z))));
    check((x - max(((x + z) + w), y)), (0 - max((y - x), (w + z))));
    check((x - max(((z + x) + w), y)), (0 - max((y - x), (w + z))));

    check(max(x + y, z) - x, max(z - x, y));
    check(max(y + x, z) - x, max(z - x, y));
    check(max(z, x + y) - x, max(z - x, y));
    check(max(z, y + x) - x, max(z - x, y));
    check((max(x, (w + (y + z))) - z), max(x - z, w + y));
    check((max(x, (w + (z + y))) - z), max(x - z, w + y));
    check((max(x, ((y + z) + w)) - z), max(x - z, w + y));
    check((max(x, ((z + y) + w)) - z), max(x - z, w + y));
    check((max((w + (y + z)), x) - z), max(x - z, w + y));
    check((max((w + (z + y)), x) - z), max(x - z, w + y));
    check((max(((y + z) + w), x) - z), max(x - z, w + y));
    check((max(((z + y) + w), x) - z), max(x - z, w + y));

    check(min((x + y) * 7 + z, w) - x * 7, min(w - x * 7, y * 7 + z));
    check(min((y + x) * 7 + z, w) - x * 7, min(w - x * 7, y * 7 + z));

    check(min(x * 12 + y, z) / 4 - x * 3, min(z - x * 12, y) / 4);
    check(min(z, x * 12 + y) / 4 - x * 3, min(z - x * 12, y) / 4);

    check((min(x * 12 + y, z) + w) / 4 - x * 3, (min(z - x * 12, y) + w) / 4);
    check((min(z, x * 12 + y) + w) / 4 - x * 3, (min(z - x * 12, y) + w) / 4);

    check(min((min(((y + 5) / 2), x) * 2), y + 3), min(x * 2, y + 3));
    check(min((min(((y + 1) / 3), x) * 3) + 1, y), min(x * 3 + 1, y));

    {
        Expr one = 1;
        Expr three = 3;
        Expr four = 4;
        Expr five = 5;
        Expr v1 = Variable::make(Int(32), "x");
        Expr v2 = Variable::make(Int(32), "y");

        // Bound: [-4, 4]
        Expr clamped = min(max(v1, -four), four);

        // min(v, 4) where v=[-4, 4] -> v
        check(min(clamped, four), simplify(clamped));
        // min(v, 5) where v=[-4, 4] -> v
        check(min(clamped, five), simplify(clamped));
        // min(v, 3) where v=[-4, 4] -> min(v, 3)
        check(min(clamped, three), simplify(min(clamped, three)));
        // min(v, -5) where v=[-4, 4] -> -5
        check(min(clamped, -five), simplify(-five));

        // max(v, 4) where v=[-4, 4] -> 4
        check(max(clamped, four), simplify(four));
        // max(v, 5) where v=[-4, 4] -> 5
        check(max(clamped, five), simplify(five));
        // max(v, 3) where v=[-4, 4] -> max(v, 3)
        check(max(clamped, three), simplify(max(clamped, three)));
        // max(v, -5) where v=[-4, 4] -> v
        check(max(clamped, -five), simplify(clamped));

        // max(min(v, 5), -5) where v=[-4, 4] -> v
        check(max(min(clamped, five), -five), simplify(clamped));
        // max(min(v, 5), 5) where v=[-4, 4] -> 5
        check(max(min(clamped, five), five), simplify(five));

        // max(min(v, -5), -5) where v=[-4, 4] -> -5
        check(max(min(clamped, -five), -five), simplify(-five));
        // max(min(v, -5), 5) where v=[-4, 4] -> 5
        check(max(min(clamped, -five), five), simplify(five));

        // min(v + 1, 4) where v=[-4, 4] -> min(v + 1, 4)
        check(min(clamped + one, four), simplify(min(clamped + one, four)));
        // min(v + 1, 5) where v=[-4, 4] -> v + 1
        check(min(clamped + one, five), simplify(clamped + one));
        // min(v + 1, -4) where v=[-4, 4] -> -4
        check(min(clamped + one, -four), simplify(-four));
        // max(min(v + 1, 4), -4) where v=[-4, 4] -> min(v + 1, 4)
        check(max(min(clamped + one, four), -four), simplify(min(clamped + one, four)));

        // max(v + 1, 4) where v=[-4, 4] -> max(v + 1, 4)
        check(max(clamped + one, four), simplify(max(clamped + one, four)));
        // max(v + 1, 5) where v=[-4, 4] -> 5
        check(max(clamped + one, five), simplify(five));
        // max(v + 1, -4) where v=[-4, 4] -> -v + 1
        check(max(clamped + one, -four), simplify(clamped + one));
        // min(max(v + 1, -4), 4) where v=[-4, 4] -> min(v + 1, 4)
        check(min(max(clamped + one, -four), four), simplify(min(clamped + one, four)));

        Expr t1 = clamp(v1, one, four);
        Expr t2 = clamp(v1, -five, -four);
        check(min(max(min(v2, t1), t2), five), simplify(max(min(t1, v2), t2)));
    }

    {
        Expr xv = Variable::make(Int(16).with_lanes(64), "x");
        Expr yv = Variable::make(Int(16).with_lanes(64), "y");
        Expr zv = Variable::make(Int(16).with_lanes(64), "z");

        // min(min(x, broadcast(y, n)), broadcast(z, n))) -> min(x, broadcast(min(y, z), n))
        check(min(min(xv, broadcast(y, 64)), broadcast(z, 64)), min(xv, broadcast(min(y, z), 64)));
        // min(min(broadcast(x, n), y), broadcast(z, n))) -> min(y, broadcast(min(x, z), n))
        check(min(min(broadcast(x, 64), yv), broadcast(z, 64)), min(yv, broadcast(min(x, z), 64)));
        // min(broadcast(x, n), min(y, broadcast(z, n)))) -> min(y, broadcast(min(x, z), n))
        check(min(broadcast(x, 64), min(yv, broadcast(z, 64))), min(yv, broadcast(min(x, z), 64)));
        // min(broadcast(x, n), min(broadcast(y, n), z))) -> min(z, broadcast(min(x, y), n))
        check(min(broadcast(x, 64), min(broadcast(y, 64), zv)), min(zv, broadcast(min(x, y), 64)));

        // max(max(x, broadcast(y, n)), broadcast(z, n))) -> max(x, broadcast(max(y, z), n))
        check(max(max(xv, broadcast(y, 64)), broadcast(z, 64)), max(xv, broadcast(max(y, z), 64)));
        // max(max(broadcast(x, n), y), broadcast(z, n))) -> max(y, broadcast(max(x, z), n))
        check(max(max(broadcast(x, 64), yv), broadcast(z, 64)), max(yv, broadcast(max(x, z), 64)));
        // max(broadcast(x, n), max(y, broadcast(z, n)))) -> max(y, broadcast(max(x, z), n))
        check(max(broadcast(x, 64), max(yv, broadcast(z, 64))), max(yv, broadcast(max(x, z), 64)));
        // max(broadcast(x, n), max(broadcast(y, n), z))) -> max(z, broadcast(max(x, y), n))
        check(max(broadcast(x, 64), max(broadcast(y, 64), zv)), max(zv, broadcast(max(x, y), 64)));
    }

    // Pull out common addition term inside min/max
    check(min((x + y) + z, x + w), min(y + z, w) + x);
    check(min((y + x) + z, x + w), min(y + z, w) + x);
    check(min(x + y, (x + z) + w), min(w + z, y) + x);
    check(min(x + y, (z + x) + w), min(w + z, y) + x);
    check(min(x + (y + z), y + w), min(x + z, w) + y);
    check(min(x + (z + y), y + w), min(x + z, w) + y);
    check(min(x + y, z + (x + w)), min(w + z, y) + x);
    check(min(x + y, z + (w + x)), min(w + z, y) + x);
    check(min(x + y / 2 + 13, x + (0 - y) / 2), min(0 - y, y + 26) / 2 + x);

    check(max((x + y) + z, x + w), max(y + z, w) + x);
    check(max((y + x) + z, x + w), max(y + z, w) + x);
    check(max(x + y, (x + z) + w), max(w + z, y) + x);
    check(max(x + y, (z + x) + w), max(w + z, y) + x);
    check(max(x + (y + z), y + w), max(x + z, w) + y);
    check(max(x + (z + y), y + w), max(x + z, w) + y);
    check(max(x + y, z + (x + w)), max(w + z, y) + x);
    check(max(x + y, z + (w + x)), max(w + z, y) + x);

    // Check min(x, y)*max(x, y) gets simplified into x*y
    check(min(x, y) * max(x, y), x * y);
    check(min(x, y) * max(y, x), x * y);
    check(max(x, y) * min(x, y), x * y);
    check(max(y, x) * min(x, y), x * y);

    // Check min(x, y) + max(x, y) gets simplified into x + y
    check(min(x, y) + max(x, y), x + y);
    check(min(x, y) + max(y, x), x + y);
    check(max(x, y) + min(x, y), x + y);
    check(max(y, x) + min(x, y), x + y);

    // Check max(min(x, y), max(x, y)) gets simplified into max(x, y)
    check(max(min(x, y), max(x, y)), max(x, y));
    check(max(min(x, y), max(y, x)), max(x, y));
    check(max(max(x, y), min(x, y)), max(x, y));
    check(max(max(y, x), min(x, y)), max(x, y));

    // Check min(max(x, y), min(x, y)) gets simplified into min(x, y)
    check(min(max(x, y), min(x, y)), min(x, y));
    check(min(max(x, y), min(y, x)), min(x, y));
    check(min(min(x, y), max(x, y)), min(x, y));
    check(min(min(x, y), max(y, x)), min(x, y));

    // Check if we can simplify away comparison on vector types considering bounds.
    Scope<Interval> bounds_info;
    bounds_info.push("x", Interval(0, 4));
    check_in_bounds(ramp(x, 1, 4) < broadcast(0, 4), const_false(4), bounds_info);
    check_in_bounds(ramp(x, 1, 4) < broadcast(8, 4), const_true(4), bounds_info);
    check_in_bounds(ramp(x, -1, 4) < broadcast(-4, 4), const_false(4), bounds_info);
    check_in_bounds(ramp(x, -1, 4) < broadcast(5, 4), const_true(4), bounds_info);
    check_in_bounds(min(ramp(x, 1, 4), broadcast(0, 4)), broadcast(0, 4), bounds_info);
    check_in_bounds(min(ramp(x, 1, 4), broadcast(8, 4)), ramp(x, 1, 4), bounds_info);
    check_in_bounds(min(ramp(x, -1, 4), broadcast(-4, 4)), broadcast(-4, 4), bounds_info);
    check_in_bounds(min(ramp(x, -1, 4), broadcast(5, 4)), ramp(x, -1, 4), bounds_info);
    check_in_bounds(max(ramp(x, 1, 4), broadcast(0, 4)), ramp(x, 1, 4), bounds_info);
    check_in_bounds(max(ramp(x, 1, 4), broadcast(8, 4)), broadcast(8, 4), bounds_info);
    check_in_bounds(max(ramp(x, -1, 4), broadcast(-4, 4)), ramp(x, -1, 4), bounds_info);
    check_in_bounds(max(ramp(x, -1, 4), broadcast(5, 4)), broadcast(5, 4), bounds_info);

    check(min(x, 63) - min(x, 3), clamp(x, 3, 63) + (-3));
    check(min(x, 3) - min(x, 63), 3 - clamp(x, 3, 63));
    check(min(63, x) - min(x, 3), clamp(x, 3, 63) + (-3));
    check(min(x, 3) - min(63, x), 3 - clamp(x, 3, 63));

    // This used to throw the simplifier into a loop
    simplify((min((min(((x * 64) + y), (z + -63)) + 31), min((((x * 64) + y) + 63), z)) -
              min((min((((x * 64) + y) + 63), z) + -31), (min(((x * 64) + y), (z + -63)) + 32))));

    check(min(x * 4 + 63, y) - min(x * 4, y - 3), clamp(y - x * 4, 3, 63));
    check(min(y, x * 4 + 63) - min(x * 4, y - 3), clamp(y - x * 4, 3, 63));
    check(min(x * 4, y - 3) - min(x * 4 + 63, y), clamp(x * 4 - y, -63, -3));
    check(min(x * 4, y - 3) - min(y, x * 4 + 63), clamp(x * 4 - y, -63, -3));

    check(max(x, 63) - max(x, 3), 63 - clamp(x, 3, 63));
    check(max(63, x) - max(3, x), 63 - clamp(x, 3, 63));
    check(max(x, 3) - max(x, 63), clamp(x, 3, 63) + -63);
    check(max(3, x) - max(x, 63), clamp(x, 3, 63) + -63);

    check(max(x * 4 + 63, y) - max(x * 4, y - 3), clamp(x * 4 - y, -63, -3) + 66);
    check(max(x * 4 + 63, y) - max(y - 3, x * 4), clamp(x * 4 - y, -63, -3) + 66);
    check(max(x * 4, y - 3) - max(x * 4 + 63, y), clamp(y - x * 4, 3, 63) + -66);
    check(max(y - 3, x * 4) - max(x * 4 + 63, y), clamp(y - x * 4, 3, 63) + -66);
}

void check_boolean() {
    Expr x = Var("x"), y = Var("y"), z = Var("z"), w = Var("w");
    Expr xf = cast<float>(x);
    Expr yf = cast<float>(y);
    Expr t = const_true(), f = const_false();
    Expr b1 = Variable::make(Bool(), "b1");
    Expr b2 = Variable::make(Bool(), "b2");

    check(x == x, t);
    check(x == (x + 1), f);
    check(x - 2 == y + 3, x == y + 5);
    check(x + y == y + z, x == z);
    check(y + x == y + z, x == z);
    check(x + y == z + y, x == z);
    check(y + x == z + y, x == z);
    check((y + x) * 17 == (z + y) * 17, x == z);
    check(x * 0 == y * 0, t);
    check(x == x + y, y == 0);
    check(x + y == x, y == 0);
    check(100 - x == 99 - y, y == x + (-1));

    check(x < x, f);
    check(x < (x + 1), t);
    check(x - 2 < y + 3, x < y + 5);
    check(x + y < y + z, x < z);
    check(y + x < y + z, x < z);
    check(x + y < z + y, x < z);
    check(y + x < z + y, x < z);
    check((y + x) * 17 < (z + y) * 17, x < z);
    check(x * 0 < y * 0, f);
    check(x < x + y, 0 < y);
    check(x + y < x, y < 0);
    check(1 < -x, x < -1);

    check(select(x < 3, 2, 2), 2);
    check(select(x < (x + 1), 9, 2), 9);
    check(select(x > (x + 1), 9, 2), 2);
    // Selects of comparisons should always become selects of LT or selects of EQ
    check(select(x != 5, 2, 3), select(x == 5, 3, 2));
    check(select(x >= 5, 2, 3), select(x < 5, 3, 2));
    check(select(x <= 5, 2, 3), select(5 < x, 3, 2));
    check(select(x > 5, 2, 3), select(5 < x, 2, 3));

    check(select(x > 5, 2, 3) + select(x > 5, 6, 2), select(5 < x, 8, 5));
    check(select(x > 5, 8, 3) - select(x > 5, 6, 2), select(5 < x, 2, 1));

    check(select(x < 5, select(x < 5, 0, 1), 2), select(x < 5, 0, 2));
    check(select(x < 5, 0, select(x < 5, 1, 2)), select(x < 5, 0, 2));

    check(max(select((x == -1), 1, x), 6), max(x, 6));
    check(max(select((x == -1), 1, x), x), select((x == -1), 1, x));
    check(max(select((x == 17), 1, x), x), x);

    check(min(select((x == 1), -1, x), -6), min(x, -6));
    check(min(select((x == 1), -1, x), x), select((x == 1), -1, x));
    check(min(select((x == -17), -1, x), x), x);

    check(min(select(x == 0, max(y, w), z), w), select(x == 0, w, min(w, z)));
    check(max(select(x == 0, y, min(z, w)), w), select(x == 0, max(w, y), w));

    check((1 - xf) * 6 < 3, 0.5f < xf);

    check(!f, t);
    check(!t, f);
    check(!(x < y), y <= x);
    check(!(x > y), x <= y);
    check(!(x >= y), x < y);
    check(!(x <= y), y < x);
    check(!(x == y), x != y);
    check(!(x != y), x == y);
    check(!(!(x == 0)), x == 0);
    check(!Expr(broadcast(x > y, 4)),
          broadcast(x <= y, 4));
    check(x % 2 < 1, x % 2 == 0);
    check(x % 3 <= 0, x % 3 == 0);
    check(x % 4 > 0, x % 4 != 0);
    check(x % 5 >= 1, x % 5 != 0);
    check(x % 6 < 5, x % 6 != 5);
    check(5 < x % 7, x % 7 == 6);

    check(b1 || !b1, t);
    check(!b1 || b1, t);
    check(b1 && !b1, f);
    check(!b1 && b1, f);
    check(b1 && b1, b1);
    check(b1 || b1, b1);
    check(broadcast(b1, 4) || broadcast(!b1, 4), broadcast(t, 4));
    check(broadcast(!b1, 4) || broadcast(b1, 4), broadcast(t, 4));
    check(broadcast(b1, 4) && broadcast(!b1, 4), broadcast(f, 4));
    check(broadcast(!b1, 4) && broadcast(b1, 4), broadcast(f, 4));
    check(broadcast(b1, 4) && broadcast(b1, 4), broadcast(b1, 4));
    check(broadcast(b1, 4) || broadcast(b1, 4), broadcast(b1, 4));

    check((x == 1) && (x != 2), (x == 1));
    check((x != 1) && (x == 2), (x == 2));
    check((x == 1) && (x != 1), f);
    check((x != 1) && (x == 1), f);

    check((x == 1) || (x != 2), (x != 2));
    check((x != 1) || (x == 2), (x != 1));
    check((x == 1) || (x != 1), t);
    check((x != 1) || (x == 1), t);

    check(x < 20 || x > 19, t);
    check(x > 19 || x < 20, t);
    check(x < 20 || x > 20, x < 20 || 20 < x);
    check(x > 20 || x < 20, 20 < x || x < 20);
    check(x < 20 && x > 19, f);
    check(x > 19 && x < 20, f);
    check(x < 20 && x > 18, x < 20 && 18 < x);
    check(x > 18 && x < 20, 18 < x && x < 20);

    check(x < y + 1 && x < y + 2 && x < y, x < y);
    check(x < y + 1 && x < y - 2 && x < y, x < y + (-2));
    check(x < y + 1 && x < y + z && x < y, x < min(z, 0) + y);

    check(x < y + 1 || x < y + 2 || x < y, x < y + 2);
    check(x < y + 1 || x < y - 2 || x < y, x < y + 1);
    check(x < y + 1 || x < y + z || x < y, x < max(z, 1) + y);

    check(x <= 20 || x > 19, t);
    check(x > 19 || x <= 20, t);
    check(x <= 18 || x > 20, x <= 18 || 20 < x);
    check(x > 20 || x <= 18, x <= 18 || 20 < x);
    check(x <= 18 && x > 19, f);
    check(x > 19 && x <= 18, f);
    check(x <= 20 && x > 19, x <= 20 && 19 < x);
    check(x > 19 && x <= 20, x <= 20 && 19 < x);

    check(x < 20 || x >= 19, t);
    check(x >= 19 || x < 20, t);
    check(x < 18 || x >= 20, 20 <= x || x < 18);
    check(x >= 20 || x < 18, 20 <= x || x < 18);
    check(x < 18 && x >= 19, f);
    check(x >= 19 && x < 18, f);
    check(x < 20 && x >= 19, 19 <= x && x < 20);
    check(x >= 19 && x < 20, 19 <= x && x < 20);

    check(x <= 20 || x >= 21, t);
    check(x >= 21 || x <= 20, t);
    check(x <= 18 || x >= 20, x <= 18 || 20 <= x);
    check(x >= 20 || x <= 18, 20 <= x || x <= 18);
    check(x <= 18 && x >= 19, f);
    check(x >= 19 && x <= 18, f);
    check(x <= 20 && x >= 20, x <= 20 && 20 <= x);
    check(x >= 20 && x <= 20, 20 <= x && x <= 20);

    check(min(x, 20) < min(x, 19), const_false());
    check(min(x, 23) < min(x, 18) - 3, const_false());

    check(max(x, 19) > max(x, 20), const_false());
    check(max(x, 18) > max(x, 23) + 3, const_false());

    // check for substitution patterns
    check((b1 == t) && (b1 && b2), b1 && b2);
    check((b1 && b2) && (b1 == t), b1 && b2);

    check(t && (x < 0), x < 0);
    check(f && (x < 0), f);
    check(t || (x < 0), t);
    check(f || (x < 0), x < 0);

    check(x == y || y != x, t);
    check(x == y || x != y, t);
    check(x == y && x != y, f);
    check(x == y && y != x, f);
    check(x < y || x >= y, t);
    check(x <= y || x > y, t);
    check(x < y && x >= y, f);
    check(x <= y && x > y, f);

    check(x <= max(x, y), t);
    check(x < min(x, y), f);
    check(min(x, y) <= x, t);
    check(max(x, y) < x, f);
    check(max(x, y) <= y, x <= y);
    check(min(x, y) >= y, y <= x);

    check(max(x, y) < min(y, z), f);
    check(max(x, y) < min(z, y), f);
    check(max(y, x) < min(y, z), f);
    check(max(y, x) < min(z, y), f);

    check(max(x, y) >= min(y, z), t);
    check(max(x, y) >= min(z, y), t);
    check(max(y, x) >= min(y, z), t);
    check(max(y, x) >= min(z, y), t);

    check(min(z, y) < min(x, y), z < min(x, y));
    check(min(z, y) < min(y, x), z < min(x, y));
    check(min(y, z) < min(x, y), z < min(x, y));
    check(min(y, z) < min(y, x), z < min(x, y));
    check(min(z, y) < min(x, y + 5), min(y, z) < x);
    check(min(z, y) < min(y + 5, x), min(y, z) < x);
    check(min(z, y - 5) < min(x, y), min(y + (-5), z) < x);
    check(min(z, y - 5) < min(y, x), min(y + (-5), z) < x);

    check(max(z, y) < max(x, y), max(y, z) < x);
    check(max(z, y) < max(y, x), max(y, z) < x);
    check(max(y, z) < max(x, y), max(y, z) < x);
    check(max(y, z) < max(y, x), max(y, z) < x);
    check(max(z, y) < max(x, y - 5), max(y, z) < x);
    check(max(z, y) < max(y - 5, x), max(y, z) < x);
    check(max(z, y + 5) < max(x, y), max(y + 5, z) < x);
    check(max(z, y + 5) < max(y, x), max(y + 5, z) < x);

    check((1 < y) && (2 < y), 2 < y);

    check(x * 5 < 4, x < 1);
    check(x * 5 < 5, x < 1);
    check(x * 5 < 6, x < 2);
    check(x * 5 <= 4, x <= 0);
    check(x * 5 <= 5, x <= 1);
    check(x * 5 <= 6, x <= 1);
    check(x * 5 > 4, 0 < x);
    check(x * 5 > 5, 1 < x);
    check(x * 5 > 6, 1 < x);
    check(x * 5 >= 4, 1 <= x);
    check(x * 5 >= 5, 1 <= x);
    check(x * 5 >= 6, 2 <= x);

    check(x / 4 < 3, x < 12);
    check(3 < x / 4, 15 < x);

    check(4 - x <= 0, 4 <= x);

    check((x / 8) * 8 < x - 8, f);
    check((x / 8) * 8 < x - 9, f);
    check((x / 8) * 8 < x - 7, f);
    check((x / 8) * 8 < x - 6, x % 8 == 7);
    check(ramp(x * 4, 1, 4) < broadcast(y * 4, 4), broadcast(x < y, 4));
    check(ramp(x * 8, 1, 4) < broadcast(y * 8, 4), broadcast(x < y, 4));
    check(ramp(x * 8 + 1, 1, 4) < broadcast(y * 8, 4), broadcast(x < y, 4));
    check(ramp(x * 8 + 4, 1, 4) < broadcast(y * 8, 4), broadcast(x < y, 4));
    check(ramp(x * 8 + 8, 1, 4) < broadcast(y * 8, 4), broadcast(x < y + (-1), 4));
    check(ramp(x * 8 + 5, 1, 4) < broadcast(y * 8, 4), ramp(x * 8 + 5, 1, 4) < broadcast(y * 8, 4));
    check(ramp(x * 8 - 1, 1, 4) < broadcast(y * 8, 4), ramp(x * 8 + (-1), 1, 4) < broadcast(y * 8, 4));
    check(ramp(x * 8, 1, 4) < broadcast(y * 4, 4), broadcast(x * 2 < y, 4));
    check(ramp(x * 8, 2, 4) < broadcast(y * 8, 4), broadcast(x < y, 4));
    check(ramp(x * 8 + 1, 2, 4) < broadcast(y * 8, 4), broadcast(x < y, 4));
    check(ramp(x * 8 + 2, 2, 4) < broadcast(y * 8, 4), ramp(x * 8 + 2, 2, 4) < broadcast(y * 8, 4));
    check(ramp(x * 8, 3, 4) < broadcast(y * 8, 4), ramp(x * 8, 3, 4) < broadcast(y * 8, 4));
    check(select(ramp((x / 16) * 16, 1, 8) < broadcast((y / 8) * 8, 8), broadcast(1, 8), broadcast(3, 8)),
          select((x / 16) * 2 < y / 8, broadcast(1, 8), broadcast(3, 8)));

    check(ramp(x * 8, -1, 4) < broadcast(y * 8, 4), ramp(x * 8, -1, 4) < broadcast(y * 8, 4));
    check(ramp(x * 8 + 1, -1, 4) < broadcast(y * 8, 4), ramp(x * 8 + 1, -1, 4) < broadcast(y * 8, 4));
    check(ramp(x * 8 + 4, -1, 4) < broadcast(y * 8, 4), broadcast(x < y, 4));
    check(ramp(x * 8 + 8, -1, 4) < broadcast(y * 8, 4), ramp(x * 8 + 8, -1, 4) < broadcast(y * 8, 4));
    check(ramp(x * 8 + 5, -1, 4) < broadcast(y * 8, 4), broadcast(x < y, 4));
    check(ramp(x * 8 - 1, -1, 4) < broadcast(y * 8, 4), broadcast(x < y + 1, 4));

    // Check anded conditions apply to the then case only
    check(IfThenElse::make(x == 4 && y == 5,
                           not_no_op(z + x + y),
                           not_no_op(z + x - y)),
          IfThenElse::make(x == 4 && y == 5,
                           not_no_op(z + 9),
                           not_no_op(x + z - y)));

    // Check ored conditions apply to the else case only
    check(IfThenElse::make(b1 || b2,
                           not_no_op(select(b1, x + 3, y + 4) + select(b2, x + 5, y + 7)),
                           not_no_op(select(b1, x + 3, y + 8) - select(b2, x + 5, y + 7))),
          IfThenElse::make(b1 || b2,
                           not_no_op(select(b1, x + 3, y + 4) + select(b2, x + 5, y + 7)),
                           not_no_op(1)));

    // Check single conditions apply to both cases of an ifthenelse
    check(IfThenElse::make(b1,
                           not_no_op(select(b1, x, y)),
                           not_no_op(select(b1, z, w))),
          IfThenElse::make(b1,
                           not_no_op(x),
                           not_no_op(w)));

    check(IfThenElse::make(x < y,
                           IfThenElse::make(x < y, not_no_op(y), not_no_op(x)),
                           not_no_op(x)),
          IfThenElse::make(x < y,
                           not_no_op(y),
                           not_no_op(x)));

    check(Block::make(IfThenElse::make(x < y, not_no_op(x + 1), not_no_op(x + 2)),
                      IfThenElse::make(x < y, not_no_op(x + 3), not_no_op(x + 4))),
          IfThenElse::make(x < y,
                           Block::make(not_no_op(x + 1), not_no_op(x + 3)),
                           Block::make(not_no_op(x + 2), not_no_op(x + 4))));

    check(Block::make(IfThenElse::make(x < y, not_no_op(x + 1)),
                      IfThenElse::make(x < y, not_no_op(x + 2))),
          IfThenElse::make(x < y, Block::make(not_no_op(x + 1), not_no_op(x + 2))));

    check(Block::make({IfThenElse::make(x < y, not_no_op(x + 1), not_no_op(x + 2)),
                       IfThenElse::make(x < y, not_no_op(x + 3), not_no_op(x + 4)),
                       not_no_op(x + 5)}),
          Block::make(IfThenElse::make(x < y,
                                       Block::make(not_no_op(x + 1), not_no_op(x + 3)),
                                       Block::make(not_no_op(x + 2), not_no_op(x + 4))),
                      not_no_op(x + 5)));

    check(Block::make({IfThenElse::make(x < y, not_no_op(x + 1)),
                       IfThenElse::make(x < y, not_no_op(x + 2)),
                       IfThenElse::make(x < y, not_no_op(x + 3)),
                       not_no_op(x + 4)}),
          Block::make(IfThenElse::make(x < y, Block::make({not_no_op(x + 1), not_no_op(x + 2), not_no_op(x + 3)})),
                      not_no_op(x + 4)));

    check(Block::make({IfThenElse::make(x < y, not_no_op(x + 1)),
                       IfThenElse::make(x < y, not_no_op(x + 2)),
                       not_no_op(x + 3)}),
          Block::make(IfThenElse::make(x < y, Block::make(not_no_op(x + 1), not_no_op(x + 2))),
                      not_no_op(x + 3)));

    check(Block::make(IfThenElse::make(x < y, not_no_op(x + 1), not_no_op(x + 2)),
                      IfThenElse::make(x < y, not_no_op(x + 3))),
          IfThenElse::make(x < y,
                           Block::make(not_no_op(x + 1), not_no_op(x + 3)),
                           not_no_op(x + 2)));

    check(Block::make(IfThenElse::make(x < y, not_no_op(x + 1)),
                      IfThenElse::make(x < y, not_no_op(x + 2), not_no_op(x + 3))),
          IfThenElse::make(x < y,
                           Block::make(not_no_op(x + 1), not_no_op(x + 2)),
                           not_no_op(x + 3)));

    // The construct
    //     if (var == expr) then a else b;
    // was being simplified incorrectly, but *only* if var was of type Bool.
    Stmt then_clause = AssertStmt::make(b2, Expr(22));
    Stmt else_clause = AssertStmt::make(b2, Expr(33));
    check(IfThenElse::make(b1 == b2, then_clause, else_clause),
          IfThenElse::make(b1 == b2, then_clause, else_clause));

    // Check common statements are pulled out of ifs.
    check(IfThenElse::make(x < y, not_no_op(x + 1), not_no_op(x + 1)),
          not_no_op(x + 1));

    check(IfThenElse::make(x < y,
                           Block::make(not_no_op(x + 1), not_no_op(x + 2)),
                           Block::make(not_no_op(x + 1), not_no_op(x + 3))),
          Block::make(not_no_op(x + 1),
                      IfThenElse::make(x < y, not_no_op(x + 2), not_no_op(x + 3))));

    check(IfThenElse::make(x < y,
                           Block::make(not_no_op(x + 1), not_no_op(x + 2)),
                           Block::make(not_no_op(x + 3), not_no_op(x + 2))),
          Block::make(IfThenElse::make(x < y, not_no_op(x + 1), not_no_op(x + 3)),
                      not_no_op(x + 2)));

    check(IfThenElse::make(x < y,
                           Block::make(not_no_op(x + 1), not_no_op(x + 2)),
                           not_no_op(x + 2)),
          Block::make(IfThenElse::make(x < y, not_no_op(x + 1)),
                      not_no_op(x + 2)));

    check(IfThenElse::make(x < y,
                           Block::make(not_no_op(x + 1), not_no_op(x + 2)),
                           not_no_op(x + 1)),
          Block::make(not_no_op(x + 1),
                      IfThenElse::make(x < y, not_no_op(x + 2))));

    check(IfThenElse::make(x < y,
                           not_no_op(x + 1),
                           Block::make(not_no_op(x + 1), not_no_op(x + 2))),
          Block::make(not_no_op(x + 1),
                      IfThenElse::make(x < y, Evaluate::make(0), not_no_op(x + 2))));

    check(IfThenElse::make(x < y,
                           not_no_op(x + 2),
                           Block::make(not_no_op(x + 1), not_no_op(x + 2))),
          Block::make(IfThenElse::make(x < y, Evaluate::make(0), not_no_op(x + 1)),
                      not_no_op(x + 2)));

    check(IfThenElse::make(x < y,
                           IfThenElse::make(z < 4, not_no_op(x + 2)),
                           IfThenElse::make(z < 4, not_no_op(x + 3))),
          IfThenElse::make(z < 4,
                           IfThenElse::make(x < y, not_no_op(x + 2), not_no_op(x + 3))));

    // A for loop is also an if statement that the extent is greater than zero
    Stmt body = AssertStmt::make(y == z, y);
    Stmt loop = For::make("t", 0, x, ForType::Serial, DeviceAPI::None, body);
    check(IfThenElse::make(0 < x, loop), loop);

    // A for loop where the extent is exactly one is just the body
    check(IfThenElse::make(x == 1, loop), IfThenElse::make(x == 1, body));

    // Check we can learn from conditions on variables
    check(IfThenElse::make(x < 5, not_no_op(min(x, 17))),
          IfThenElse::make(x < 5, not_no_op(x)));

    check(IfThenElse::make(x < min(y, 5), not_no_op(min(x, 17))),
          IfThenElse::make(x < min(y, 5), not_no_op(x)));

    check(IfThenElse::make(5 < x, not_no_op(max(x, 2))),
          IfThenElse::make(5 < x, not_no_op(x)));

    check(IfThenElse::make(max(y, 5) < x, not_no_op(max(x, 2))),
          IfThenElse::make(max(y, 5) < x, not_no_op(x)));

    check(IfThenElse::make(x <= 5, not_no_op(min(x, 17))),
          IfThenElse::make(x <= 5, not_no_op(x)));

    check(IfThenElse::make(x <= min(y, 5), not_no_op(min(x, 17))),
          IfThenElse::make(x <= min(y, 5), not_no_op(x)));

    check(IfThenElse::make(5 <= x, not_no_op(max(x, 2))),
          IfThenElse::make(5 <= x, not_no_op(x)));

    check(IfThenElse::make(max(y, 5) <= x, not_no_op(max(x, 2))),
          IfThenElse::make(max(y, 5) <= x, not_no_op(x)));

    // Concretely, this lets us skip some redundant assertions
    check(Block::make(AssertStmt::make(max(y, 3) < x, x),
                      AssertStmt::make(0 < x, x)),
          Block::make(AssertStmt::make(max(y, 3) < x, x),
                      Evaluate::make(0)));

    // Check it works transitively
    check(IfThenElse::make(0 < x,
                           IfThenElse::make(x < y,
                                            IfThenElse::make(y < z,
                                                             AssertStmt::make(z != 2, x)))),
          // z can't possibly be two, because x is at least one, so y
          // is at least two, so z must be at least three.
          Evaluate::make(0));

    // Simplifications of selects
    check(select(x == 3, 5, 7) + 7, select(x == 3, 12, 14));
    check(select(x == 3, 5, 7) - 7, select(x == 3, -2, 0));
    check(select(x == 3, 5, y) - y, select(x == 3, 5 - y, 0));
    check(select(x == 3, y, 5) - y, select(x == 3, 0, 5 - y));
    check(y - select(x == 3, 5, y), select(x == 3, y, 5) + (-5));
    check(y - select(x == 3, y, 5), select(x == 3, 0, y + (-5)));

    check(select(x == 3, 5, 7) == 7, x != 3);
    check(select(x == 3, z, y) == z, (x == 3) || (y == z));

    check(select(x == 3, 4, 2) == 0, const_false());
    check(select(x == 3, y, 2) == 4, (x == 3) && (y == 4));
    check(select(x == 3, 2, y) == 4, (x != 3) && (y == 4));

    check(min(select(x == 2, y * 3, 8), select(x == 2, y + 8, y * 7)),
          select(x == 2, min(y * 3, y + 8), min(y * 7, 8)));

    check(max(select(x == 2, y * 3, 8), select(x == 2, y + 8, y * 7)),
          select(x == 2, max(y * 3, y + 8), max(y * 7, 8)));

    Expr cond = (x * x == 16);
    check(select(cond, x + 1, x + 5), select(cond, 1, 5) + x);
    check(select(cond, x + y, x + z), select(cond, y, z) + x);
    check(select(cond, y + x, x + z), select(cond, y, z) + x);
    check(select(cond, y + x, z + x), select(cond, y, z) + x);
    check(select(cond, x + y, z + x), select(cond, y, z) + x);
    check(select(cond, x * 2, x * 5), select(cond, 2, 5) * x);
    check(select(cond, x * y, x * z), select(cond, y, z) * x);
    check(select(cond, y * x, x * z), select(cond, y, z) * x);
    check(select(cond, y * x, z * x), select(cond, y, z) * x);
    check(select(cond, x * y, z * x), select(cond, y, z) * x);
    check(select(cond, x - y, x - z), x - select(cond, y, z));
    check(select(cond, y - x, z - x), select(cond, y, z) - x);
    check(select(cond, x + y, x - z), select(cond, y, 0 - z) + x);
    check(select(cond, y + x, x - z), select(cond, y, 0 - z) + x);
    check(select(cond, x - z, x + y), select(cond, 0 - z, y) + x);
    check(select(cond, x - z, y + x), select(cond, 0 - z, y) + x);
    check(select(cond, x / y, z / y), select(cond, x, z) / y);
    check(select(cond, x % y, z % y), select(cond, x, z) % y);

    {
        Expr b[12];
        for (int i = 0; i < 12; i++) {
            b[i] = Variable::make(Bool(), unique_name('b'));
        }

        // Some rules that collapse selects
        check(select(b[0], x, select(b[1], x, y)),
              select(b[0] || b[1], x, y));
        check(select(b[0], x, select(b[1], y, x)),
              select(!b[1] || b[0], x, y));
        check(select(b[0], select(b[1], x, y), x),
              select(!b[1] && b[0], y, x));
        check(select(b[0], select(b[1], y, x), x),
              select(b[0] && b[1], y, x));

        // Ternary boolean expressions in two variables
        check(b[0] || (b[0] && b[1]), b[0]);
        check((b[0] && b[1]) || b[0], b[0]);
        check(b[0] && (b[0] || b[1]), b[0]);
        check((b[0] || b[1]) && b[0], b[0]);
        check(b[0] && (b[0] && b[1]), b[0] && b[1]);
        check((b[0] && b[1]) && b[0], b[0] && b[1]);
        check(b[0] || (b[0] || b[1]), b[0] || b[1]);
        check((b[0] || b[1]) || b[0], b[0] || b[1]);

        // A nasty unsimplified boolean Expr seen in the wild
        Expr nasty = ((((((((((((((((((((((((((((((((((((((((((((b[0] && b[1]) || (b[2] && b[1])) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[6]) || (b[2] && b[6]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[3]) || (b[2] && b[3]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[7]) || (b[2] && b[7]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[4]) || (b[2] && b[4]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[8]) || (b[2] && b[8]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[5]) || (b[2] && b[5]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[10]) || (b[2] && b[10]))) || b[0]) || b[2]) || b[0]) || b[2]) && ((b[0] && b[9]) || (b[2] && b[9]))) || b[0]) || b[2]);
        check(nasty, b[0] || b[2]);
    }

    {
        // verify that likely(const-bool) is *not* simplified.
        check(likely(t), likely(t));
        check(likely(f), likely(f));

        // verify that !likely(e) -> likely(!e)
        check(!likely(t), likely(f));
        check(!likely(f), likely(t));
        check(!likely(x == 2), likely(x != 2));

        // can_prove(likely(const-true)) = true
        // can_prove(!likely(const-false)) = true
        internal_assert(can_prove(likely(t)));
        internal_assert(can_prove(!likely(f)));

        // unprovable cases
        internal_assert(!can_prove(likely(f)));
        internal_assert(!can_prove(!likely(t)));
        internal_assert(!can_prove(!likely(x == 2)));
    }
}

void check_math() {
    Var x = Var("x");

    check(Halide::sqrt(4.0f), 2.0f);
    check(Halide::log(0.5f + 0.5f), 0.0f);
    check(Halide::exp(Halide::log(2.0f)), 2.0f);
    check(Halide::pow(4.0f, 0.5f), 2.0f);
    check(Halide::round(1000.0f * Halide::pow(Halide::exp(1.0f), Halide::log(10.0f))), 10000.0f);

    check(Halide::floor(0.98f), 0.0f);
    check(Halide::ceil(0.98f), 1.0f);
    check(Halide::round(0.6f), 1.0f);
    check(Halide::round(-0.5f), 0.0f);
    check(Halide::trunc(-1.6f), -1.0f);
    check(Halide::floor(round(x)), round(x));
    check(Halide::ceil(ceil(x)), ceil(x));

    check(strict_float(strict_float(x)), strict_float(x));
}

void check_overflow() {
    Expr overflowing[] = {
        make_const(Int(32), 0x7fffffff) + 1,
        make_const(Int(32), 0x7ffffff0) + 16,
        (make_const(Int(32), 0x7fffffff) +
         make_const(Int(32), 0x7fffffff)),
        make_const(Int(32), 0x08000000) * 16,
        (make_const(Int(32), 0x00ffffff) *
         make_const(Int(32), 0x00ffffff)),
        make_const(Int(32), 0x80000000) - 1,
        0 - make_const(Int(32), 0x80000000),
        make_const(Int(64), (int64_t)0x7fffffffffffffffLL) + 1,
        make_const(Int(64), (int64_t)0x7ffffffffffffff0LL) + 16,
        (make_const(Int(64), (int64_t)0x7fffffffffffffffLL) +
         make_const(Int(64), (int64_t)0x7fffffffffffffffLL)),
        make_const(Int(64), (int64_t)0x0800000000000000LL) * 16,
        (make_const(Int(64), (int64_t)0x00ffffffffffffffLL) *
         make_const(Int(64), (int64_t)0x00ffffffffffffffLL)),
        make_const(Int(64), (int64_t)0x8000000000000000LL) - 1,
        0 - make_const(Int(64), (int64_t)0x8000000000000000LL),
    };
    Expr not_overflowing[] = {
        make_const(Int(32), 0x7ffffffe) + 1,
        make_const(Int(32), 0x7fffffef) + 16,
        make_const(Int(32), 0x07ffffff) * 2,
        (make_const(Int(32), 0x0000ffff) *
         make_const(Int(32), 0x00008000)),
        make_const(Int(32), 0x80000001) - 1,
        0 - make_const(Int(32), 0x7fffffff),
        make_const(Int(64), (int64_t)0x7ffffffffffffffeLL) + 1,
        make_const(Int(64), (int64_t)0x7fffffffffffffefLL) + 16,
        make_const(Int(64), (int64_t)0x07ffffffffffffffLL) * 16,
        (make_const(Int(64), (int64_t)0x00000000ffffffffLL) *
         make_const(Int(64), (int64_t)0x0000000080000000LL)),
        make_const(Int(64), (int64_t)0x8000000000000001LL) - 1,
        0 - make_const(Int(64), (int64_t)0x7fffffffffffffffLL),
    };

    for (Expr e : overflowing) {
        internal_assert(!is_const(simplify(e)))
            << "Overflowing expression should not have simplified: " << e << "\n";
    }
    for (Expr e : not_overflowing) {
        internal_assert(is_const(simplify(e)))
            << "Non-everflowing expression should have simplified: " << e << "\n";
    }

    // We also risk 64-bit overflow when computing the constant bounds of subexpressions
    Expr x = Variable::make(halide_type_of<int64_t>(), "x");
    Expr y = Variable::make(halide_type_of<int64_t>(), "y");

    Expr zero = make_const(Int(64), 0);
    Expr two_32 = make_const(Int(64), (int64_t)1 << 32);
    Expr neg_two_32 = make_const(Int(64), -((int64_t)1 << 32));
    Expr min_64 = make_const(Int(64), INT64_MIN);
    Expr max_64 = make_const(Int(64), INT64_MAX);
    for (int x_pos = 0; x_pos <= 1; x_pos++) {
        for (int y_pos = 0; y_pos <= 1; y_pos++) {
            // Mul
            {
                Scope<Interval> scope;
                if (x_pos) {
                    scope.push("x", {zero, two_32});
                } else {
                    scope.push("x", {neg_two_32, zero});
                }
                if (y_pos) {
                    scope.push("y", {zero, two_32});
                } else {
                    scope.push("y", {neg_two_32, zero});
                }
                if (x_pos == y_pos) {
                    internal_assert(!is_const(simplify((x * y) < two_32, true, scope)));
                } else {
                    internal_assert(!is_const(simplify((x * y) > neg_two_32, true, scope)));
                }
            }
            // Add/Sub
            {
                Scope<Interval> scope;
                if (x_pos) {
                    scope.push("x", {zero, max_64});
                } else {
                    scope.push("x", {min_64, zero});
                }
                if (y_pos) {
                    scope.push("y", {zero, max_64});
                } else {
                    scope.push("y", {min_64, zero});
                }
                if (x_pos && y_pos) {
                    internal_assert(!is_const(simplify((x + y) < two_32, true, scope)));
                } else if (x_pos && !y_pos) {
                    internal_assert(!is_const(simplify((x - y) < two_32, true, scope)));
                } else if (!x_pos && y_pos) {
                    internal_assert(!is_const(simplify((x - y) > neg_two_32, true, scope)));
                } else {
                    internal_assert(!is_const(simplify((x + y) > neg_two_32, true, scope)));
                }
            }
        }
    }
}

template<typename T>
void check_clz(uint64_t value, uint64_t result) {
    Expr x = Variable::make(halide_type_of<T>(), "x");
    check(Let::make("x", cast<T>(Expr(value)), count_leading_zeros(x)), cast<T>(Expr(result)));

    Type vt = halide_type_of<T>().with_lanes(4);
    Expr xv = Variable::make(vt, "x");
    check(Let::make("x", cast(vt, broadcast(Expr(value), 4)), count_leading_zeros(xv)), cast(vt, broadcast(Expr(result), 4)));
}

template<typename T>
void check_ctz(uint64_t value, uint64_t result) {
    Expr x = Variable::make(halide_type_of<T>(), "x");
    check(Let::make("x", cast<T>(Expr(value)), count_trailing_zeros(x)), cast<T>(Expr(result)));

    Type vt = halide_type_of<T>().with_lanes(4);
    Expr xv = Variable::make(vt, "x");
    check(Let::make("x", cast(vt, broadcast(Expr(value), 4)), count_trailing_zeros(xv)), cast(vt, broadcast(Expr(result), 4)));
}

template<typename T>
void check_popcount(uint64_t value, uint64_t result) {
    Expr x = Variable::make(halide_type_of<T>(), "x");
    check(Let::make("x", cast<T>(Expr(value)), popcount(x)), cast<T>(Expr(result)));

    Type vt = halide_type_of<T>().with_lanes(4);
    Expr xv = Variable::make(vt, "x");
    check(Let::make("x", cast(vt, broadcast(Expr(value), 4)), popcount(xv)), cast(vt, broadcast(Expr(result), 4)));
}

void check_bitwise() {
    Expr x = Var("x");

    // Check bitshift operations
    check(cast(Int(16), x) << 10, cast(Int(16), x) * 1024);
    check(cast(Int(16), x) >> 10, cast(Int(16), x) / 1024);

    // Shift by negative amount is a shift in the opposite direction
    check(cast(Int(16), x) << -10, cast(Int(16), x) / 1024);
    check(cast(Int(16), x) >> -10, cast(Int(16), x) * 1024);

    // Shift by >= type size is an overflow
    check_is_sio(cast(Int(16), x) << 20);
    check_is_sio(cast(Int(16), x) >> 20);

    // Check bitwise_and. (Added as result of a bug.)
    // TODO: more coverage of bitwise_and and bitwise_or.
    check(cast(UInt(32), x) & Expr((uint32_t)0xaaaaaaaa),
          cast(UInt(32), x) & Expr((uint32_t)0xaaaaaaaa));

    // Check constant-folding of bitwise ops (and indirectly, reinterpret)
    check(Let::make(x.as<Variable>()->name, 5, (((~x) & 3) | 16) ^ 33), ((~5 & 3) | 16) ^ 33);
    check(Let::make(x.as<Variable>()->name, 5, (((~cast<uint8_t>(x)) & 3) | 16) ^ 33), make_const(UInt(8), ((~5 & 3) | 16) ^ 33));

    // Check bitwise ops of constant broadcasts.
    Expr v = Broadcast::make(12, 4);
    check(v >> 2, Broadcast::make(3, 4));
    check(Broadcast::make(32768, 4) >> 1, Broadcast::make(16384, 4));
    check((Broadcast::make(1, 4) << 15) >> 1, Broadcast::make(16384, 4));
    check(Ramp::make(0, 1, 4) << Broadcast::make(4, 4), Ramp::make(0, 16, 4));

    check_clz<int8_t>(10, 4);
    check_clz<int16_t>(10, 12);
    check_clz<int32_t>(10, 28);
    check_clz<int64_t>(10, 60);
    check_clz<uint8_t>(10, 4);
    check_clz<uint16_t>(10, 12);
    check_clz<uint32_t>(10, 28);
    check_clz<uint64_t>(10, 60);
    check_clz<uint64_t>(10ULL << 32, 28);

    check_ctz<int8_t>(64, 6);
    check_ctz<int16_t>(64, 6);
    check_ctz<int32_t>(64, 6);
    check_ctz<int64_t>(64, 6);
    check_ctz<uint8_t>(64, 6);
    check_ctz<uint16_t>(64, 6);
    check_ctz<uint32_t>(64, 6);
    check_ctz<uint64_t>(64, 6);
    check_ctz<uint64_t>(64ULL << 32, 38);

    check_popcount<int8_t>(0xa5, 4);
    check_popcount<int16_t>(0xa5a5, 8);
    check_popcount<int32_t>(0xa5a5a5a5, 16);
    check_popcount<int64_t>(0xa5a5a5a5a5a5a5a5, 32);
    check_popcount<uint8_t>(0xa5, 4);
    check_popcount<uint16_t>(0xa5a5, 8);
    check_popcount<uint32_t>(0xa5a5a5a5, 16);
    check_popcount<uint64_t>(0xa5a5a5a5a5a5a5a5, 32);
}

void check_lets() {
    Expr x = Var("x"), y = Var("y");
    Expr v = Variable::make(Int(32, 4), "v");
    Expr a = Variable::make(Int(32), "a");
    Expr b = Variable::make(Int(32), "b");
    // Check constants get pushed inwards
    check(Let::make("x", 3, x + 4), 7);

    // Check ramps in lets get pushed inwards
    check(Let::make("v", ramp(x * 2 + 7, 3, 4), v + Expr(broadcast(2, 4))),
          ramp(x * 2 + 9, 3, 4));

    // Check broadcasts in lets get pushed inwards
    check(Let::make("v", broadcast(x, 4), v + Expr(broadcast(2, 4))),
          broadcast(x + 2, 4));

    // Check that dead lets get stripped
    check(Let::make("x", 3 * y * y * y, 4), 4);
    check(Let::make("a", 3 * y * y * y, Let::make("b", 4 * a * a * a, b - b)), 0);
    check(Let::make("a", b / 2, a - a), 0);
    check(Let::make("a", b / 2 + (x + y) * 64, a - a), 0);
    check(Let::make("x", 3 * y * y * y, x - x), 0);
    check(Let::make("x", 0, 0), 0);

    // Check that lets inside an evaluate node get lifted
    check(Evaluate::make(Let::make("x", Call::make(Int(32), "dummy", {3, x, 4}, Call::Extern), Let::make("y", 10, x + y + 2))),
          LetStmt::make("x", Call::make(Int(32), "dummy", {3, x, 4}, Call::Extern), Evaluate::make(x + 12)));
}

void check_inv(Expr before) {
    Expr after = simplify(before);
    internal_assert(before.same_as(after))
        << "Expressions should be equal by value and by identity: "
        << " Before: " << before << "\n"
        << " After: " << after << "\n";
}

void check_invariant() {
    // Check a bunch of expressions *don't* simplify. These should try
    // and then fail to match every single rule (which should trigger
    // fuzz testing of each as a side effect). The final expression
    // should be exactly the same object as the input.
    for (Type t : {UInt(1), UInt(8), UInt(16), UInt(32), UInt(64),
                   Int(8), Int(16), Int(32), Int(64),
                   Float(32), Float(64)}) {
        Expr x = Variable::make(t, "x");
        Expr y = Variable::make(t, "y");
        Expr z = Variable::make(t, "z");
        Expr w = Variable::make(t, "w");
        check_inv(x + y);
        check_inv(x - y);
        check_inv(x % y);
        check_inv(x * y);
        check_inv(x / y);
        check_inv(min(x, y));
        check_inv(max(x, y));
        check_inv(x == y);
        check_inv(x != y);
        check_inv(x < y);
        check_inv(x <= y);
        if (t.is_bool()) {
            check_inv(x && y);
            check_inv(x || y);
            check_inv(!x);
        }
        check_inv(select(x == y, z, w));
    }
}

void check_unreachable() {
    Var x("x"), y("y");

    check(x + unreachable(), unreachable());

    check(Block::make(not_no_op(x), Evaluate::make(unreachable())),
          Evaluate::make(unreachable()));
    check(Block::make(Evaluate::make(unreachable()), not_no_op(x)),
          Evaluate::make(unreachable()));

    check(Block::make(not_no_op(y), IfThenElse::make(x != 0, Evaluate::make(unreachable()), Evaluate::make(unreachable()))),
          Evaluate::make(unreachable()));
    check(IfThenElse::make(x != 0, not_no_op(y), Evaluate::make(unreachable())),
          not_no_op(y));
    check(IfThenElse::make(x != 0, Evaluate::make(unreachable()), not_no_op(y)),
          not_no_op(y));

    check(y + Call::make(Int(32), Call::if_then_else, {x != 0, unreachable(), unreachable()}, Call::PureIntrinsic),
          unreachable());
    check(Call::make(Int(32), Call::if_then_else, {x != 0, y, unreachable()}, Call::PureIntrinsic), y);
    check(Call::make(Int(32), Call::if_then_else, {x != 0, unreachable(), y}, Call::PureIntrinsic), y);

    check(Block::make(not_no_op(y), For::make("i", 0, 1, ForType::Serial, DeviceAPI::None, Evaluate::make(unreachable()))),
          Evaluate::make(unreachable()));
    check(For::make("i", 0, x, ForType::Serial, DeviceAPI::None, Evaluate::make(unreachable())),
          Evaluate::make(0));
}

int main(int argc, char **argv) {
    check_invariant();
    check_casts();
    check_algebra();
    check_vectors();
    check_bounds();
    check_math();
    check_boolean();
    check_overflow();
    check_bitwise();
    check_lets();
    check_unreachable();

    // Miscellaneous cases that don't fit into one of the categories above.
    Expr x = Var("x"), y = Var("y");

    // Check that constant args to a stringify get combined
    check(Call::make(type_of<const char *>(), Call::stringify, {3, std::string(" "), 4}, Call::PureIntrinsic),
          std::string("3 4"));

    check(Call::make(type_of<const char *>(), Call::stringify, {3, x, 4, std::string(", "), 3.4f}, Call::PureIntrinsic),
          Call::make(type_of<const char *>(), Call::stringify, {std::string("3"), x, std::string("4, 3.400000")}, Call::PureIntrinsic));

    {
        // Check that contiguous prefetch call get collapsed
        Expr base = Variable::make(Handle(), "buf");
        Expr offset = x;
        check(Call::make(Int(32), Call::prefetch, {base, offset, 4, 1, 64, 4, min(x + y, 128), 256}, Call::Intrinsic),
              Call::make(Int(32), Call::prefetch, {base, offset, min(x + y, 128) * 256, 1}, Call::Intrinsic));
    }

    // This expression is a good stress-test. It caused exponential
    // slowdown at one point in time, and constant folding leading to
    // overflow at another.
    {
        Expr e = x;
        for (int i = 0; i < 100; i++) {
            e = max(e, 1) / 2;
        }
        check(e, e);
    }

    // This expression used to cause infinite recursion.
    check(Broadcast::make(-16, 2) < (ramp(Cast::make(UInt(16), 7), Cast::make(UInt(16), 11), 2) - Broadcast::make(1, 2)),
          Broadcast::make(-15, 2) < (ramp(make_const(UInt(16), 7), make_const(UInt(16), 11), 2)));

    {
        // Verify that integer types passed to min() and max() are coerced to match
        // Exprs, rather than being promoted to int first. (TODO: This doesn't really
        // belong in the test for Simplify, but IROperator has no test unit of its own.)
        Expr one = cast<uint16_t>(1);
        const int two = 2;  // note that type is int, not uint16_t
        Expr r1, r2, r3;

        r1 = min(one, two);
        internal_assert(r1.type() == halide_type_of<uint16_t>());
        r2 = min(one, two, one);
        internal_assert(r2.type() == halide_type_of<uint16_t>());
        // Explicitly passing 'two' as an Expr, rather than an int, will defeat this logic.
        r3 = min(one, Expr(two), one);
        internal_assert(r3.type() == halide_type_of<int>());

        r1 = max(one, two);
        internal_assert(r1.type() == halide_type_of<uint16_t>());
        r2 = max(one, two, one);
        internal_assert(r2.type() == halide_type_of<uint16_t>());
        // Explicitly passing 'two' as an Expr, rather than an int, will defeat this logic.
        r3 = max(one, Expr(two), one);
        internal_assert(r3.type() == halide_type_of<int>());
    }

    {
        Expr x = Variable::make(UInt(32), "x");
        Expr y = Variable::make(UInt(32), "y");
        // This is used to get simplified into broadcast(x - y, 2) which is
        // incorrect when there is overflow.
        Expr e = simplify(max(ramp(x, y, 2), broadcast(x, 2)) - max(broadcast(y, 2), ramp(y, y, 2)));
        Expr expected = max(ramp(x, y, 2), broadcast(x, 2)) - max(ramp(y, y, 2), broadcast(y, 2));
        check(e, expected);
    }

    // Check that provably-true require() expressions are simplified away
    {
        Expr result(42);

        check(require(Expr(1) > Expr(0), result, "error"), result);
        check(require(x == x, result, "error"), result);
    }

    // Check that is_nan() returns a boolean result for constant inputs
    {
        check(Halide::is_nan(cast<float16_t>(Expr(0.f))), const_false());
        check(Halide::is_nan(Expr(0.f)), const_false());
        check(Halide::is_nan(Expr(0.0)), const_false());

        check(Halide::is_nan(Expr(cast<float16_t>(std::nanf("1")))), const_true());
        check(Halide::is_nan(Expr(std::nanf("1"))), const_true());
        check(Halide::is_nan(Expr(std::nan("1"))), const_true());
    }

    // Check that is_inf() returns a boolean result for constant inputs
    {
        constexpr float inf32 = std::numeric_limits<float>::infinity();
        constexpr double inf64 = std::numeric_limits<double>::infinity();

        check(Halide::is_inf(cast<float16_t>(Expr(0.f))), const_false());
        check(Halide::is_inf(Expr(0.f)), const_false());
        check(Halide::is_inf(Expr(0.0)), const_false());

        check(Halide::is_inf(Expr(cast<float16_t>(inf32))), const_true());
        check(Halide::is_inf(Expr(inf32)), const_true());
        check(Halide::is_inf(Expr(inf64)), const_true());

        check(Halide::is_inf(Expr(cast<float16_t>(-inf32))), const_true());
        check(Halide::is_inf(Expr(-inf32)), const_true());
        check(Halide::is_inf(Expr(-inf64)), const_true());
    }

    // Check that is_finite() returns a boolean result for constant inputs
    {
        constexpr float inf32 = std::numeric_limits<float>::infinity();
        constexpr double inf64 = std::numeric_limits<double>::infinity();

        check(Halide::is_finite(cast<float16_t>(Expr(0.f))), const_true());
        check(Halide::is_finite(Expr(0.f)), const_true());
        check(Halide::is_finite(Expr(0.0)), const_true());

        check(Halide::is_finite(Expr(cast<float16_t>(std::nanf("1")))), const_false());
        check(Halide::is_finite(Expr(std::nanf("1"))), const_false());
        check(Halide::is_finite(Expr(std::nan("1"))), const_false());

        check(Halide::is_finite(Expr(cast<float16_t>(inf32))), const_false());
        check(Halide::is_finite(Expr(inf32)), const_false());
        check(Halide::is_finite(Expr(inf64)), const_false());

        check(Halide::is_finite(Expr(cast<float16_t>(-inf32))), const_false());
        check(Halide::is_finite(Expr(-inf32)), const_false());
        check(Halide::is_finite(Expr(-inf64)), const_false());
    }

    {
        using ConciseCasts::i32;

        // Wrap all in i32() to ensure C++ won't optimize our multiplies away at compiletime
        Expr e = max(max(max(i32(-1074233344) * i32(-32767), i32(-32783) * i32(32783)), i32(32767) * i32(-32767)), i32(1074200561) * i32(32783)) / i32(64);
        Expr e2 = e / i32(2);
        check_is_sio(e2);
    }

    {
        Expr m = Int(32).max();
        Expr e = m + m;
        Expr l = Let::make("x", e, x + 1);
        Expr sl = substitute_in_all_lets(simplify(l));
        check_is_sio(sl);
    }

    {
        using ConciseCasts::i16;

        const Expr a = Expr(std::numeric_limits<int16_t>::lowest());
        const Expr b = Expr(std::numeric_limits<int16_t>::max());

        check(a >> 14, i16(-2));
        check(a << 14, i16(0));
        check(a >> 15, i16(-1));
        check(a << 15, i16(0));

        check(b >> 14, i16(1));
        check(b << 14, i16(-16384));
        check(b >> 15, i16(0));
        check(b << 15, i16(-32768));
    }

    {
        using ConciseCasts::u16;

        const Expr a = Expr(std::numeric_limits<uint16_t>::lowest());
        const Expr b = Expr(std::numeric_limits<uint16_t>::max());

        check(a >> 15, u16(0));
        check(b >> 15, u16(1));
        check(a << 15, u16(0));
        check(b << 15, Expr((uint16_t)0x8000));
    }

    {
        using ConciseCasts::i64;

        const Expr a = Expr(std::numeric_limits<int64_t>::lowest());
        const Expr b = Expr(std::numeric_limits<int64_t>::max());

        check(a >> 62, i64(-2));
        check_is_sio(a << 62);
        check(a >> 63, i64(-1));
        check(a << 63, i64(0));

        check(b >> 62, i64(1));
        check_is_sio(b << 62);
        check(b >> 63, i64(0));
        check(b << 63, Expr(std::numeric_limits<int64_t>::lowest()));
    }

    {
        using ConciseCasts::u64;

        const Expr a = Expr(std::numeric_limits<uint64_t>::lowest());
        const Expr b = Expr(std::numeric_limits<uint64_t>::max());

        check(a >> 63, u64(0));
        check(b >> 63, u64(1));
        check(a << 63, u64(0));
        check(b << 63, Expr((uint64_t)0x8000000000000000ULL));
    }

    {
        Expr vec_x = Variable::make(Int(32, 32), "x");
        Expr vec_y = Variable::make(Int(32, 32), "y");
        Expr vec_z = Variable::make(Int(32, 32), "z");
        check(slice(slice(vec_x, 2, 3, 8), 3, 2, 3), slice(vec_x, 11, 6, 3));
        check(slice(concat_vectors({vec_x, vec_y, vec_z}), 0, 2, 32), slice(concat_vectors({vec_x, vec_y}), 0, 2, 32));
        check(slice(concat_vectors({vec_x, vec_y, vec_z}), 1, 2, 32), slice(concat_vectors({vec_x, vec_y}), 1, 2, 32));
        check(slice(concat_vectors({vec_x, vec_y, vec_z}), 2, 2, 32), slice(concat_vectors({vec_x, vec_y, vec_z}), 2, 2, 32));
        check(slice(concat_vectors({vec_x, vec_y, vec_z}), 2, 2, 31), slice(concat_vectors({vec_x, vec_y}), 2, 2, 31));
        check(slice(concat_vectors({vec_x, vec_y, vec_z}), 0, 2, 16), slice(concat_vectors({vec_x}), 0, 2, 16));
        check(slice(concat_vectors({vec_x, vec_y, vec_z}), 32, 2, 22), slice(concat_vectors({vec_y, vec_z}), 0, 2, 22));
        check(slice(concat_vectors({vec_x, vec_y, vec_z}), 33, 2, 16), slice(concat_vectors({vec_y}), 1, 2, 16));
    }

    {
        Stmt body = AssertStmt::make(x > 0, y);
        check(For::make("t", 0, x, ForType::Serial, DeviceAPI::None, body),
              Evaluate::make(0));
    }

    {
        check(concat_bits({x}), x);
    }

    // Check a bounds-related fuzz tester failure found in issue https://github.com/halide/Halide/issues/3764
    check(Let::make("b", 105, 336 / max(cast<int32_t>(cast<int16_t>(Variable::make(Int(32), "b"))), 38) + 29), 32);

    printf("Success!\n");

    return 0;
}
