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

    void visit(const Variable *op) {
        if (lets.contains(op->name)) {
            expr = Variable::make(lets.get(op->name), op->name);
        } else {
            expr = op;
        }
    }

    template <typename T>
    void visit_comparison(const T* op) {
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
    }

    void visit(const EQ *op) { visit_comparison(op); }
    void visit(const NE *op) { visit_comparison(op); }
    void visit(const LT *op) { visit_comparison(op); }
    void visit(const LE *op) { visit_comparison(op); }
    void visit(const GT *op) { visit_comparison(op); }
    void visit(const GE *op) { visit_comparison(op); }

    template <typename T>
    void visit_logical_binop(const T* op, const std::string& bitwise_op) {
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
            expr = Call::make(t, bitwise_op, {a, b}, Call::PureIntrinsic);
        } else if (!a.same_as(op->a) || !b.same_as(op->b)) {
            expr = T::make(a, b);
        } else {
            expr = op;
        }
    }

    void visit(const Or *op) {
        visit_logical_binop(op, Call::bitwise_or);
    }

    void visit(const And *op) {
        visit_logical_binop(op, Call::bitwise_and);
    }

    void visit(const Not *op) {
        Expr a = mutate(op->a);
        if (a.type().lanes() > 1) {
            // Replace logical operation with bitwise operation.
            expr = Call::make(a.type(), Call::bitwise_not, {a}, Call::PureIntrinsic);
        } else if (!a.same_as(op->a)) {
            expr = Not::make(a);
        } else {
            expr = op;
        }
    }

    void visit(const Cast *op) {
        if (op->value.type().is_bool() && op->value.type().is_vector()) {
            // Casting from bool
            expr = mutate(Select::make(op->value,
                                       make_one(op->type),
                                       make_zero(op->type)));
        } else if (op->type.is_bool() && op->type.is_vector()) {
            // Cast to bool
            expr = mutate(op->value != make_zero(op->value.type()));
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Store *op) {
        Expr predicate = mutate(op->predicate);
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
            stmt = op;
        } else {
            stmt = Store::make(op->name, value, index, op->param, predicate);
        }
    }

    void visit(const Select *op) {
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
            internal_assert(true_value.type().bits() == false_value.type().bits());
            if (true_value.type().bits() != cond_ty.bits()) {
                cond_ty = cond_ty.with_bits(true_value.type().bits());
                cond = Call::make(cond_ty, Call::cast_mask, {cond}, Call::PureIntrinsic);
            }

            expr = Call::make(true_value.type(), Call::select_mask, {cond, true_value, false_value}, Call::PureIntrinsic);
        } else if (!cond.same_as(op->condition) ||
                   !true_value.same_as(op->true_value) ||
                   !false_value.same_as(op->false_value)) {
            expr = Select::make(cond, true_value, false_value);
        } else {
            expr = op;
        }
    }

    void visit(const Broadcast *op) {
        Expr value = mutate(op->value);
        if (op->type.bits() == 1) {
            expr = Broadcast::make(Call::make(Int(8), Call::bool_to_mask, {value}, Call::PureIntrinsic), op->lanes);
        } else if (!value.same_as(op->value)) {
            expr = Broadcast::make(value, op->lanes);
        } else {
            expr = op;
        }
    }

    void visit(const Shuffle *op) {
        IRMutator::visit(op);
        if (op->is_extract_element() && op->type.is_bool()) {
            op = expr.as<Shuffle>();
            internal_assert(op);
            // This is extracting a scalar element of a bool
            // vector. Generate a call to extract_mask_element.
            expr = Call::make(Bool(), Call::extract_mask_element, {Shuffle::make_concat(op->vectors), op->indices[0]}, Call::PureIntrinsic);
        }
    }

    template <typename NodeType, typename LetType>
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

    void visit(const Let *op) { expr = visit_let<Expr>(op); }
    void visit(const LetStmt *op) { stmt = visit_let<Stmt>(op); }
};

Stmt eliminate_bool_vectors(Stmt s) {
    return EliminateBoolVectors().mutate(s);
}

Expr eliminate_bool_vectors(Expr e) {
    return EliminateBoolVectors().mutate(e);
}

}  // namespace Internal
}  // namespace Halide
