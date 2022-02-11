#include "FindIntrinsics.h"
#include "CSE.h"
#include "CodeGen_Internal.h"
#include "ConciseCasts.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

using namespace Halide::ConciseCasts;

namespace {

bool find_intrinsics_for_type(const Type &t) {
    // Currently, we only try to find and replace intrinsics for vector types that aren't bools.
    return t.is_vector() && t.bits() >= 8;
}

Expr widen(Expr a) {
    Type result_type = a.type().widen();
    return Cast::make(result_type, std::move(a));
}

Expr narrow(Expr a) {
    Type result_type = a.type().narrow();
    return Cast::make(result_type, std::move(a));
}

Expr lossless_narrow(const Expr &x) {
    return lossless_cast(x.type().narrow(), x);
}

// Remove a widening cast even if it changes the sign of the result.
Expr strip_widening_cast(const Expr &x) {
    Expr narrow = lossless_narrow(x);
    if (narrow.defined()) {
        return narrow;
    }
    return lossless_cast(x.type().narrow().with_code(halide_type_uint), x);
}

Expr saturating_narrow(const Expr &a) {
    Type narrow = a.type().narrow();
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

// If there's a widening add or subtract in the first e.type().bits() / 2 - 1
// levels down a tree of adds or subtracts, we know there's enough headroom for
// another add without overflow. For example, it is safe to add to
// (widening_add(x, y) - z) without overflow.
bool is_safe_for_add(const Expr &e, int max_depth) {
    if (max_depth-- <= 0) {
        return false;
    }
    if (const Add *add = e.as<Add>()) {
        return is_safe_for_add(add->a, max_depth) || is_safe_for_add(add->b, max_depth);
    } else if (const Sub *sub = e.as<Sub>()) {
        return is_safe_for_add(sub->a, max_depth) || is_safe_for_add(sub->b, max_depth);
    } else if (const Cast *cast = e.as<Cast>()) {
        if (cast->type.bits() > cast->value.type().bits()) {
            return true;
        } else if (cast->type.bits() == cast->value.type().bits()) {
            return is_safe_for_add(cast->value, max_depth);
        }
    } else if (Call::as_intrinsic(e, {Call::widening_add, Call::widening_sub})) {
        return true;
    }
    return false;
}

bool is_safe_for_add(const Expr &e) {
    return is_safe_for_add(e, e.type().bits() / 2 - 1);
}

// We want to find and remove an add of 'round' from e. This is not
// the same thing as just subtracting round, we specifically want
// to remove an addition of exactly round.
Expr find_and_subtract(const Expr &e, const Expr &round) {
    if (const Add *add = e.as<Add>()) {
        Expr a = find_and_subtract(add->a, round);
        if (a.defined()) {
            return Add::make(a, add->b);
        }
        Expr b = find_and_subtract(add->b, round);
        if (b.defined()) {
            return Add::make(add->a, b);
        }
    } else if (const Sub *sub = e.as<Sub>()) {
        Expr a = find_and_subtract(sub->a, round);
        if (a.defined()) {
            return Sub::make(a, sub->b);
        }
        // We can't recurse into the negatve part of a subtract.
    } else if (can_prove(e == round)) {
        return make_zero(e.type());
    }
    return Expr();
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
        Type round_type = a.type().with_lanes(1);
        if (Call::as_intrinsic(a, {Call::widening_add})) {
            round_type = round_type.narrow();
        }
        Expr round;
        if (c->is_intrinsic(Call::shift_right)) {
            round = simplify((make_one(round_type) << max(cast(b.type().with_bits(round_type.bits()), b), 0)) / 2);
        } else {
            round = simplify((make_one(round_type) >> min(cast(b.type().with_bits(round_type.bits()), b), 0)) / 2);
        }

        // We can always handle widening adds.
        if (const Call *add = Call::as_intrinsic(a, {Call::widening_add})) {
            if (can_prove(lower_intrinsics(add->args[0]) == round)) {
                return rounding_shift(cast(add->type, add->args[1]), b);
            } else if (can_prove(lower_intrinsics(add->args[1]) == round)) {
                return rounding_shift(cast(add->type, add->args[0]), b);
            }
        }

        // If it wasn't a widening or saturating add, we might still
        // be able to safely accept the rounding.
        Expr a_less_round = find_and_subtract(a, round);
        if (a_less_round.defined()) {
            // We found and removed the rounding. However, we may have just changed
            // behavior due to overflow. This is still safe if the type is not
            // overflowing, or we can find a widening add or subtract in the tree
            // of adds/subtracts. This is a common pattern, e.g.
            // rounding_halving_add(a, b) = shift_round(widening_add(a, b) + 1, 1).
            // TODO: This could be done with bounds inference instead of this hack
            // if it supported intrinsics like widening_add and tracked bounds for
            // types other than int32.
            if (no_overflow(a.type()) || is_safe_for_add(a_less_round)) {
                return rounding_shift(simplify(a_less_round), b);
            }
        }
    }

    return Expr();
}

class FindIntrinsics : public IRMutator {
protected:
    using IRMutator::visit;

