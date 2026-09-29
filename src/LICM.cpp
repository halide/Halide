#include "LICM.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "ReversePeel.h"
#include "Scope.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

Stmt hoist_loop_invariant_values(const Stmt &s) {
    return reverse_peel_lets(s);
}

namespace {

// Move IfThenElse nodes from the inside of a piece of Stmt IR to the
// outside when legal.
class HoistIfStatements : public IRMutator {
protected:
    using IRMutator::visit;

    Stmt visit(const LetStmt *op) override {
        Stmt body = mutate(op->body);
        if (const IfThenElse *i = body.as<IfThenElse>()) {
            if (!i->else_case.defined() &&
                is_pure(op->value) &&
                is_pure(i->condition) &&
                !expr_uses_var(i->condition, op->name)) {
                Stmt s = op->with(op->value, i->then_case);
                return IfThenElse::make(i->condition, s);
            }
        }
        return op->with(op->value, body);
    }

    Stmt visit(const For *op) override {
        Stmt body = mutate(op->body);
        if (const IfThenElse *i = body.as<IfThenElse>()) {
            if (!i->else_case.defined() &&
                is_pure(i->condition) &&
                !expr_uses_var(i->condition, op->name)) {
                Stmt s = op->with(op->min, op->max, i->then_case);
                return IfThenElse::make(i->condition, s);
            }
        }
        return op->with(op->min, op->max, body);
    }

    Stmt visit(const ProducerConsumer *op) override {
        Stmt body = mutate(op->body);
        if (const IfThenElse *i = body.as<IfThenElse>()) {
            if (!i->else_case.defined() &&
                is_pure(i->condition)) {
                Stmt s = op->with(i->then_case);
                return IfThenElse::make(i->condition, s);
            }
        }
        return op->with(body);
    }

    Stmt visit(const IfThenElse *op) override {
        Stmt then_case = mutate(op->then_case);
        if (!op->else_case.defined() &&
            is_pure(op->condition)) {
            if (const IfThenElse *i = then_case.as<IfThenElse>()) {
                if (!i->else_case.defined() &&
                    is_pure(i->condition)) {
                    return IfThenElse::make(op->condition && i->condition, then_case);
                }
            }
        }
        Stmt else_case = mutate(op->else_case);
        if (then_case.same_as(op->then_case) && else_case.same_as(op->else_case)) {
            return op;
        } else {
            return IfThenElse::make(op->condition, then_case, else_case);
        }
    }

    Stmt visit(const Allocate *op) override {
        Stmt body = mutate(op->body);
        if (const IfThenElse *i = body.as<IfThenElse>()) {
            if (!i->else_case.defined() &&
                is_pure(i->condition)) {
                Stmt s = op->with(op->extents, op->condition, i->then_case);
                return IfThenElse::make(i->condition, s);
            }
        }
        return op->with(op->extents, op->condition, body);
    }

    Stmt visit(const Block *op) override {
        Stmt first = mutate(op->first);
        Stmt rest = mutate(op->rest);

        const IfThenElse *i1 = first.as<IfThenElse>();
        const Block *b = rest.as<Block>();
        const IfThenElse *i2 = b ? b->first.as<IfThenElse>() : rest.as<IfThenElse>();

        if (i1 &&
            i2 &&
            !i1->else_case.defined() &&
            !i2->else_case.defined() &&
            is_pure(i1->condition) &&
            can_prove(i1->condition == i2->condition)) {
            Stmt s = Block::make(i1->then_case, i2->then_case);
            s = IfThenElse::make(i1->condition, s);
            if (b) {
                s = Block::make(s, b->rest);
            }
            return s;
        } else if (first.same_as(op->first) && rest.same_as(op->rest)) {
            return op;
        } else {
            return Block::make(first, rest);
        }
    }
};

}  // namespace

Stmt hoist_loop_invariant_if_statements(const Stmt &s) {
    return HoistIfStatements()(s);
}

}  // namespace Internal
}  // namespace Halide
