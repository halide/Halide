#include "PatternMatchIntrinsics.h"
#include "CodeGen_Internal.h"
#include "ConciseCasts.h"
#include "CSE.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using namespace Halide::ConciseCasts;

namespace {

Expr saturating_narrow(Expr a) {
    Type narrow = a.type().with_bits(a.type().bits() / 2);
    return saturating_cast(narrow, a);
}

// Returns true iff t is an integral type where overflow is undefined
bool no_overflow_int(Type t) {
    return t.is_int() && t.bits() >= 32;
}

// Returns true iff t does not have a well defined overflow behavior.
bool no_overflow(Type t) {
    return t.is_float() || no_overflow_int(t);
}

// Add helpers to let us (mostly) use intrinsics without the boilerplate in patterns.
// These only work if the first argument is a wildcard.
template <int A, typename B>
auto widening_add(IRMatcher::Wild<A> a, B b) {
    return IRMatcher::intrin(Call::widening_add, a, b);
}
template <int A, typename B>
auto widening_sub(IRMatcher::Wild<A> a, B b) {
    return IRMatcher::intrin(Call::widening_sub, a, b);
}
template <int A, typename B>
auto widening_mul(IRMatcher::Wild<A> a, B b) {
    return IRMatcher::intrin(Call::widening_mul, a, b);
}
template <int A, typename B>
auto saturating_add(IRMatcher::Wild<A> a, B b) {
    return IRMatcher::intrin(Call::saturating_add, a, b);
}
template <int A, typename B>
auto saturating_sub(IRMatcher::Wild<A> a, B b) {
    return IRMatcher::intrin(Call::saturating_sub, a, b);
}
template <int A, typename B>
auto halving_add(IRMatcher::Wild<A> a, B b) {
    return IRMatcher::intrin(Call::halving_add, a, b);
}
template <int A, typename B>
auto halving_sub(IRMatcher::Wild<A> a, B b) {
    return IRMatcher::intrin(Call::halving_sub, a, b);
}
template <int A, typename B>
auto rounding_halving_add(IRMatcher::Wild<A> a, B b) {
    return IRMatcher::intrin(Call::rounding_halving_add, a, b);
}
template <int A, typename B>
auto rounding_halving_sub(IRMatcher::Wild<A> a, B b) {
    return IRMatcher::intrin(Call::rounding_halving_sub, a, b);
}
template <int A, typename B>
auto shift_left(IRMatcher::Wild<A> a, B b) {
    return IRMatcher::intrin(Call::shift_left, a, b);
}
template <int A, typename B>
auto shift_right(IRMatcher::Wild<A> a, B b) {
    return IRMatcher::intrin(Call::shift_right, a, b);
}
template <int A, typename B>
auto rounding_shift_left(IRMatcher::Wild<A> a, B b) {
    return IRMatcher::intrin(Call::rounding_shift_left, a, b);
}
template <int A, typename B>
auto rounding_shift_right(IRMatcher::Wild<A> a, B b) {
    return IRMatcher::intrin(Call::rounding_shift_right, a, b);
}

// If there's a widening add or subtract in the first e.type().bits() / 2 - 1
// levels down a tree of adds or subtracts, we know there's enough headroom for
// another add without overflow.
bool find_widening_add_or_subtract(const Expr &e, int max_depth) {
    if (max_depth-- <= 0) {
        return false;
    }
    if (const Add *add = e.as<Add>()) {
        return find_widening_add_or_subtract(add->a, max_depth) || find_widening_add_or_subtract(add->b, max_depth);
    } else if (const Sub *sub = e.as<Sub>()) {
        return find_widening_add_or_subtract(sub->a, max_depth) || find_widening_add_or_subtract(sub->b, max_depth);
    } else if (Call::as_intrinsic(e, {Call::widening_add, Call::widening_sub})) {
        return true;
    }
    return false;
}

bool find_widening_add_or_subtract(const Expr &e) {
    return find_widening_add_or_subtract(e, e.type().bits() / 2 - 1);
}

// We want to find and remove an add of 'term' from e.
Expr find_and_subtract(const Expr &e, const Expr &round) {
    if (const Add *add = e.as<Add>()) {
        Expr a = find_and_subtract(add->a, round);
        if (!a.same_as(add->a)) {
            return Add::make(a, add->b);
        }
        Expr b = find_and_subtract(add->b, round);
        if (!b.same_as(add->b)) {
            return Add::make(add->a, b);
        }
    } else if (const Sub *sub = e.as<Sub>()) {
        Expr a = find_and_subtract(sub->a, round);
        if (!a.same_as(sub->a)) {
            return Sub::make(a, sub->b);
        }
        // We can't recurse into the negatve part of a subtract.
    } else if (can_prove(e == round)) {
        return make_zero(e.type());
    }
    return e;
}

Expr to_rounding_shift(const Call *c) {
    if (c->is_intrinsic(Call::shift_left) || c->is_intrinsic(Call::shift_right)) {
        internal_assert(c->args.size() == 2);
        Expr a = c->args[0];
        Expr b = c->args[1];

        // Helper to make the appropriate shift.
        auto rounding_shift = [&](const Expr &a, const Expr &b) {
            if (c->is_intrinsic(Call::shift_right)) {
                return rounding_shift_right(a, b);
            } else {
                return rounding_shift_left(a, b);
            }
        };

        // The rounding offset for the shift we have.
        Expr round;
        if (c->is_intrinsic(Call::shift_right)) {
            round = simplify((make_const(a.type().with_lanes(1), 1) << max(b, 0)) >> 1);
        } else {
            round = simplify((make_const(a.type().with_lanes(1), 1) >> min(b, 0)) >> 1);
        }

        // We can always handle widening or saturating adds.
        if (const Call *add = Call::as_intrinsic(a, {Call::widening_add, Call::saturating_add})) {
            if (can_prove(add->args[0] == round)) {
                return rounding_shift(cast(add->type, add->args[1]), b);
            } else if (can_prove(add->args[1] == round)) {
                return rounding_shift(cast(add->type, add->args[0]), b);
            }
        }

        // If it wasn't a widening or saturating add, we might still
        // be able to safely accept the rounding.
        Expr a_less_round = find_and_subtract(a, round);
        if (!a_less_round.same_as(a)) {
            // We found and removed the rounding. However, we may have just changed
            // behavior due to overflow. This is still save if the type is not
            // overflowing, or we can find a widening add or subtract in the tree
            // of adds/subtracts. This is a common pattern, e.g.
            // rounding_halving_add(a, b) = shift_round(widening_add(a, b) + 1, 1).
            // TODO: This could be done with bounds inference instead of this hack
            // if it supported intrinsics like widening_add.
            if (no_overflow(a.type()) || find_widening_add_or_subtract(a_less_round)) {
                return rounding_shift(simplify(a_less_round), b);
            }
        }
    }

    return Expr();
}

// Perform peephole optimizations on the IR, adding appropriate
// interleave and deinterleave calls.
class PatternMatchIntrinsics : public IRMutator {
protected:
    using IRMutator::visit;