    IRMatcher::Wild<0> x;
    IRMatcher::Wild<1> y;
    IRMatcher::Wild<2> z;
    IRMatcher::WildConst<0> c0;
    IRMatcher::WildConst<1> c1;

    Expr visit(const Add *op) override {
        if (!find_intrinsics_for_type(op->type)) {
            return IRMutator::visit(op);
        }

        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        // Try widening both from the same signedness as the result, and from uint.
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
        if (!find_intrinsics_for_type(op->type)) {
            return IRMutator::visit(op);
        }

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
        if (!find_intrinsics_for_type(op->type)) {
            return IRMutator::visit(op);
        }

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
        // This uses strip_widening_cast to ignore the signedness of the narrow value.
        Expr narrow_a = strip_widening_cast(a);
        Expr narrow_b = strip_widening_cast(b);
        if (narrow_a.defined() && narrow_b.defined() &&
            (narrow_a.type().is_int_or_uint() == narrow_b.type().is_int_or_uint() ||
             narrow_a.type().is_float() == narrow_b.type().is_float())) {
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
        if (!find_intrinsics_for_type(op->type)) {
            return IRMutator::visit(op);
        }

        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        int shift_amount;
        if (is_const_power_of_two_integer(b, &shift_amount) && op->type.is_int_or_uint()) {
            return mutate(a >> make_const(UInt(a.type().bits()), shift_amount));
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Div::make(a, b);
        }
    }

    // We don't handle Mod because we don't have any patterns that look for bitwise and vs.
    // mod.

    template<class MinOrMax>
    Expr visit_min_or_max(const MinOrMax *op) {
        if (!find_intrinsics_for_type(op->type)) {
            return IRMutator::visit(op);
        }

        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        if (const Cast *cast_a = a.as<Cast>()) {
            Expr cast_b = lossless_cast(cast_a->value.type(), b);
            if (cast_a->type.can_represent(cast_a->value.type()) && cast_b.defined()) {
                // This is a widening cast that can be moved outside the min.
                return mutate(Cast::make(cast_a->type, MinOrMax::make(cast_a->value, cast_b)));
            }
        }
        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return MinOrMax::make(a, b);
        }
    }

    Expr visit(const Min *op) override {
        return visit_min_or_max(op);
    }

    Expr visit(const Max *op) override {
        return visit_min_or_max(op);
    }

