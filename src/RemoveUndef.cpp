#include "RemoveUndef.h"
#include "IRMutator.h"
#include "Scope.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::vector;

class RemoveUndef : public IRMutator {
    using IRMutator::visit;

    Scope<int> dead_vars;

    Expr predicate;

    void visit(const Variable *op) {
        if (dead_vars.contains(op->name)) {
            expr = Expr();
        } else {
            expr = op;
        }
    }

    template<typename T>
    void mutate_binary_operator(const T *op) {
        Expr a = mutate(op->a);
        if (!expr.defined()) return;
        Expr b = mutate(op->b);
        if (!expr.defined()) return;
        if (a.same_as(op->a) &&
            b.same_as(op->b)) {
            expr = op;
        } else {
            expr = T::make(a, b);
        }
        stmt = Stmt();
    }

    void visit(const Cast *op) {
        Expr value = mutate(op->value);
        if (!expr.defined()) return;
        if (value.same_as(op->value)) {
            expr = op;
        } else {
            expr = Cast::make(op->type, value);
        }
    }

    void visit(const Add *op)     {mutate_binary_operator(op);}
    void visit(const Sub *op)     {mutate_binary_operator(op);}
    void visit(const Mul *op)     {mutate_binary_operator(op);}
    void visit(const Div *op)     {mutate_binary_operator(op);}
    void visit(const Mod *op)     {mutate_binary_operator(op);}
    void visit(const Min *op)     {mutate_binary_operator(op);}
    void visit(const Max *op)     {mutate_binary_operator(op);}
    void visit(const EQ *op)      {mutate_binary_operator(op);}
    void visit(const NE *op)      {mutate_binary_operator(op);}
    void visit(const LT *op)      {mutate_binary_operator(op);}
    void visit(const LE *op)      {mutate_binary_operator(op);}
    void visit(const GT *op)      {mutate_binary_operator(op);}
    void visit(const GE *op)      {mutate_binary_operator(op);}
    void visit(const And *op)     {mutate_binary_operator(op);}
    void visit(const Or *op)      {mutate_binary_operator(op);}

    void visit(const Not *op) {
        Expr a = mutate(op->a);
        if (!expr.defined()) return;
        if (a.same_as(op->a)) {
            expr = op;
        }
        else expr = Not::make(a);
    }

    void visit(const Select *op)  {
        Expr cond = mutate(op->condition);
        Expr t = mutate(op->true_value);
        Expr f = mutate(op->false_value);

        if (!cond.defined()) {
            expr = Expr();
            return;
        }

        if (!t.defined() && !f.defined()) {
            expr = Expr();
            return;
        }

        if (!t.defined()) {
            // Swap the cases so that we only need to deal with the
            // case when false is not defined below.
            cond = Not::make(cond);
            t = f;
            f = Expr();
        }

        if (!f.defined()) {
            // We need to convert this to an if-then-else
            if (predicate.defined()) {
                predicate = predicate && cond;
            } else {
                predicate = cond;
            }
            expr = t;
        } else if (cond.same_as(op->condition) &&
            t.same_as(op->true_value) &&
            f.same_as(op->false_value)) {
            expr = op;
        } else {
            expr = Select::make(cond, t, f);
        }
    }

    void visit(const Load *op) {
        Expr index = mutate(op->index);
        if (!expr.defined()) return;
        if (index.same_as(op->index)) {
            expr = op;
        } else {
            expr = Load::make(op->type, op->name, index, op->image, op->param);
        }
    }

    void visit(const Ramp *op) {
        Expr base = mutate(op->base);
        if (!expr.defined()) return;
        Expr stride = mutate(op->stride);
        if (!expr.defined()) return;
        if (base.same_as(op->base) &&
            stride.same_as(op->stride)) {
            expr = op;
        } else {
            expr = Ramp::make(base, stride, op->width);
        }
    }