    IRMatcher::Wild<0> x;
    IRMatcher::Wild<1> y;
    IRMatcher::Wild<2> z;
    IRMatcher::Wild<3> w;
    IRMatcher::Wild<4> u;
    IRMatcher::Wild<5> v;
    IRMatcher::WildConst<0> c0;
    IRMatcher::WildConst<1> c1;
    IRMatcher::WildConst<2> c2;
    IRMatcher::WildConst<3> c3;
    IRMatcher::WildConst<4> c4;
    IRMatcher::WildConst<5> c5;

    Expr visit(const Add *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        for (halide_type_code_t code : {op->type.code(), halide_type_uint}) {
            Type narrow = op->type.with_bits(op->type.bits() / 2).with_code(code);
            Expr narrow_a = lossless_cast(narrow, a);
            Expr narrow_b = lossless_cast(narrow, b);

            if (narrow_a.defined() && narrow_b.defined()) {
                Expr result = widening_add(narrow_a, narrow_b);
                if (result.type() != op->type) {
                    result = Cast::make(op->type, result);
                }
                return result;
            }
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Add::make(a, b);
        }
    }

    Expr visit(const Sub *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        for (halide_type_code_t code : {halide_type_int, halide_type_uint}) {
            Type narrow = op->type.with_bits(op->type.bits() / 2).with_code(code);
            Expr narrow_a = lossless_cast(narrow, a);
            Expr narrow_b = lossless_cast(narrow, b);

            if (narrow_a.defined() && narrow_b.defined()) {
                Expr result = widening_sub(narrow_a, narrow_b);
                if (result.type() != op->type) {
                    result = Cast::make(op->type, result);
                }
                return result;
            }
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Sub::make(a, b);
        }
    }

    Expr visit(const Mul *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        // Rewrite multiplies to shifts if possible.
        if (op->type.is_int() || op->type.is_uint()) {
            int pow2 = 0;
            if (is_const_power_of_two_integer(a, &pow2)) {
                return b << pow2;
            } else if (is_const_power_of_two_integer(b, &pow2)) {
                return a << pow2;
            }
        }

        // We're applying this to float, which seems OK? float16 * float16 -> float32 is a widening multiply?
        Type narrow = op->type.with_bits(op->type.bits() / 2);
        Expr narrow_a = lossless_cast(narrow, a);
        Expr narrow_b = lossless_cast(narrow, b);

        if (narrow_a.defined() && narrow_b.defined()) {
            return widening_mul(narrow_a, narrow_b);
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Mul::make(a, b);
        }
    }

