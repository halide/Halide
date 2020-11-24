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

Expr widen(Expr a) {
    return Cast::make(a.type().with_bits(a.type().bits() * 2), std::move(a));
}

Expr narrow(Expr a) {
    return Cast::make(a.type().with_bits(a.type().bits() / 2), std::move(a));
}

Expr saturating_narrow(Expr a) {
    Type narrow = a.type().with_bits(a.type().bits() / 2);
    return saturating_cast(narrow, a);
}

struct Pattern {
    Expr pattern;
    Call::IntrinsicOp replacement;
};

Expr apply_patterns(Type type, Expr x, const std::vector<Pattern> &patterns) {
    std::vector<Expr> matches;
    for (const Pattern &i : patterns) {
        if (expr_match(i.pattern, x, matches)) {
            return Call::make(type, i.replacement, matches, Call::PureIntrinsic);
        }
    }
    return Expr();
}

// Perform peephole optimizations on the IR, adding appropriate
// interleave and deinterleave calls.
class PatternMatchIntrinsics : public IRMutator {
private:
    using IRMutator::visit;

    Expr visit(const Add *op) override {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        for (halide_type_code_t code : {op->type.code(), halide_type_uint}) {
            Type narrow = op->type.with_bits(op->type.bits() / 2).with_code(code);
            Expr narrow_a = lossless_cast(narrow, a);
            Expr narrow_b = lossless_cast(narrow, b);

            if (narrow_a.defined() && narrow_b.defined()) {
                return widening_add(narrow_a, narrow_b);
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

        if (op->type.is_int()) {
            for (halide_type_code_t code : {halide_type_int, halide_type_uint}) {
                Type narrow = op->type.with_bits(op->type.bits() / 2).with_code(code);
                Expr narrow_a = lossless_cast(narrow, a);
                Expr narrow_b = lossless_cast(narrow, b);

                if (narrow_a.defined() && narrow_b.defined()) {
                    return widening_subtract(narrow_a, narrow_b);
                }
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

        // We're applying this to float, which seems OK? float16 * float16 -> float32 is a widening multiply?
        Type narrow = op->type.with_bits(op->type.bits() / 2);
        Expr narrow_a = lossless_cast(narrow, a);
        Expr narrow_b = lossless_cast(narrow, b);

        if (narrow_a.defined() && narrow_b.defined()) {
            return widening_multiply(narrow_a, narrow_b);
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

        auto rewrite = IRMatcher::rewriter(IRMatcher::div(a, b), op->type);
/*
        if (rewrite((widen(x) + widen(y)) / 2, halving_add(x, y)) ||
            rewrite((widen(x) + widen(y) + 1) / 2, rounding_halving_add(x, y)) ||
            rewrite((widen(x) - widen(y)) / 2, halving_subtract(x, y)) ||
            rewrite((widen(x) - widen(y) + 1) / 2, rounding_halving_subtract(x, y)) ||
            false) {
            return rewrite.result;
        }
        */

        if (!op->type.is_float()) {
            return mutate(lower_int_uint_div(a, b));
        }

        if (a.same_as(op->a) && b.same_as(op->b)) {
            return op;
        } else {
            return Div::make(a, b);
        }
    }

    Expr visit(const Cast *op) override {
        Expr value = mutate(op->value);

        // TODO: This probably should work for 64-bit types too, but it requires 128-bit intermediates.
        if ((op->type.is_int() || op->type.is_uint()) && op->type.bits() > 1 && op->type.bits() <= 32) {
            Expr lower = op->type.min();
            Expr upper = op->type.max();
            if (op->type.is_uint()) {
                Expr uwild(Variable::make(op->type, "*"));
                Expr swild(Variable::make(op->type.with_code(halide_type_int), "*"));
                std::vector<Pattern> patterns = {
                    // Saturating add/subtract
                    { max(min(widening_add(uwild, uwild), upper), lower), Call::saturating_add },
                    { min(max(widening_add(uwild, uwild), lower), upper), Call::saturating_add },
                    { max(min(widening_subtract(uwild, uwild), upper), lower), Call::saturating_subtract },
                    { min(max(widening_subtract(uwild, uwild), lower), upper), Call::saturating_subtract },
                    { min(widening_add(uwild, uwild), upper), Call::saturating_add },
                    { max(widening_subtract(uwild, uwild), lower), Call::saturating_subtract },

                    // Averaging/halving add/subtract
                    { widening_add(uwild, uwild) >> 1, Call::halving_add },
                    { widening_subtract(uwild, uwild) >> 1, Call::halving_subtract },
                    { rounding_shift_right(widening_add(uwild, uwild), 1), Call::rounding_halving_add },
                    // Don't thinkt his one is correct?
                    //{ rounding_shift_right(widening_subtract(uwild, uwild), 1), Call::rounding_halving_subtract },
                };
                Expr result = apply_patterns(op->type, value, patterns);
                if (result.defined()) {
                    return result;
                }
            } else {
                Expr wild(Variable::make(op->type, "*"));
                std::vector<Pattern> patterns = {
                    // Saturating add/subtract
                    { max(min(widening_add(wild, wild), upper), lower), Call::saturating_add },
                    { min(max(widening_add(wild, wild), lower), upper), Call::saturating_add },
                    { max(min(widening_subtract(wild, wild), upper), lower), Call::saturating_subtract },
                    { min(max(widening_subtract(wild, wild), lower), upper), Call::saturating_subtract },

                    // Averaging/halving add/subtract.
                    { widening_add(wild, wild) >> 1, Call::halving_add },
                    { widening_subtract(wild, wild) >> 1, Call::halving_subtract },
                    { rounding_shift_right(widening_add(wild, wild), 1), Call::rounding_halving_add },
                    { rounding_shift_right(widening_subtract(wild, wild), 1), Call::rounding_halving_subtract },
                };
                Expr result = apply_patterns(op->type, value, patterns);
                if (result.defined()) {
                    return result;
                }
            }
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

            if (const Add *add = a.as<Add>()) {
                // Match rounding_shift_right(a, b) = (a + ((1 << b) / 2)) >> b
                Expr round;
                if (op->is_intrinsic(Call::shift_right)) {
                    round = (make_const(a.type(), 1) << max(b, 0)) >> 1;
                } else {
                    round = (make_const(a.type(), 1) >> min(b, 0)) >> 1;
                }
                round = simplify(round);
                Expr add_a = add->a;
                Expr add_b = add->b;
                if (can_prove(add_a == round)) {
                    std::swap(add_a, add_b);
                }
                if (can_prove(add_b == round)) {
                    if (op->is_intrinsic(Call::shift_right)) {
                        return rounding_shift_right(add_a, b);
                    } else {
                        return rounding_shift_left(add_a, b);
                    }
                }
            }

            if (a.same_as(op->args[0]) && b.same_as(op->args[1])) {
                return op;
            } else if (op->is_intrinsic(Call::shift_right)) {
                return Call::make(op->type, Call::shift_right, {a, b}, Call::PureIntrinsic);
            } else {
                return Call::make(op->type, Call::shift_left, {a, b}, Call::PureIntrinsic);
            }
        } else {
            return op;
        }
    }
};

}  // namespace

Stmt pattern_match_intrinsics(Stmt s) {
    //s = substitute_in_all_lets(s);
    s = PatternMatchIntrinsics().mutate(s);
    //s = common_subexpression_elimination(s);
    return s;
}

Expr lower_widening_add(Expr a, Expr b) {
    return widen(a) + widen(b);
}

Expr lower_widening_subtract(Expr a, Expr b) {
    Type wide = a.type().with_bits(a.type().bits() * 2);
    if (wide.is_uint()) {
        wide = wide.with_code(halide_type_int);
    }
    return cast(wide, a) - cast(wide, b);
}

Expr lower_widening_multiply(Expr a, Expr b) {
    return widen(a) * widen(b);
}


Expr lower_rounding_shift_right(Expr a, Expr b) {
    Expr round = simplify((make_const(a.type(), 1) << max(b, 0)) >> 1);
    // This implementating of rounding shift overflows! But it matches a lot of old behavior.
    a = simplify(a + round);
    return Call::make(a.type(), Call::shift_right, {a, b}, Call::PureIntrinsic);
}

Expr lower_rounding_shift_left(Expr a, Expr b) {
    Expr round = simplify((make_const(a.type(), 1) >> min(b, 0)) >> 1);
    // This implementating of rounding shift overflows! But it matches a lot of old behavior.
    a = simplify(a + round);
    return Call::make(a.type(), Call::shift_left, {a, b}, Call::PureIntrinsic);
}

// These intentionally use the non-lowered versions of widening_add/widening_subtract, in the
// hopes that maybe the user of this will be able to use the information. If not, it will
// probably recursively call lower_widening_add/lower_widening_subtract.
Expr lower_saturating_add(Expr a, Expr b) {
    internal_assert(a.type() == b.type());
    return saturating_narrow(widening_add(a, b));
}
Expr lower_saturating_subtract(Expr a, Expr b) {
    internal_assert(a.type() == b.type());
    return saturating_cast(a.type(), widening_subtract(a, b));
}

Expr lower_halving_add(Expr a, Expr b) {
    internal_assert(a.type() == b.type());
    return Call::make(a.type(), Call::shift_right, {widening_add(a, b), 1}, Call::PureIntrinsic);
}

Expr lower_rounding_halving_add(Expr a, Expr b) {
    internal_assert(a.type() == b.type());
    return Call::make(a.type(), Call::shift_right, {widening_add(a, b) + 1, 1}, Call::PureIntrinsic);
}

Expr lower_halving_subtract(Expr a, Expr b) {
    internal_assert(a.type() == b.type());
    return Call::make(a.type(), Call::shift_right, {widening_subtract(a, b), 1}, Call::PureIntrinsic);
}

Expr lower_rounding_halving_subtract(Expr a, Expr b) {
    internal_assert(a.type() == b.type());
    return Call::make(a.type(), Call::shift_right, {widening_subtract(a, b) + 1, 1}, Call::PureIntrinsic);
}


}  // namespace Internal
}  // namespace Halide