    Expr visit(const Cast *op) override {
        if (!find_intrinsics_for_type(op->type)) {
            return IRMutator::visit(op);
        }

        Expr value = mutate(op->value);

        // This mutator can generate redundant casts. We can't use the simplifier because it
        // undoes some of the intrinsic lowering here, and it causes some problems due to
        // factoring (instead of distributing) constants.
        if (const Cast *cast = value.as<Cast>()) {
            if (cast->type.can_represent(cast->value.type()) || cast->type.can_represent(op->type)) {
                // The intermediate cast is redundant.
                value = cast->value;
            }
        }

        if (op->type.is_int() || op->type.is_uint()) {
            Expr lower = cast(value.type(), op->type.min());
            Expr upper = cast(value.type(), op->type.max());

            auto rewrite = IRMatcher::rewriter(value, op->type);

            Type op_type_wide = op->type.widen();
            Type signed_type_wide = op_type_wide.with_code(halide_type_int);
            Type unsigned_type = op->type.with_code(halide_type_uint);

            // Give concise names to various predicates we want to use in
            // rewrite rules below.
            int bits = op->type.bits();
            auto is_x_same_int = op->type.is_int() && is_int(x, bits);
            auto is_x_same_uint = op->type.is_uint() && is_uint(x, bits);
            auto is_x_same_int_or_uint = is_x_same_int || is_x_same_uint;
            auto x_y_same_sign = (is_int(x) && is_int(y)) || (is_uint(x) && is_uint(y));
            auto is_y_narrow_uint = op->type.is_uint() && is_uint(y, bits / 2);
            if (
                // Saturating patterns
                rewrite(max(min(widening_add(x, y), upper), lower),
                        saturating_add(x, y),
                        is_x_same_int_or_uint) ||

                rewrite(max(min(widening_sub(x, y), upper), lower),
                        saturating_sub(x, y),
                        is_x_same_int_or_uint) ||

                rewrite(min(cast(signed_type_wide, widening_add(x, y)), upper),
                        saturating_add(x, y),
                        is_x_same_uint) ||

                rewrite(min(widening_add(x, y), upper),
                        saturating_add(x, y),
                        op->type.is_uint() && is_x_same_uint) ||

                rewrite(max(widening_sub(x, y), lower),
                        saturating_sub(x, y),
                        op->type.is_uint() && is_x_same_uint) ||

                // Averaging patterns
                //
                // We have a slight preference for rounding_halving_add over
                // using halving_add when unsigned, because x86 supports it.

                rewrite(shift_right(widening_add(x, c0), 1),
                        rounding_halving_add(x, c0 - 1),
                        c0 > 0 && is_x_same_uint) ||

                rewrite(shift_right(widening_add(x, y), 1),
                        halving_add(x, y),
                        is_x_same_int_or_uint) ||

                rewrite(shift_right(widening_add(x, c0), c1),
                        rounding_shift_right(x, cast(op->type, c1)),
                        c0 == shift_left(1, c1 - 1) && is_x_same_int_or_uint) ||

                rewrite(shift_right(widening_add(x, c0), c1),
                        shift_right(rounding_halving_add(x, cast(op->type, fold(c0 - 1))), cast(op->type, fold(c1 - 1))),
                        c0 > 0 && c1 > 0 && is_x_same_uint) ||

                rewrite(shift_right(widening_add(x, y), c0),
                        shift_right(halving_add(x, y), cast(op->type, fold(c0 - 1))),
                        c0 > 0 && is_x_same_int_or_uint) ||

                rewrite(shift_right(widening_sub(x, y), 1),
                        halving_sub(x, y),
                        is_x_same_int_or_uint) ||

                rewrite(halving_add(widening_add(x, y), 1),
                        rounding_halving_add(x, y),
                        is_x_same_int_or_uint) ||

                rewrite(halving_add(widening_add(x, 1), y),
                        rounding_halving_add(x, y),
                        is_x_same_int_or_uint) ||

                rewrite(halving_add(widening_sub(x, y), 1),
                        rounding_halving_sub(x, y),
                        is_x_same_int_or_uint) ||

                rewrite(rounding_shift_right(widening_add(x, y), 1),
                        rounding_halving_add(x, y),
                        is_x_same_int_or_uint) ||

                rewrite(rounding_shift_right(widening_sub(x, y), 1),
                        rounding_halving_sub(x, y),
                        is_x_same_int_or_uint) ||

                // Multiply-keep-high-bits patterns.
                rewrite(max(min(shift_right(widening_mul(x, y), z), upper), lower),
                        mul_shift_right(x, y, cast(unsigned_type, z)),
                        is_x_same_int_or_uint && x_y_same_sign && is_uint(z)) ||

                rewrite(max(min(rounding_shift_right(widening_mul(x, y), z), upper), lower),
                        rounding_mul_shift_right(x, y, cast(unsigned_type, z)),
                        is_x_same_int_or_uint && x_y_same_sign && is_uint(z)) ||

                rewrite(min(shift_right(widening_mul(x, y), z), upper),
                        mul_shift_right(x, y, cast(unsigned_type, z)),
                        is_x_same_uint && x_y_same_sign && is_uint(z)) ||

                rewrite(min(rounding_shift_right(widening_mul(x, y), z), upper),
                        rounding_mul_shift_right(x, y, cast(unsigned_type, z)),
                        is_x_same_uint && x_y_same_sign && is_uint(z)) ||

                // We don't need saturation for the full upper half of a multiply.
                // For signed integers, this is almost true, except for when x and y
                // are both the most negative value. For these, we only need saturation
                // at the upper bound.

                rewrite(min(shift_right(widening_mul(x, y), c0), upper),
                        mul_shift_right(x, y, cast(unsigned_type, c0)),
                        is_x_same_int && x_y_same_sign && c0 >= bits - 1) ||

                rewrite(min(rounding_shift_right(widening_mul(x, y), c0), upper),
                        rounding_mul_shift_right(x, y, cast(unsigned_type, c0)),
                        is_x_same_int && x_y_same_sign && c0 >= bits - 1) ||

                rewrite(shift_right(widening_mul(x, y), c0),
                        mul_shift_right(x, y, cast(unsigned_type, c0)),
                        is_x_same_int_or_uint && x_y_same_sign && c0 >= bits) ||

                rewrite(rounding_shift_right(widening_mul(x, y), c0),
                        rounding_mul_shift_right(x, y, cast(unsigned_type, c0)),
                        is_x_same_int_or_uint && x_y_same_sign && c0 >= bits) ||

                // We can also match on smaller shifts if one of the args is
                // narrow. We don't do this for signed (yet), because the
                // saturation issue is tricky.
                rewrite(shift_right(widening_mul(x, cast(op->type, y)), c0),
                        mul_shift_right(x, cast(op->type, y), cast(unsigned_type, c0)),
                        is_x_same_int_or_uint && is_y_narrow_uint && c0 >= bits / 2) ||

                rewrite(rounding_shift_right(widening_mul(x, cast(op->type, y)), c0),
                        rounding_mul_shift_right(x, cast(op->type, y), cast(unsigned_type, c0)),
                        is_x_same_int_or_uint && is_y_narrow_uint && c0 >= bits / 2) ||

                rewrite(shift_right(widening_mul(cast(op->type, y), x), c0),
                        mul_shift_right(cast(op->type, y), x, cast(unsigned_type, c0)),
                        is_x_same_int_or_uint && is_y_narrow_uint && c0 >= bits / 2) ||

                rewrite(rounding_shift_right(widening_mul(cast(op->type, y), x), c0),
                        rounding_mul_shift_right(cast(op->type, y), x, cast(unsigned_type, c0)),
                        is_x_same_int_or_uint && is_y_narrow_uint && c0 >= bits / 2) ||

                // Halving subtract patterns
                rewrite(shift_right(cast(op_type_wide, widening_sub(x, y)), 1),
                        halving_sub(x, y),
                        is_x_same_int_or_uint) ||

                rewrite(rounding_shift_right(cast(op_type_wide, widening_sub(x, y)), 1),
                        rounding_halving_sub(x, y),
                        is_x_same_int_or_uint) ||

                false) {
                internal_assert(rewrite.result.type() == op->type)
                    << "Rewrite changed type: " << Expr(op) << " -> " << rewrite.result << "\n";
                return mutate(rewrite.result);
            }

            // When the argument is a widened rounding shift, we might not need the widening.
            // When there is saturation, we can only avoid the widening if we know the shift is
            // a right shift. Without saturation, we can ignore the widening.
            auto is_x_wide_int = op->type.is_int() && is_int(x, bits * 2);
            auto is_x_wide_uint = op->type.is_uint() && is_uint(x, bits * 2);
            auto is_x_wide_int_or_uint = is_x_wide_int || is_x_wide_uint;
            // We can't do everything we want here with rewrite rules alone. So, we rewrite them
            // to rounding_shifts with the widening still in place, and narrow it after the rewrite
            // succeeds.
            // clang-format off
            if (rewrite(max(min(rounding_shift_right(x, y), upper), lower), rounding_shift_right(x, y), is_x_wide_int_or_uint) ||
                rewrite(rounding_shift_right(x, y), rounding_shift_right(x, y), is_x_wide_int_or_uint) ||
                rewrite(rounding_shift_left(x, y), rounding_shift_left(x, y), is_x_wide_int_or_uint) ||
                false) {
                const Call *shift = Call::as_intrinsic(rewrite.result, {Call::rounding_shift_right, Call::rounding_shift_left});
                internal_assert(shift);
                bool is_saturated = op->value.as<Max>() || op->value.as<Min>();
                Expr a = lossless_cast(op->type, shift->args[0]);
                Expr b = lossless_cast(op->type.with_code(shift->args[1].type().code()), shift->args[1]);
                if (a.defined() && b.defined()) {
                    if (!is_saturated ||
                        (shift->is_intrinsic(Call::rounding_shift_right) && can_prove(b >= 0)) ||
                        (shift->is_intrinsic(Call::rounding_shift_left) && can_prove(b <= 0))) {
                        return mutate(Call::make(op->type, shift->name, {a, b}, Call::PureIntrinsic));
                    }
                }
            }
            // clang-format on
        }

        if (value.same_as(op->value)) {
            return op;
        } else if (op->type != value.type()) {
            return Cast::make(op->type, value);
        } else {
            return value;
        }
    }

