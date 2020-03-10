#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include <algorithm>

namespace Halide {
namespace Internal {

class EliminateBoolVectors : public IRMutator {
private:
    using IRMutator::visit;

    Scope<Type> lets;

    Expr visit(const Variable *op) override {
        if (lets.contains(op->name)) {
            return Variable::make(lets.get(op->name), op->name);
        } else {
            return op;
        }
    }

    template<typename T>
    Expr visit_comparison(const T *op) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);
        Type t = a.type();

        // Ensure both a and b have the same type (if this is a vector
        // comparison). This should only be necessary if the operands are
        // integer vectors (promoted from bool vectors).
        if (t.lanes() > 1 && t.bits() != b.type().bits()) {
            internal_assert(t.is_int() && b.type().is_int());

            t = t.with_bits(std::max(t.bits(), b.type().bits()));
            if (t != a.type()) {
                a = Call::make(t, Call::cast_mask, {a}, Call::PureIntrinsic);
            }
            if (t != b.type()) {
                b = Call::make(t, Call::cast_mask, {b}, Call::PureIntrinsic);
            }
        }

        Expr expr;
        if (!a.same_as(op->a) || !b.same_as(op->b)) {
            expr = T::make(a, b);
        } else {
            expr = op;
        }

        if (t.lanes() > 1) {
            // To represent bool vectors, OpenCL uses vectors of signed
            // integers with the same width as the types being compared.
            expr = Call::make(t.with_code(Type::Int), Call::bool_to_mask, {expr}, Call::PureIntrinsic);
        }
        return expr;
    }

    Expr visit(const EQ *op) override {
        return visit_comparison(op);
    }
    Expr visit(const NE *op) override {
        return visit_comparison(op);
    }
    Expr visit(const LT *op) override {
        return visit_comparison(op);
    }
    Expr visit(const LE *op) override {
        return visit_comparison(op);
    }
    Expr visit(const GT *op) override {
        return visit_comparison(op);
    }
    Expr visit(const GE *op) override {
        return visit_comparison(op);
    }

    template<typename T>
    Expr visit_logical_binop(const T *op, Call::IntrinsicOp bitwise_op) {
        Expr a = mutate(op->a);
        Expr b = mutate(op->b);

        Type ta = a.type();
        Type tb = b.type();
        if (ta.lanes() > 1) {
            // Ensure that both a and b have the same type.
            Type t = ta.with_bits(std::max(ta.bits(), tb.bits()));
            if (t != a.type()) {
                a = Call::make(t, Call::cast_mask, {a}, Call::PureIntrinsic);
            }
            if (t != b.type()) {
                b = Call::make(t, Call::cast_mask, {b}, Call::PureIntrinsic);
            }
            // Replace logical operation with bitwise operation.
            return Call::make(t, bitwise_op, {a, b}, Call::PureIntrinsic);
        } else if (!a.same_as(op->a) || !b.same_as(op->b)) {
            return T::make(a, b);
        } else {
            return op;
        }
    }

    Expr visit(const Or *op) override {
        return visit_logical_binop(op, Call::bitwise_or);
    }

    Expr visit(const And *op) override {
        return visit_logical_binop(op, Call::bitwise_and);
    }

    Expr visit(const Not *op) override {
        Expr a = mutate(op->a);
        if (a.type().lanes() > 1) {
            // Replace logical operation with bitwise operation.
            return Call::make(a.type(), Call::bitwise_not, {a}, Call::PureIntrinsic);
        } else if (!a.same_as(op->a)) {
            return Not::make(a);
        } else {
            return op;
        }
    }

