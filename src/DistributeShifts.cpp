#include "CSE.h"
#include "CodeGen_Internal.h"
#include "ConciseCasts.h"
#include "FindIntrinsics.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

namespace {

/**
 * Distribute constant RHS widening shift lefts as multiplies.
 * This is an extremely unfortunate mess. Unfortunately, the
 * simplifier needs to lift constant multiplications due to its
 * cost model. This transformation is very architecture and data-
 * type specific (e.g. useful on ARM and HVX due to a plethora of
 * dot product / widening multiply instructions).
 */
class DistributeShiftsAsMuls : public IRMutator {
public:
    DistributeShiftsAsMuls(const bool multiply_adds)
        : multiply_adds(multiply_adds) {
    }

private:
    const bool multiply_adds;

    static bool is_cast(const Expr &e, Type value_t) {
        if (const Cast *cast = e.as<Cast>()) {
            return cast->value.type() == value_t;
        }
        return false;
    }

    static Expr distribute(const Expr &a, const Expr &b) {
        if (const Add *add = a.as<Add>()) {
            return Add::make(distribute(add->a, b), distribute(add->b, b));
        } else if (const Sub *sub = a.as<Sub>()) {
            Expr sub_a = distribute(sub->a, b);
            Expr sub_b = distribute(sub->b, b);
            Expr negative_sub_b = lossless_negate(sub_b);
            if (negative_sub_b.defined()) {
                return Add::make(sub_a, negative_sub_b);
            } else {
                return Sub::make(sub_a, sub_b);
            }
        } else if (const Cast *cast = a.as<Cast>()) {
            Expr cast_b = lossless_cast(b.type().with_bits(cast->value.type().bits()), b);
            if (cast_b.defined()) {
                Expr mul = widening_mul(cast->value, cast_b);
                if (mul.type().bits() <= cast->type.bits()) {
                    if (mul.type() != cast->type) {
                        mul = Cast::make(cast->type, mul);
                    }
                    return mul;
                }
            }
        } else if (const Call *add = Call::as_intrinsic(a, {Call::widening_add})) {
            Expr add_a = Cast::make(add->type, add->args[0]);
            Expr add_b = Cast::make(add->type, add->args[1]);
            add_a = distribute(add_a, b);
            add_b = distribute(add_b, b);
            // If add_a and add_b are the same kind of cast, we should remake a widening add.
            const Cast *add_a_cast = add_a.as<Cast>();
            const Cast *add_b_cast = add_b.as<Cast>();
            if (add_a_cast && add_b_cast &&
                add_a_cast->value.type() == add->args[0].type() &&
                add_b_cast->value.type() == add->args[1].type()) {
                return widening_add(add_a_cast->value, add_b_cast->value);
            } else {
                return Add::make(add_a, add_b);
            }
        } else if (const Call *sub = Call::as_intrinsic(a, {Call::widening_sub})) {
            Expr sub_a = Cast::make(sub->type, sub->args[0]);
            Expr sub_b = Cast::make(sub->type, sub->args[1]);
            sub_a = distribute(sub_a, b);
            sub_b = distribute(sub_b, b);
            Expr negative_sub_b = lossless_negate(sub_b);
            if (negative_sub_b.defined()) {
                sub_b = negative_sub_b;
            }
            // If sub_a and sub_b are the same kind of cast, we should remake a widening sub.
            const Cast *sub_a_cast = sub_a.as<Cast>();
            const Cast *sub_b_cast = sub_b.as<Cast>();
            if (sub_a_cast && sub_b_cast &&
                sub_a_cast->value.type() == sub->args[0].type() &&
                sub_b_cast->value.type() == sub->args[1].type()) {
                if (negative_sub_b.defined()) {
                    return widening_add(sub_a_cast->value, sub_b_cast->value);
                } else {
                    return widening_sub(sub_a_cast->value, sub_b_cast->value);
                }
            } else {
                if (negative_sub_b.defined()) {
                    return Add::make(sub_a, sub_b);
                } else {
                    return Sub::make(sub_a, sub_b);
                }
            }
        } else if (const Call *mul = Call::as_intrinsic(a, {Call::widening_mul})) {
            Expr mul_a = Cast::make(mul->type, mul->args[0]);
            Expr mul_b = Cast::make(mul->type, mul->args[1]);
            mul_a = distribute(mul_a, b);
            if (const Cast *mul_a_cast = mul_a.as<Cast>()) {
                if (mul_a_cast->value.type() == mul->args[0].type()) {
                    return widening_mul(mul_a_cast->value, mul->args[1]);
                }
            }
            mul_b = distribute(mul_b, b);
            if (const Cast *mul_b_cast = mul_b.as<Cast>()) {
                if (mul_b_cast->value.type() == mul->args[1].type()) {
                    return widening_mul(mul->args[0], mul_b_cast->value);
                }
            }
        }
        return simplify(Mul::make(a, b));
    }

    Expr distribute_shift(const Call *op) {
        if (op->is_intrinsic(Call::shift_left)) {
            if (const uint64_t *const_b = as_const_uint(op->args[1])) {
                Expr a = op->args[0];
                // Only rewrite widening shifts.
                const Cast *cast_a = a.as<Cast>();
                bool is_widening_cast = cast_a && cast_a->type.bits() >= cast_a->value.type().bits() * 2;
                if (is_widening_cast || Call::as_intrinsic(a, {Call::widening_add, Call::widening_mul, Call::widening_sub})) {
                    const uint64_t const_m = 1ull << *const_b;
                    Expr b = make_const(a.type(), const_m);
                    return mutate(distribute(a, b));
                }
            }
        } else if (op->is_intrinsic(Call::widening_shift_left)) {
            if (const uint64_t *const_b = as_const_uint(op->args[1])) {
                const uint64_t const_m = 1ull << *const_b;
                Expr b = make_const(op->type, const_m);
                Expr a = Cast::make(op->type, op->args[0]);
                return mutate(distribute(a, b));
            }
        }
        return IRMutator::visit(op);
    }

    template<typename T>
    Expr visit_add_sub(const T *op) {
        if (multiply_adds) {
            Expr a, b;
            if (const Call *a_call = op->a.template as<Call>()) {
                if (a_call->is_intrinsic({Call::shift_left, Call::widening_shift_left})) {
                    a = distribute_shift(a_call);
                }
            }
            if (const Call *b_call = op->b.template as<Call>()) {
                if (b_call->is_intrinsic({Call::shift_left, Call::widening_shift_left})) {
                    b = distribute_shift(b_call);
                }
            }

            if (a.defined() && b.defined()) {
                return T::make(a, b);
            } else if (a.defined()) {
                b = mutate(op->b);
                return T::make(a, b);
            } else if (b.defined()) {
                a = mutate(op->a);
                return T::make(a, b);
            } else {
                return IRMutator::visit(op);
            }
        } else {
            return IRMutator::visit(op);
        }
    }

    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (multiply_adds) {
            return IRMutator::visit(op);
        } else {
            return distribute_shift(op);
        }
    }

    Expr visit(const Add *op) override {
        return visit_add_sub<Add>(op);
    }

    Expr visit(const Sub *op) override {
        return visit_add_sub<Sub>(op);
    }
};

}  // namespace

Stmt distribute_shifts(const Stmt &s, const bool multiply_adds) {
    return DistributeShiftsAsMuls(multiply_adds).mutate(s);
}

}  // namespace Internal
}  // namespace Halide