    Expr visit(const Call *op) override {
        if (!find_intrinsics_for_type(op->type)) {
            return IRMutator::visit(op);
        }

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
            if (rewrite(halving_add(x + y, 1), rounding_halving_add(x, y)) ||
                rewrite(halving_add(x, y + 1), rounding_halving_add(x, y)) ||
                rewrite(halving_add(x + 1, y), rounding_halving_add(x, y)) ||
                rewrite(halving_add(x - y, 1), rounding_halving_sub(x, y)) ||
                rewrite(halving_sub(x + 1, y), rounding_halving_sub(x, y)) ||
                rewrite(halving_add(x, 1), rounding_shift_right(x, 1)) ||
                rewrite(shift_right(x + y, 1), halving_add(x, y)) ||
                rewrite(shift_right(x - y, 1), halving_sub(x, y)) ||
                rewrite(rounding_shift_right(x + y, 1), rounding_halving_add(x, y)) ||
                rewrite(rounding_shift_right(x - y, 1), rounding_halving_sub(x, y)) ||
                false) {
                return mutate(rewrite.result);
            }
            // clang-format on
        }

        // Move widening casts inside widening arithmetic outside the arithmetic,
        // e.g. widening_mul(widen(u8), widen(i8)) -> widen(widening_mul(u8, i8)).
        if (op->is_intrinsic(Call::widening_mul)) {
            internal_assert(op->args.size() == 2);
            Expr narrow_a = strip_widening_cast(op->args[0]);
            Expr narrow_b = strip_widening_cast(op->args[1]);
            if (narrow_a.defined() && narrow_b.defined()) {
                return mutate(Cast::make(op->type, widening_mul(narrow_a, narrow_b)));
            }
        } else if (op->is_intrinsic(Call::widening_add)) {
            internal_assert(op->args.size() == 2);
            for (halide_type_code_t t : {op->type.code(), halide_type_uint}) {
                Type narrow_t = op->type.narrow().narrow().with_code(t);
                Expr narrow_a = lossless_cast(narrow_t, op->args[0]);
                Expr narrow_b = lossless_cast(narrow_t, op->args[1]);
                if (narrow_a.defined() && narrow_b.defined()) {
                    return mutate(Cast::make(op->type, widening_add(narrow_a, narrow_b)));
                }
            }
        } else if (op->is_intrinsic(Call::widening_sub)) {
            internal_assert(op->args.size() == 2);
            for (halide_type_code_t t : {op->type.code(), halide_type_uint}) {
                Type narrow_t = op->type.narrow().narrow().with_code(t);
                Expr narrow_a = lossless_cast(narrow_t, op->args[0]);
                Expr narrow_b = lossless_cast(narrow_t, op->args[1]);
                if (narrow_a.defined() && narrow_b.defined()) {
                    return mutate(Cast::make(op->type, widening_sub(narrow_a, narrow_b)));
                }
            }
        }