    Expr visit(const Div *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        if (!op->type.is_float()) {
            return mutate(lower_int_uint_div(a, b));
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Div::make(a, b);
        }
    }

    Expr visit(const Mod *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        if (!op->type.is_float()) {
            return mutate(lower_int_uint_mod(a, b));
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Mod::make(a, b);
        }
    }

    Expr visit(const Cast *op) override {
        Expr value = mutate(op->value);

        if (op->type.is_int() || op->type.is_uint()) {
            Expr lower = cast(value.type(), op->type.min());
            Expr upper = cast(value.type(), op->type.max());

            auto rewrite = IRMatcher::rewriter(value, op->type);

            int bits = op->type.bits();
            auto is_same_int = is_int(x, bits);
            auto is_same_uint = is_uint(x, bits);
            auto is_same_int_or_uint = is_same_int || is_same_uint;
            // clang-format off
            if (rewrite(max(min(widening_add(x, y), upper), lower), saturating_add(x, y), is_same_int_or_uint) ||
                rewrite(max(min(widening_sub(x, y), upper), lower), saturating_sub(x, y), is_same_int_or_uint) ||
                rewrite(min(widening_add(x, y), upper), saturating_add(x, y), is_same_uint) ||
                rewrite(max(widening_sub(x, y), 0), saturating_sub(x, y), is_same_uint) ||
                rewrite(intrin(Call::shift_right, widening_add(x, y), 1), halving_add(x, y), is_same_int_or_uint) ||
                rewrite(intrin(Call::shift_right, widening_sub(x, y), 1), halving_sub(x, y), is_same_int_or_uint) ||
                rewrite(intrin(Call::rounding_shift_right, widening_add(x, y), 1), rounding_halving_add(x, y), is_same_int_or_uint) ||
                rewrite(intrin(Call::rounding_shift_right, widening_sub(x, y), 1), halving_sub(x, y), is_same_int) ||
                false) {
                return rewrite.result;
            }
            // clang-format on
        }

        if (value.same_as(op->value)) {
            return op;
        } else {
            return Cast::make(op->type, value);
        }
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::shift_right) || op->is_intrinsic(Call::shift_left)) {
            internal_assert(op->args.size() == 2);
            Expr a = mutate(op->args[0]);
            Expr b = mutate(op->args[1]);

            Expr result;
            if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
                result = op;
            } else if (op->is_intrinsic(Call::shift_right)) {
                result = Call::make(op->type, Call::shift_right, {a, b}, Call::PureIntrinsic);
            } else {
                result = Call::make(op->type, Call::shift_left, {a, b}, Call::PureIntrinsic);
            }

            // Try to turn this into a rounding shift.
            Expr rounding_shift = to_rounding_shift(result.as<Call>());
            if (rounding_shift.defined()) {
                return rounding_shift;
            }

            return result;
        } else {
            return op;
        }
    }
};

}  // namespace

Stmt pattern_match_intrinsics(Stmt s) {
    s = substitute_in_all_lets(s);
    s = PatternMatchIntrinsics().mutate(s);
    s = common_subexpression_elimination(s);
    return s;
}

Expr lower_widening_add(const Expr &a, const Expr &b) {
    return widen(a) + widen(b);
}

Expr lower_widening_sub(const Expr &a, const Expr &b) {
    Type wide = a.type().with_bits(a.type().bits() * 2);
    if (wide.is_uint()) {
        wide = wide.with_code(halide_type_int);
    }
    return cast(wide, a) - cast(wide, b);
}

Expr lower_widening_mul(const Expr &a, const Expr &b) {
    return widen(a) * widen(b);
}

Expr lower_rounding_shift_right(const Expr &a, const Expr &b) {
    Expr round = (make_const(a.type(), 1) << max(b, 0)) >> 1;
    Expr a_rounded = simplify(saturating_add(a, round));
    return Call::make(a.type(), Call::shift_right, {a_rounded, b}, Call::PureIntrinsic);
}

Expr lower_rounding_shift_left(const Expr &a, const Expr &b) {
    Expr round = (make_const(a.type(), 1) >> min(b, 0)) >> 1;
    Expr a_rounded = simplify(saturating_add(a, round));
    return Call::make(a.type(), Call::shift_left, {a_rounded, b}, Call::PureIntrinsic);
}

