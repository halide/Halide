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

bool is_widen(const Expr &x) {
    if (const Cast *cast = x.as<Cast>()) {
        return cast->type.code() == cast->value.type().code() && cast->type.bits() > cast->value.type().bits();
    }
    return false;
}

Expr lossless_narrow(const Expr &x) {
    return lossless_cast(x.type().narrow(), x);
}

Expr strip_widening_cast(const Expr &x) {
    Expr narrow = lossless_cast(x.type().narrow(), x);
    if (narrow.defined()) {
        return narrow;
    }
    return lossless_cast(x.type().narrow().with_code(halide_type_uint), x);
}

Expr saturating_narrow(Expr a) {
    Type narrow = a.type().narrow();
    return saturating_cast(narrow, a);
}

Expr make_shift_right(Expr a, int const_b) {
    internal_assert(const_b > 0);
    Expr b = make_const(a.type().with_code(halide_type_uint), const_b);
    return Call::make(a.type(), Call::shift_right, {a, b}, Call::PureIntrinsic);
}

Expr make_rounding_shift_right(Expr a, int const_b) {
    internal_assert(const_b > 0);
    Expr b = make_const(a.type().with_code(halide_type_uint), const_b);
    return Call::make(a.type(), Call::rounding_shift_right, {a, b}, Call::PureIntrinsic);
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
bool is_safe_for_add(const Expr &e, int max_depth) {
    if (max_depth-- <= 0) {
        return false;
    }
    if (const Add *add = e.as<Add>()) {
        return is_safe_for_add(add->a, max_depth) || is_safe_for_add(add->b, max_depth);
    } else if (const Sub *sub = e.as<Sub>()) {
        return is_safe_for_add(sub->a, max_depth) || is_safe_for_add(sub->b, max_depth);
    } else if (Call::as_intrinsic(e, {Call::widening_add, Call::widening_sub})) {
        return true;
    }
    return false;
}

bool is_safe_for_add(const Expr &e) {
    return is_safe_for_add(e, e.type().bits() / 2 - 1);
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
            round = simplify(make_shift_right(make_one(a.type().with_lanes(1)) << max(b, 0), 1));
        } else {
            round = simplify(make_shift_right(make_one(a.type().with_lanes(1)) >> min(b, 0), 1));
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
            if (no_overflow(a.type()) || is_safe_for_add(a_less_round)) {
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

        // Try widening both from the same type as the result, and from uint.
        for (halide_type_code_t code : {op->type.code(), halide_type_uint}) {
            Type narrow = op->type.narrow().with_code(code);
            Expr narrow_a = lossless_cast(narrow, a);
            Expr narrow_b = lossless_cast(narrow, b);

            if (narrow_a.defined() && narrow_b.defined()) {
                Expr result = widening_add(narrow_a, narrow_b);
                if (result.type() != op->type) {
                    result = Cast::make(op->type, result);
                }
                return mutate(result);
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

        // Try widening both from the same type as the result, and from uint.
        for (halide_type_code_t code : {op->type.code(), halide_type_uint}) {
            Type narrow = op->type.narrow().with_code(code);
            Expr narrow_a = lossless_cast(narrow, a);
            Expr narrow_b = lossless_cast(narrow, b);

            if (narrow_a.defined() && narrow_b.defined()) {
                Expr negative_narrow_b = lossless_negate(narrow_b);
                Expr result;
                if (negative_narrow_b.defined()) {
                    result = widening_add(narrow_a, negative_narrow_b);
                } else {
                    result = widening_sub(narrow_a, narrow_b);
                }
                if (result.type() != op->type) {
                    result = Cast::make(op->type, result);
                }
                return mutate(result);
            }
        }

        Expr negative_b = lossless_negate(b);
        if (negative_b.defined()) {
            return Add::make(a, negative_b);
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Sub::make(a, b);
        }
    }

    Expr visit(const Mul *op) override {
        if (as_const_int(op->b) || as_const_uint(op->b)) {
            // Distribute constants through add/sub. Do this before we muck everything up with widening
            // intrinsics.
            // TODO: Only do this for widening?
            // TODO: Try to do this with IRMatcher::rewriter. The challenge is managing the narrowing/widening casts,
            // and doing constant folding without the simplifier undoing the work.
            if (const Add *add_a = op->a.as<Add>()) {
                return mutate(Add::make(simplify(Mul::make(add_a->a, op->b)), simplify(Mul::make(add_a->b, op->b))));
            } else if (const Sub *sub_a = op->a.as<Sub>()) {
                return mutate(Sub::make(simplify(Mul::make(sub_a->a, op->b)), simplify(Mul::make(sub_a->b, op->b))));
            }
        }

        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        // Rewrite multiplies to shifts if possible.
        if (op->type.is_int() || op->type.is_uint()) {
            int pow2 = 0;
            if (is_const_power_of_two_integer(a, &pow2)) {
                return mutate(b << cast(UInt(b.type().bits()), pow2));
            } else if (is_const_power_of_two_integer(b, &pow2)) {
                return mutate(a << cast(UInt(a.type().bits()), pow2));
            }
        }

        // We're applying this to float, which seems OK? float16 * float16 -> float32 is a widening multiply?
        Expr narrow_a = strip_widening_cast(a);
        Expr narrow_b = strip_widening_cast(b);

        if (narrow_a.defined() && narrow_b.defined()) {
            Expr result = widening_mul(narrow_a, narrow_b);
            if (result.type() != op->type) {
                result = Cast::make(op->type, result);
            }
            return mutate(result);
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
            auto is_x_same_int = is_int(x, bits);
            auto is_x_same_uint = is_uint(x, bits);
            auto is_x_same_int_or_uint = is_x_same_int || is_x_same_uint;
            // clang-format off
            if (rewrite(max(min(widening_add(x, y), upper), lower), saturating_add(x, y), is_x_same_int_or_uint) ||
                rewrite(max(min(widening_sub(x, y), upper), lower), saturating_sub(x, y), is_x_same_int_or_uint) ||
                rewrite(min(widening_add(x, y), upper), saturating_add(x, y), op->type.is_uint() && is_x_same_uint) ||
                rewrite(max(widening_sub(x, y), lower), saturating_sub(x, y), op->type.is_uint() && is_x_same_uint) ||

                rewrite(intrin(Call::shift_right, widening_add(x, y), 1), halving_add(x, y), is_x_same_int_or_uint) ||
                rewrite(intrin(Call::shift_right, widening_sub(x, y), 1), halving_sub(x, y), is_x_same_int_or_uint) ||

                rewrite(intrin(Call::halving_add, widening_add(x, y), 1), rounding_halving_add(x, y), is_x_same_int_or_uint) ||
                rewrite(intrin(Call::halving_add, widening_add(x, 1), y), rounding_halving_add(x, y), is_x_same_int_or_uint) ||
                rewrite(intrin(Call::halving_add, widening_sub(x, y), 1), rounding_halving_sub(x, y), is_x_same_int) ||
                rewrite(intrin(Call::rounding_shift_right, widening_add(x, y), 1), rounding_halving_add(x, y), is_x_same_int_or_uint) ||
                rewrite(intrin(Call::rounding_shift_right, widening_sub(x, y), 1), rounding_halving_sub(x, y), is_x_same_int) ||

                false) {
                return rewrite.result;
            }
            // clang-format on

            // When the argument is a widened rounding shift, and we know the shift is right
            // by at least one, we don't need the widening.
            auto is_x_wide_int = is_int(x, bits * 2);
            auto is_x_wide_uint = is_uint(x, bits * 2);
            auto is_x_wide_int_or_uint = is_x_wide_int || is_x_wide_uint;
            // clang-format off
            if (rewrite(max(min(rounding_shift_right(x, y), upper), lower), rounding_shift_right(x, y), is_x_wide_int_or_uint && y > 0) ||
                rewrite(max(min(rounding_shift_left(x, y), upper), lower), rounding_shift_left(x, y), is_x_wide_int_or_uint && y < 0) ||
                rewrite(rounding_shift_right(x, y), rounding_shift_right(x, y), is_x_wide_int_or_uint && y > 0) ||
                rewrite(rounding_shift_left(x, y), rounding_shift_left(x, y), is_x_wide_int_or_uint && y < 0) ||

                false) {
                const Call *shift = Call::as_intrinsic(rewrite.result, {Call::rounding_shift_right, Call::rounding_shift_left});
                if (is_widen(shift->args[0])) {
                    Expr arg0 = shift->args[0].as<Cast>()->value;
                    Expr arg1 = simplify(narrow(shift->args[1]));
                    return Call::make(op->type, shift->name, {arg0, arg1}, Call::PureIntrinsic);
                }
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
        Expr mutated = IRMutator::visit(op);
        op = mutated.as<Call>();
        if (!op) {
            return mutated;
        }

        auto rewrite = IRMatcher::rewriter(op, op->type);
        if (rewrite(intrin(Call::abs, widening_sub(x, y)), cast(op->type, intrin(Call::absd, x, y))) ||
            false) {
            return rewrite.result;
        }

        if (no_overflow(op->type)) {
            // clang-format off
            if (rewrite(intrin(Call::halving_add, x + y, 1), rounding_halving_add(x, y)) ||
                rewrite(intrin(Call::halving_add, x, y + 1), rounding_halving_add(x, y)) ||
                rewrite(intrin(Call::halving_add, x + 1, y), rounding_halving_add(x, y)) ||
                rewrite(intrin(Call::halving_add, x - y, 1), rounding_halving_sub(x, y)) ||
                rewrite(intrin(Call::halving_sub, x + 1, y), rounding_halving_sub(x, y)) ||
                rewrite(intrin(Call::shift_right, x + y, 1), halving_add(x, y)) ||
                rewrite(intrin(Call::shift_right, x - y, 1), halving_sub(x, y)) ||
                rewrite(intrin(Call::rounding_shift_right, x + y, 1), rounding_halving_add(x, y)) ||
                rewrite(intrin(Call::rounding_shift_right, x - y, 1), rounding_halving_sub(x, y)) ||
                false) {
                return mutate(rewrite.result);
            }
            // clang-format on
        }

        if (op->is_intrinsic(Call::widening_mul)) {
            internal_assert(op->args.size() == 2);
            Expr narrow_a = strip_widening_cast(op->args[0]);
            Expr narrow_b = strip_widening_cast(op->args[1]);
            if (narrow_a.defined() && narrow_b.defined()) {
                return Cast::make(op->type, widening_mul(narrow_a, narrow_b));
            }
        } else if (op->is_intrinsic(Call::widening_add)) {
            internal_assert(op->args.size() == 2);
            Expr narrow_a = strip_widening_cast(op->args[0]);
            Expr narrow_b = strip_widening_cast(op->args[1]);
            if (narrow_a.defined() && narrow_b.defined()) {
                return Cast::make(op->type, widening_add(narrow_a, narrow_b));
            }
        } else if (op->is_intrinsic(Call::widening_sub)) {
            internal_assert(op->args.size() == 2);
            Expr narrow_a = strip_widening_cast(op->args[0]);
            Expr narrow_b = strip_widening_cast(op->args[1]);
            if (narrow_a.defined() && narrow_b.defined()) {
                return Cast::make(op->type, widening_sub(narrow_a, narrow_b));
            }
        }

        if (op->is_intrinsic(Call::shift_right) || op->is_intrinsic(Call::shift_left)) {
            internal_assert(op->args.size() == 2);

            // Try to turn this into a widening left shift.
            if (op->is_intrinsic(Call::shift_left)) {
                Expr a_narrow = lossless_narrow(op->args[0]);
                if (a_narrow.defined()) {
                    if (const Cast *ca = a_narrow.as<Cast>()) {
                        // If there is more casting, we can move it after the shift.
                        a_narrow = ca->value;
                    }
                    Expr b = op->args[1];
                    b = simplify(cast(a_narrow.type().with_code(b.type().code()), b));
                    Expr result = widening_shift_left(a_narrow, b);
                    if (result.type() != op->type) {
                        result = Cast::make(op->type, result);
                    }
                    return mutate(result);
                }
            }

            // Try to turn this into a rounding shift.
            Expr rounding_shift = to_rounding_shift(op);
            if (rounding_shift.defined()) {
                return mutate(rounding_shift);
            }
        }
        return op;
    }
};

}  // namespace

Stmt pattern_match_intrinsics(Stmt s) {
    s = substitute_in_all_lets(s);
    s = PatternMatchIntrinsics().mutate(s);
    s = common_subexpression_elimination(s);
    return s;
}

Expr pattern_match_intrinsics(Expr e) {
    e = substitute_in_all_lets(e);
    e = PatternMatchIntrinsics().mutate(e);
    e = common_subexpression_elimination(e);
    return e;
}

Expr lower_widening_add(const Expr &a, const Expr &b) {
    return widen(a) + widen(b);
}

Expr lower_widening_mul(const Expr &a, const Expr &b) {
    return widen(a) * widen(b);
}

Expr lower_widening_sub(const Expr &a, const Expr &b) {
    Type wide = a.type().widen();
    if (wide.is_uint()) {
        wide = wide.with_code(halide_type_int);
    }
    return cast(wide, a) - cast(wide, b);
}

Expr lower_widening_shift_left(const Expr &a, const Expr &b) {
    const uint64_t *const_shift = as_const_uint(b);
    if (const_shift && (a.type().is_vector() || b.type().is_vector())) {
        Expr const_b = lossless_cast(a.type(), make_const(UInt(64), 1LL << *const_shift));
        if (const_b.defined()) {
            // Most backends would probably rather see a widening multiply by a constant (1-2 vector-instructions)
            // than a widen (1-2 vector-instructions) followed by a shift (2 vector-instructions).
            return widening_mul(a, const_b);
        }
    }
    return widen(a) << b;
}

Expr lower_rounding_shift_right(const Expr &a, const Expr &b) {
    Expr round = make_shift_right(make_one(a.type()) << max(b, 0), 1);
    Expr a_rounded = simplify(saturating_add(a, round));
    return a_rounded >> b;
}

Expr lower_rounding_shift_left(const Expr &a, const Expr &b) {
    Expr round = make_shift_right(make_one(a.type()) >> min(b, 0), 1);
    Expr a_rounded = simplify(saturating_add(a, round));
    return a_rounded << b;
}

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
    return Cast::make(a.type(), make_shift_right(result_2x, 1));
}

Expr lower_halving_sub(const Expr &a, const Expr &b) {
    internal_assert(a.type() == b.type());
    Expr result_2x = widening_sub(a, b);
    return Cast::make(a.type(), make_shift_right(result_2x, 1));
}

// TODO: These should using rounding_shift_right, but lowering that
// results in double widening and the simplifier doesn't fix it.
Expr lower_rounding_halving_add(const Expr &a, const Expr &b) {
    internal_assert(a.type() == b.type());
    Expr result_2x = widening_add(a, b) + 1;
    return Cast::make(a.type(), make_shift_right(result_2x, 1));
}

Expr lower_rounding_halving_sub(const Expr &a, const Expr &b) {
    internal_assert(a.type() == b.type());
    Expr result_2x = widening_sub(a, b) + 1;
    return Cast::make(a.type(), make_shift_right(result_2x, 1));
}

Expr lower_intrinsic(const Call *op) {
    if (op->is_intrinsic(Call::widening_add)) {
        internal_assert(op->args.size() == 2);
        return lower_widening_add(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::widening_mul)) {
        internal_assert(op->args.size() == 2);
        return lower_widening_mul(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::widening_sub)) {
        internal_assert(op->args.size() == 2);
        return lower_widening_sub(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::widening_shift_left)) {
        internal_assert(op->args.size() == 2);
        return lower_widening_shift_left(op->args[0], op->args[1]);
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
    } else {
        return Expr();
    }
}

}  // namespace Internal
}  // namespace Halide