    Expr visit(const Cast *op) override {
        if (op->value.type().is_bool() && op->value.type().is_vector()) {
            // Casting from bool
            return mutate(Select::make(op->value,
                                       make_one(op->type),
                                       make_zero(op->type)));
        } else if (op->type.is_bool() && op->type.is_vector()) {
            // Cast to bool
            return mutate(op->value != make_zero(op->value.type()));
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Store *op) override {
        Expr predicate = op->predicate;
        if (!is_one(predicate)) {
            predicate = mutate(predicate);
        }
        Expr value = op->value;
        if (op->value.type().is_bool()) {
            Type ty = UInt(8, op->value.type().lanes());
            value = Select::make(value,
                                 make_one(ty),
                                 make_zero(ty));
        }
        value = mutate(value);
        Expr index = mutate(op->index);

        if (predicate.same_as(op->predicate) && value.same_as(op->value) && index.same_as(op->index)) {
            return op;
        } else {
            return Store::make(op->name, value, index, op->param, predicate, op->alignment);
        }
    }

    Expr visit(const Load *op) override {
        Expr predicate = op->predicate;
        if (!is_one(predicate)) {
            predicate = mutate(predicate);
        }
        Expr index = mutate(op->index);
        if (predicate.same_as(op->predicate) && index.same_as(op->index)) {
            return op;
        } else {
            return Load::make(op->type, op->name, std::move(index),
                              op->image, op->param, std::move(predicate),
                              op->alignment);
        }
    }

    // Assuming that a and b should have the same scalar type and they might have
    // been boolean vectors converted to integer vectors, cast the masks to be the
    // same type. This is necessary in the case of a select or Call::if_then_else, e.g.:
    //
    //    Expr a = float_expr1() < float_expr2();  // promoted to int32xN
    //    Expr b = uint8_expr1() < uint8_expr2();  // promoted to int8xN
    //    Expr c = select(a < b, a, b);            // whoops
    static void unify_bool_vector_types(Expr &a, Expr &b) {
        if (a.type().bits() != b.type().bits() &&
            a.type().lanes() == b.type().lanes() &&
            a.type().is_int() && b.type().is_int()) {
            if (a.type().bits() > b.type().bits()) {
                b = Call::make(a.type(), Call::cast_mask, {b}, Call::PureIntrinsic);
            } else {
                a = Call::make(b.type(), Call::cast_mask, {a}, Call::PureIntrinsic);
            }
        }
    }

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::if_then_else)) {
            internal_assert(op->args.size() == 3);
            if (op->args[0].type().is_vector()) {
                Expr cond = mutate(op->args[0]);
                Expr true_value = mutate(op->args[1]);
                Expr false_value = mutate(op->args[2]);
                Type cond_ty = cond.type();

                // If the condition is a vector, it should be a vector of ints.
                internal_assert(cond_ty.code() == Type::Int);

                // if_then_else_mask requires that all 3 operands have the same
                // width.
                unify_bool_vector_types(true_value, false_value);
                internal_assert(true_value.type().bits() == false_value.type().bits());
                if (true_value.type().bits() != cond_ty.bits()) {
                    cond_ty = cond_ty.with_bits(true_value.type().bits());
                    cond = Call::make(cond_ty, Call::cast_mask, {cond}, Call::PureIntrinsic);
                }

                return Call::make(true_value.type(), Call::if_then_else_mask, {cond, true_value, false_value}, Call::PureIntrinsic);
            }
        } else if (op->is_intrinsic(Call::require)) {
            internal_assert(op->args.size() == 3);
            if (op->args[0].type().is_vector()) {
                Expr cond = mutate(op->args[0]);
                Expr value = mutate(op->args[1]);
                Expr message = mutate(op->args[2]);

                // If the condition is a vector, it should be a vector of ints.
                internal_assert(cond.type().code() == Type::Int);

                return Call::make(value.type(), Call::require_mask, {cond, value, message}, Call::PureIntrinsic);
            }
        }

        return IRMutator::visit(op);
    }

    Expr visit(const Select *op) override {
        Expr cond = mutate(op->condition);
        Expr true_value = mutate(op->true_value);
        Expr false_value = mutate(op->false_value);
        Type cond_ty = cond.type();
        if (cond_ty.is_vector()) {
            // If the condition is a vector, it should be a vector of
            // ints.
            internal_assert(cond_ty.code() == Type::Int);

            // select_mask requires that all 3 operands have the same
            // width.
            unify_bool_vector_types(true_value, false_value);
            internal_assert(true_value.type().bits() == false_value.type().bits());
            if (true_value.type().bits() != cond_ty.bits()) {
                cond_ty = cond_ty.with_bits(true_value.type().bits());
                cond = Call::make(cond_ty, Call::cast_mask, {cond}, Call::PureIntrinsic);
            }

            return Call::make(true_value.type(), Call::select_mask, {cond, true_value, false_value}, Call::PureIntrinsic);
        } else if (!cond.same_as(op->condition) ||
                   !true_value.same_as(op->true_value) ||
                   !false_value.same_as(op->false_value)) {
            return Select::make(cond, true_value, false_value);
        } else {
            return op;
        }
    }

    Expr visit(const Broadcast *op) override {
        Expr value = mutate(op->value);
        if (op->type.bits() == 1) {
            return Broadcast::make(Call::make(Int(8), Call::bool_to_mask, {value}, Call::PureIntrinsic), op->lanes);
        } else if (!value.same_as(op->value)) {
            return Broadcast::make(value, op->lanes);
        } else {
            return op;
        }
    }

    Expr visit(const Shuffle *op) override {
        Expr expr = IRMutator::visit(op);
        if (op->is_extract_element() && op->type.is_bool()) {
            op = expr.as<Shuffle>();
            internal_assert(op);
            // This is extracting a scalar element of a bool
            // vector. Generate a call to extract_mask_element.
            expr = Call::make(Bool(), Call::extract_mask_element, {Shuffle::make_concat(op->vectors), op->indices[0]}, Call::PureIntrinsic);
        }
        return expr;
    }

    template<typename NodeType, typename LetType>
    NodeType visit_let(const LetType *op) {
        Expr value = mutate(op->value);

        // We changed the type of the let, we need to replace the
        // references to the let in the body. We can't just substitute
        // them, because the types won't match without running the
        // other visitors during the substitution, so we save the
        // types that we changed for later.
        if (value.type() != op->value.type()) {
            lets.push(op->name, value.type());
        }
        auto body = mutate(op->body);
        if (value.type() != op->value.type()) {
            lets.pop(op->name);
        }

        if (!value.same_as(op->value) || !body.same_as(op->body)) {
            return LetType::make(op->name, value, body);
        } else {
            return op;
        }
    }

    Expr visit(const Let *op) override {
        return visit_let<Expr>(op);
    }
    Stmt visit(const LetStmt *op) override {
        return visit_let<Stmt>(op);
    }
};

Stmt eliminate_bool_vectors(const Stmt &s) {
    return EliminateBoolVectors().mutate(s);
}

Expr eliminate_bool_vectors(const Expr &e) {
    return EliminateBoolVectors().mutate(e);
}

}  // namespace Internal
}  // namespace Halide