// These intentionally use the non-lowered versions of widening_add/widening_sub, in the
// hopes that maybe the user of this will be able to use the information. If not, it will
// probably recursively call lower_widening_add/lower_widening_sub.
Expr lower_saturating_add(const Expr &a, const Expr &b) {
    internal_assert(a.type() == b.type());
    return saturating_narrow(widening_add(a, b));
}
Expr lower_saturating_sub(const Expr &a, const Expr &b) {
    internal_assert(a.type() == b.type());
    return saturating_cast(a.type(), widening_sub(a, b));
}

Expr lower_halving_add(const Expr &a, const Expr &b) {
    internal_assert(a.type() == b.type());
    Expr result_2x = widening_add(a, b);
    return Cast::make(a.type(), result_2x >> 1);
}

Expr lower_rounding_halving_add(const Expr &a, const Expr &b) {
    internal_assert(a.type() == b.type());
    Expr result_2x = widening_add(a, b);
    return Cast::make(a.type(), rounding_shift_right(result_2x, 1));
}

Expr lower_halving_sub(const Expr &a, const Expr &b) {
    internal_assert(a.type() == b.type());
    Expr result_2x = widening_sub(a, b);
    return Cast::make(a.type(), result_2x >> 1);
}

Expr lower_rounding_halving_sub(const Expr &a, const Expr &b) {
    internal_assert(a.type() == b.type());
    Expr result_2x = widening_sub(a, b);
    return Cast::make(a.type(), rounding_shift_right(result_2x, 1));
}

Expr lower_mulhi_shr(const Expr &a, const Expr &b, const Expr &shift) {
    Expr p = widening_mul(a, b);
    p = p >> cast(UInt(p.type().bits()), p.type().bits() / 2);
    return narrow(p) >> shift;
}

Expr lower_sorted_avg(const Expr &a, const Expr &b) {
    // b > a, so the following works without widening:
    // a + (b - a)/2
    // TODO: This is tricky. Targets with halving_sub would be better off using that,
    // but presumably targets that have that also have halving_add, so there's no reason
    // to use this.
    return a + (b - a) / 2;
}

Expr lower_abs(const Expr &a) {
    // Generate select(x >= 0, x, -x) instead
    std::string x_name = unique_name('x');
    Expr x = Variable::make(a.type(), x_name);
    return Let::make(x_name, a, select(x >= 0, x, -x));
}

Expr lower_absd(const Expr &a, const Expr &b) {
    // Use a select instead
    std::string a_name = unique_name('a');
    std::string b_name = unique_name('b');
    Expr a_var = Variable::make(a.type(), a_name);
    Expr b_var = Variable::make(b.type(), b_name);
    return Let::make(a_name, a,
                      Let::make(b_name, b,
                                Select::make(a_var < b_var, b_var - a_var, a_var - b_var)));
}

Expr lower_intrinsic(const Call *op) {
    if (op->is_intrinsic(Call::widening_add)) {
        internal_assert(op->args.size() == 2);
        return lower_widening_add(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::widening_sub)) {
        internal_assert(op->args.size() == 2);
        return lower_widening_sub(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::widening_mul)) {
        internal_assert(op->args.size() == 2);
        return lower_widening_mul(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::rounding_shift_right)) {
        internal_assert(op->args.size() == 2);
        return lower_rounding_shift_right(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::rounding_shift_left)) {
        internal_assert(op->args.size() == 2);
        return lower_rounding_shift_left(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::halving_add)) {
        internal_assert(op->args.size() == 2);
        return lower_halving_add(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::halving_sub)) {
        internal_assert(op->args.size() == 2);
        return lower_halving_sub(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::rounding_halving_add)) {
        internal_assert(op->args.size() == 2);
        return lower_rounding_halving_add(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::rounding_halving_sub)) {
        internal_assert(op->args.size() == 2);
        return lower_rounding_halving_sub(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::mulhi_shr)) {
        internal_assert(op->args.size() == 3);
        return lower_mulhi_shr(op->args[0], op->args[1], op->args[2]);
    } else if (op->is_intrinsic(Call::sorted_avg)) {
        internal_assert(op->args.size() == 2);
        return lower_sorted_avg(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::abs)) {
        internal_assert(op->args.size() == 1);
        return lower_abs(op->args[0]);
    } else if (op->is_intrinsic(Call::absd)) {
        internal_assert(op->args.size() == 2);
        return lower_absd(op->args[0], op->args[1]);
    } else {
        internal_error << "Unknown intrinsic " << op->name;
        return Expr();
    }
}

}  // namespace Internal
}  // namespace Halide