        if (op->is_intrinsic(Call::shift_right) || op->is_intrinsic(Call::shift_left)) {
            // Try to turn this into a widening shift.
            internal_assert(op->args.size() == 2);
            Expr a_narrow = lossless_narrow(op->args[0]);
            Expr b_narrow = lossless_narrow(op->args[1]);
            if (a_narrow.defined() && b_narrow.defined()) {
                Expr result = op->is_intrinsic(Call::shift_left) ? widening_shift_left(a_narrow, b_narrow) : widening_shift_right(a_narrow, b_narrow);
                if (result.type() != op->type) {
                    result = Cast::make(op->type, result);
                }
                return mutate(result);
            }

            // Try to turn this into a rounding shift.
            Expr rounding_shift = to_rounding_shift(op);
            if (rounding_shift.defined()) {
                return mutate(rounding_shift);
            }
        }

        if (op->is_intrinsic(Call::rounding_shift_left) || op->is_intrinsic(Call::rounding_shift_right)) {
            // Try to turn this into a widening shift.
            internal_assert(op->args.size() == 2);
            Expr a_narrow = lossless_narrow(op->args[0]);
            Expr b_narrow = lossless_narrow(op->args[1]);
            if (a_narrow.defined() && b_narrow.defined()) {
                Expr result;
                if (op->is_intrinsic(Call::rounding_shift_right) && can_prove(b_narrow > 0)) {
                    result = rounding_shift_right(a_narrow, b_narrow);
                } else if (op->is_intrinsic(Call::rounding_shift_left) && can_prove(b_narrow < 0)) {
                    result = rounding_shift_left(a_narrow, b_narrow);
                } else {
                    return op;
                }
                if (result.type() != op->type) {
                    result = Cast::make(op->type, result);
                }
                return mutate(result);
            }
        }
        return op;
    }
};

