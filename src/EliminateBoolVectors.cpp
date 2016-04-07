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
                a = Cast::make(t, a);
            }
            if (t != b.type()) {
                b = Cast::make(t, b);
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
            t = t.with_code(Type::Int);
            expr = Cast::make(t, expr);
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
                a = Cast::make(t, a);
            }
            if (t != b.type()) {
                b = Cast::make(t, b);
            }
            // Replace logical operation with bitwise operation.
            expr = Call::make(t, bitwise_op, {a, b}, Call::Intrinsic);
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
            expr = Call::make(a.type(), Call::bitwise_not, {a}, Call::Intrinsic);
        } else if (!a.same_as(op->a)) {
            expr = Not::make(a);
        } else {
            expr = op;
        }
    }

    void visit(const Select *op) {
        Expr cond = mutate(op->condition);
        Expr true_value = mutate(op->true_value);
        Expr false_value = mutate(op->false_value);
        Type cond_ty = cond.type();
        if (cond_ty.lanes() > 1) {
            // If the condition is a vector, it should be a vector of
            // ints, so rewrite it to compare to 0.
            internal_assert(cond_ty.code() == Type::Int);

            // OpenCL's select function requires that all 3 operands
            // have the same width.
            internal_assert(true_value.type().bits() == false_value.type().bits());
            if (true_value.type().bits() != cond_ty.bits()) {
                cond_ty = cond_ty.with_bits(true_value.type().bits());
                cond = Cast::make(cond_ty, cond);
            }

            // To make the Select op legal, convert it back to a
            // vector of bool by comparing with zero.
            expr = Select::make(NE::make(cond, make_zero(cond_ty)), true_value, false_value);
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
            expr = Broadcast::make(-Cast::make(Int(8), value), op->lanes);
        } else if (!value.same_as(op->value)) {
            expr = Broadcast::make(value, op->lanes);
        } else {
            expr = op;
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
    EliminateBoolVectors eliminator;
    return eliminator.mutate(s);
}

}  // namespace Internal
}  // namespace Halide