    void visit(const Broadcast *op) {
        Expr value = mutate(op->value);
        if (!expr.defined()) return;
        if (value.same_as(op->value)) expr = op;
        else expr = Broadcast::make(value, op->width);
    }

    void visit(const Call *op) {
        if (op->name == Call::undef &&
            op->call_type == Call::Intrinsic) {
            expr = Expr();
            return;
        }

        vector<Expr> new_args(op->args.size());
        bool changed = false;

        // Mutate the args
        for (size_t i = 0; i < op->args.size(); i++) {
            Expr old_arg = op->args[i];
            Expr new_arg = mutate(old_arg);
            if (!expr.defined()) return;
            if (!new_arg.same_as(old_arg)) changed = true;
            new_args[i] = new_arg;
        }

        if (!changed) {
            expr = op;
        } else {
            expr = Call::make(op->type, op->name, new_args, op->call_type,
                              op->func, op->value_index, op->image, op->param);
        }
    }

    void visit(const Let *op) {
        Expr value = mutate(op->value);
        if (!value.defined()) {
            dead_vars.push(op->name, 0);
        }
        Expr body = mutate(op->body);
        if (!value.defined()) {
            dead_vars.pop(op->name);
        }
        if (!expr.defined()) return;
        if (value.same_as(op->value) &&
            body.same_as(op->body)) {
            expr = op;
        } else if (!value.defined()) {
            expr = body;
        } else {
            expr = Let::make(op->name, value, body);
            predicate = substitute(op->name, value, predicate);
        }
    }

    void visit(const LetStmt *op) {
        Expr value = mutate(op->value);
        if (!value.defined()) {
            dead_vars.push(op->name, 0);
        }
        Stmt body = mutate(op->body);
        if (!value.defined()) {
            dead_vars.pop(op->name);
        }
        if (!stmt.defined()) return;
        if (value.same_as(op->value) &&
            body.same_as(op->body)) {
            stmt = op;
        } else if (!value.defined()) {
            stmt = body;
        } else {
            stmt = LetStmt::make(op->name, value, body);
        }
    }

    void visit(const AssertStmt *op) {
        Expr condition = mutate(op->condition);
        if (!expr.defined()) {
            stmt = Stmt();
            return;
        }
        if (condition.same_as(op->condition)) {
            stmt = op;
        } else {
            stmt = AssertStmt::make(condition, op->message);
        }
    }

    void visit(const Pipeline *op) {
        Stmt produce = mutate(op->produce);
        if (!produce.defined()) {
            produce = AssertStmt::make(const_true(), "Produce step elided due to use of Halide::undef");
        }
        Stmt update = mutate(op->update);
        Stmt consume = mutate(op->consume);
        if (!stmt.defined()) return;
        if (produce.same_as(op->produce) &&
            update.same_as(op->update) &&
            consume.same_as(op->consume)) {
            stmt = op;
        } else {
            stmt = Pipeline::make(op->name, produce, update, consume);
        }
    }

    void visit(const For *op) {
        Expr min = mutate(op->min);
        if (!expr.defined()) {
            stmt = Stmt();
            return;
        }
        Expr extent = mutate(op->extent);
        if (!expr.defined()) {
            stmt = Stmt();
            return;
        }
        Stmt body = mutate(op->body);
        if (!stmt.defined()) return;
        if (min.same_as(op->min) &&
            extent.same_as(op->extent) &&
            body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = For::make(op->name, min, extent, op->for_type, body);
        }
    }

    void visit(const Store *op) {
        predicate = Expr();

        Expr value = mutate(op->value);
        if (!expr.defined()) {
            stmt = Stmt();
            return;
        }

        Expr index = mutate(op->index);
        if (!expr.defined()) {
            stmt = Stmt();
            return;
        }

        if (predicate.defined()) {
            // This becomes a conditional store
            stmt = IfThenElse::make(predicate, Store::make(op->name, value, index));
            predicate = Expr();
        } else if (value.same_as(op->value) &&
                   index.same_as(op->index)) {
            stmt = op;
        } else {
            stmt = Store::make(op->name, value, index);
        }
    }