// Substitute in let values than have an output vector
// type wider than all the types of other variables
// referenced. This can't cause combinatorial explosion,
// because each let in a chain has a wider value than the
// ones it refers to.
class SubstituteInWideningLets : public IRMutator {
    using IRMutator::visit;

    bool widens(const Expr &e) {
        class AllInputsNarrowerThan : public IRVisitor {
            int bits;

            using IRVisitor::visit;

            void visit(const Variable *op) override {
                result &= op->type.bits() < bits;
            }

            void visit(const Load *op) override {
                result &= op->type.bits() < bits;
            }

            void visit(const Call *op) override {
                if (op->is_pure() && op->is_intrinsic()) {
                    IRVisitor::visit(op);
                } else {
                    result &= op->type.bits() < bits;
                }
            }

        public:
            AllInputsNarrowerThan(Type t)
                : bits(t.bits()) {
            }
            bool result = true;
        } widens(e.type());
        e.accept(&widens);
        return widens.result;
    }

    Scope<Expr> replacements;
    Expr visit(const Variable *op) override {
        if (replacements.contains(op->name)) {
            return replacements.get(op->name);
        } else {
            return op;
        }
    }

    template<typename T>
    auto visit_let(const T *op) -> decltype(op->body) {
        struct Frame {
            std::string name;
            Expr new_value;
            ScopedBinding<Expr> bind;
            Frame(const std::string &name, const Expr &new_value, ScopedBinding<Expr> &&bind)
                : name(name), new_value(new_value), bind(std::move(bind)) {
            }
        };
        std::vector<Frame> frames;
        decltype(op->body) body;
        do {
            body = op->body;
            Expr value = op->value;
            bool should_replace = find_intrinsics_for_type(value.type()) && widens(value);

            // We can only substitute in pure stuff. Isolate all
            // impure subexpressions and leave them behind here as
            // lets.
            class LeaveBehindSubexpressions : public IRMutator {
                using IRMutator::visit;

                Expr visit(const Call *op) override {
                    if (!op->is_pure() || !op->is_intrinsic()) {
                        // Only enter pure intrinsics (e.g. existing uses of widening_add)
                        std::string name = unique_name('t');
                        frames.emplace_back(name, op, ScopedBinding<Expr>{});
                        return Variable::make(op->type, name);
                    } else {
                        return IRMutator::visit(op);
                    }
                }

                Expr visit(const Load *op) override {
                    // Never enter loads. They can be impure and none
                    // of our patterns match them.
                    std::string name = unique_name('t');
                    frames.emplace_back(name, op, ScopedBinding<Expr>{});
                    return Variable::make(op->type, name);
                }

                std::vector<Frame> &frames;

            public:
                LeaveBehindSubexpressions(std::vector<Frame> &frames)
                    : frames(frames) {
                }
            } extractor(frames);

            if (should_replace) {
                size_t start_of_new_lets = frames.size();
                value = extractor.mutate(value);
                // Mutate any subexpressions the extractor decided to
                // leave behind, in case they in turn depend on lets
                // we've decided to substitute in.
                for (size_t i = start_of_new_lets; i < frames.size(); i++) {
                    frames[i].new_value = mutate(frames[i].new_value);
                }

                // Check it wasn't lifted entirely
                should_replace = !value.as<Variable>();
            }

            // TODO: If it's an int32/64 vector, it may be
            // implicitly widening because overflow is UB. Hard to
            // see how to handle this without worrying about
            // combinatorial explosion of substitutions.
            value = mutate(value);
            ScopedBinding<Expr> bind(should_replace, replacements, op->name, value);
            frames.emplace_back(op->name, value, std::move(bind));
            op = body.template as<T>();
        } while (op);

        body = mutate(body);

        while (!frames.empty()) {
            if (!frames.back().bind.bound()) {
                body = T::make(frames.back().name, frames.back().new_value, body);
            }
            frames.pop_back();
        }

        return body;
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }
};

}  // namespace

Stmt find_intrinsics(const Stmt &s) {
    Stmt stmt = SubstituteInWideningLets().mutate(s);
    stmt = FindIntrinsics().mutate(stmt);
    // In case we want to hoist widening ops back out
    stmt = common_subexpression_elimination(stmt);
    return stmt;
}

Expr find_intrinsics(const Expr &e) {
    Expr expr = SubstituteInWideningLets().mutate(e);
    expr = FindIntrinsics().mutate(expr);
    expr = common_subexpression_elimination(expr);
    return expr;
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
    return Cast::make(wide, a) - Cast::make(wide, b);
}

Expr lower_widening_shift_left(const Expr &a, const Expr &b) {
    return widen(a) << b;
}

Expr lower_widening_shift_right(const Expr &a, const Expr &b) {
    return widen(a) >> b;
}

Expr lower_rounding_shift_left(const Expr &a, const Expr &b) {
    // Shift left, then add one to the result if bits were dropped
    // (because b < 0) and the most significant dropped bit was a one.
    Expr b_negative = select(b < 0, make_one(a.type()), make_zero(a.type()));
    return simplify((a << b) + (b_negative & (a << (b + 1))));
}

Expr lower_rounding_shift_right(const Expr &a, const Expr &b) {
    if (is_positive_const(b)) {
        // We can handle the rounding with an averaging instruction. We prefer
        // the rounding average instruction (we could use either), because the
        // non-rounding one is missing on x86.
        Expr shift = simplify(b - 1);
        Expr round = simplify(cast(a.type(), (1 << shift) - 1));
        return rounding_halving_add(a, round) >> shift;
    }
    // Shift right, then add one to the result if bits were dropped
    // (because b > 0) and the most significant dropped bit was a one.
    Expr b_positive = select(b > 0, make_one(a.type()), make_zero(a.type()));
    return simplify((a >> b) + (b_positive & (a >> (b - 1))));
}

Expr lower_saturating_add(const Expr &a, const Expr &b) {
    internal_assert(a.type() == b.type());
    // Lower saturating add without using widening arithmetic, which may require
    // types that aren't supported.
    return simplify(clamp(a, a.type().min() - min(b, 0), a.type().max() - max(b, 0))) + b;
}

Expr lower_saturating_sub(const Expr &a, const Expr &b) {
    internal_assert(a.type() == b.type());
    // Lower saturating add without using widening arithmetic, which may require
    // types that aren't supported.
    return simplify(clamp(a, a.type().min() + max(b, 0), a.type().max() + min(b, 0))) - b;
}

Expr lower_halving_add(const Expr &a, const Expr &b) {
    internal_assert(a.type() == b.type());
    // Borrowed from http://aggregate.org/MAGIC/#Average%20of%20Integers
    return (a & b) + ((a ^ b) >> 1);
}

Expr lower_halving_sub(const Expr &a, const Expr &b) {
    internal_assert(a.type() == b.type());
    return (a >> 1) - (b >> 1) - (((b & 1) - (a & 1) + 1) >> 1);
}

// TODO: These should using rounding_shift_right, but lowering that
// results in double widening and the simplifier doesn't fix it.
Expr lower_rounding_halving_add(const Expr &a, const Expr &b) {
    internal_assert(a.type() == b.type());
    return (a >> 1) + (b >> 1) + (((a & 1) + (b & 1) + 1) >> 1);
}

Expr lower_rounding_halving_sub(const Expr &a, const Expr &b) {
    internal_assert(a.type() == b.type());
    return (a >> 1) - (b >> 1) + (((a & 1) - (b & 1) + 1) >> 1);
}

Expr lower_sorted_avg(const Expr &a, const Expr &b) {
    // b > a, so the following works without widening.
    return a + ((b - a) >> 1);
}