    void visit(const Provide *op) {
        vector<Expr> new_args(op->args.size());
        vector<Expr> new_values(op->values.size());
        bool changed = false;

        // Mutate the args
        for (size_t i = 0; i < op->args.size(); i++) {
            Expr old_arg = op->args[i];
            Expr new_arg = mutate(old_arg);
            if (!expr.defined()) {
                stmt = Stmt();
                return;
            }
            if (!new_arg.same_as(old_arg)) changed = true;
            new_args[i] = new_arg;
        }

        for (size_t i = 0; i < op->values.size(); i++) {
            Expr old_value = op->values[i];
            Expr new_value = mutate(old_value);
            if (!expr.defined()) {
                stmt = Stmt();
                return;
            }
            if (!new_value.same_as(old_value)) changed = true;
            new_values[i] = new_value;
        }

        if (!changed) {
            stmt = op;
        } else {
            stmt = Provide::make(op->name, new_values, new_args);
        }
    }

    void visit(const Allocate *op) {
        Expr size = mutate(op->size);
        if (!expr.defined()) {
            stmt = Stmt();
            return;
        }
        Stmt body = mutate(op->body);
        if (!stmt.defined()) return;
        if (size.same_as(op->size) && body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = Allocate::make(op->name, op->type, size, body);
        }
    }

    void visit(const Free *op) {
        stmt = op;
    }

    void visit(const Realize *op) {
        Region new_bounds(op->bounds.size());
        bool bounds_changed = false;

        // Mutate the bounds
        for (size_t i = 0; i < op->bounds.size(); i++) {
            Expr old_min    = op->bounds[i].min;
            Expr old_extent = op->bounds[i].extent;
            Expr new_min    = mutate(old_min);
            if (!expr.defined()) {
                stmt = Stmt();
                return;
            }
            Expr new_extent = mutate(old_extent);
            if (!expr.defined()) {
                stmt = Stmt();
                return;
            }
            if (!new_min.same_as(old_min))       bounds_changed = true;
            if (!new_extent.same_as(old_extent)) bounds_changed = true;
            new_bounds[i] = Range(new_min, new_extent);
        }

        Stmt body = mutate(op->body);
        if (!stmt.defined()) return;
        if (!bounds_changed && body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = Realize::make(op->name, op->types, new_bounds, body);
        }
    }

    void visit(const Block *op) {
        Stmt first = mutate(op->first);
        Stmt rest = mutate(op->rest);
        if (!first.defined()) {
            stmt = rest;
        } else if (!rest.defined()) {
            stmt = first;
        } else if (first.same_as(op->first) &&
                   rest.same_as(op->rest)) {
            stmt = op;
        } else {
            stmt = Block::make(first, rest);
        }
    }

    void visit(const IfThenElse *op) {
        Expr condition = mutate(op->condition);
        if (!expr.defined()) {
            stmt = Stmt();
            return;
        }
        Stmt then_case = mutate(op->then_case);
        Stmt else_case = mutate(op->else_case);

        if (!then_case.defined() && !else_case.defined()) {
            stmt = Stmt();
            return;
        }

        if (!then_case.defined()) {
            condition = Not::make(condition);
            then_case = else_case;
            else_case = Stmt();
        }

        if (condition.same_as(op->condition) &&
            then_case.same_as(op->then_case) &&
            else_case.same_as(op->else_case)) {
            stmt = op;
        } else {
            stmt = IfThenElse::make(condition, then_case, else_case);
        }
    }

    void visit(const Evaluate *op) {
        Expr v = mutate(op->value);
        if (!expr.defined()) {
            stmt = Stmt();
        } else if (v.same_as(op->value)) {
            stmt = op;
        } else {
            stmt = Evaluate::make(v);
        }
    }
};

Stmt remove_undef(Stmt s) {
    return RemoveUndef().mutate(s);
}

}
}