Expr lower_mul_shift_right(const Expr &a, const Expr &b, const Expr &q) {
    internal_assert(a.type() == b.type());
    int full_q = a.type().bits();
    if (a.type().is_int()) {
        full_q -= 1;
    }
    if (can_prove(q < full_q)) {
        // Try to rewrite this to a "full precision" multiply by multiplying
        // one of the operands and the denominator by a constant. We only do this
        // if it isn't already full precision. This avoids infinite loops despite
        // "lowering" this to another mul_shift_right operation.
        Expr missing_q = full_q - q;
        internal_assert(missing_q.type().bits() == b.type().bits());
        Expr new_b = simplify(b << missing_q);
        if (is_const(new_b) && can_prove(new_b >> missing_q == b)) {
            return mul_shift_right(a, new_b, full_q);
        }
        Expr new_a = simplify(a << missing_q);
        if (is_const(new_a) && can_prove(new_a >> missing_q == a)) {
            return mul_shift_right(new_a, b, full_q);
        }
    }

    if (can_prove(q > a.type().bits())) {
        // If q is bigger than the narrow type, write it as an exact upper
        // half multiply, followed by an extra shift.
        Expr result = mul_shift_right(a, b, a.type().bits());
        result = result >> simplify(q - a.type().bits());
        return result;
    }

    // If all else fails, just widen, shift, and narrow.
    Expr result = widening_mul(a, b) >> q;
    if (!can_prove(q >= a.type().bits())) {
        result = saturating_narrow(result);
    } else {
        result = narrow(result);
    }
    return result;
}

Expr lower_rounding_mul_shift_right(const Expr &a, const Expr &b, const Expr &q) {
    internal_assert(a.type() == b.type());
    int full_q = a.type().bits();
    if (a.type().is_int()) {
        full_q -= 1;
    }
    // Try to rewrite this to a "full precision" multiply by multiplying
    // one of the operands and the denominator by a constant. We only do this
    // if it isn't already full precision. This avoids infinite loops despite
    // "lowering" this to another mul_shift_right operation.
    if (can_prove(q < full_q)) {
        Expr missing_q = full_q - q;
        internal_assert(missing_q.type().bits() == b.type().bits());
        Expr new_b = simplify(b << missing_q);
        if (is_const(new_b) && can_prove(new_b >> missing_q == b)) {
            return rounding_mul_shift_right(a, new_b, full_q);
        }
        Expr new_a = simplify(a << missing_q);
        if (is_const(new_a) && can_prove(new_a >> missing_q == a)) {
            return rounding_mul_shift_right(new_a, b, full_q);
        }
    }

    // If all else fails, just widen, shift, and narrow.
    Expr result = rounding_shift_right(widening_mul(a, b), q);
    if (!can_prove(q >= a.type().bits())) {
        result = saturating_narrow(result);
    } else {
        result = narrow(result);
    }
    return result;
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
    } else if (op->is_intrinsic(Call::saturating_add)) {
        internal_assert(op->args.size() == 2);
        return lower_saturating_add(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::saturating_sub)) {
        internal_assert(op->args.size() == 2);
        return lower_saturating_sub(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::widening_shift_left)) {
        internal_assert(op->args.size() == 2);
        return lower_widening_shift_left(op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::widening_shift_right)) {
        internal_assert(op->args.size() == 2);
        return lower_widening_shift_right(op->args[0], op->args[1]);
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
    } else if (op->is_intrinsic(Call::rounding_mul_shift_right)) {
        internal_assert(op->args.size() == 3);
        return lower_rounding_mul_shift_right(op->args[0], op->args[1], op->args[2]);
    } else if (op->is_intrinsic(Call::mul_shift_right)) {
        internal_assert(op->args.size() == 3);
        return lower_mul_shift_right(op->args[0], op->args[1], op->args[2]);
    } else if (op->is_intrinsic(Call::sorted_avg)) {
        internal_assert(op->args.size() == 2);
        return lower_sorted_avg(op->args[0], op->args[1]);
    } else {
        return Expr();
    }
}

namespace {

class LowerIntrinsics : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Call *op) override {
        Expr lowered = lower_intrinsic(op);
        if (lowered.defined()) {
            return mutate(lowered);
        }
        return IRMutator::visit(op);
    }
};

}  // namespace

Expr lower_intrinsics(const Expr &e) {
    return LowerIntrinsics().mutate(e);
}

Stmt lower_intrinsics(const Stmt &s) {
    return LowerIntrinsics().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
