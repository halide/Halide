#include "IRMutator.h"

namespace HalideInternal {

    Expr IRMutator::mutate(Expr e) {
        if (e.defined()) {
            e.accept(this);
        } else {
            expr = Expr();
        }
        stmt = Stmt();
        return expr;
    }

    Stmt IRMutator::mutate(Stmt s) {
        if (s.defined()) {
            s.accept(this);
        } else {
            stmt = Stmt();
        }
        expr = Expr();
        return stmt;
    }

    template<typename T> 
    void mutateBinaryOperator(IRMutator *mutator, const T *op, Expr *expr, Stmt *stmt) {
        Expr a = mutator->mutate(op->a);
        Expr b = mutator->mutate(op->b);
        if (a.sameAs(op->a) && 
            b.sameAs(op->b)) *expr = op;
        else *expr = new T(a, b);            
        *stmt = NULL;
    }

    void IRMutator::visit(const IntImm *op)   {expr = op;}
    void IRMutator::visit(const FloatImm *op) {expr = op;}
    void IRMutator::visit(const Var *op) {expr = op;}

    void IRMutator::visit(const Cast *op) {
        Expr value = mutate(op->value);
        if (value.sameAs(op->value)) expr = op;
        else expr = new Cast(op->type, value);
    }

    void IRMutator::visit(const Add *op)     {mutateBinaryOperator(this, op, &expr, &stmt);}
    void IRMutator::visit(const Sub *op)     {mutateBinaryOperator(this, op, &expr, &stmt);}
    void IRMutator::visit(const Mul *op)     {mutateBinaryOperator(this, op, &expr, &stmt);}
    void IRMutator::visit(const Div *op)     {mutateBinaryOperator(this, op, &expr, &stmt);}
    void IRMutator::visit(const Mod *op)     {mutateBinaryOperator(this, op, &expr, &stmt);}
    void IRMutator::visit(const Min *op)     {mutateBinaryOperator(this, op, &expr, &stmt);}
    void IRMutator::visit(const Max *op)     {mutateBinaryOperator(this, op, &expr, &stmt);}
    void IRMutator::visit(const EQ *op)      {mutateBinaryOperator(this, op, &expr, &stmt);}
    void IRMutator::visit(const NE *op)      {mutateBinaryOperator(this, op, &expr, &stmt);}
    void IRMutator::visit(const LT *op)      {mutateBinaryOperator(this, op, &expr, &stmt);}
    void IRMutator::visit(const LE *op)      {mutateBinaryOperator(this, op, &expr, &stmt);}
    void IRMutator::visit(const GT *op)      {mutateBinaryOperator(this, op, &expr, &stmt);}
    void IRMutator::visit(const GE *op)      {mutateBinaryOperator(this, op, &expr, &stmt);}
    void IRMutator::visit(const And *op)     {mutateBinaryOperator(this, op, &expr, &stmt);}
    void IRMutator::visit(const Or *op)      {mutateBinaryOperator(this, op, &expr, &stmt);}

    void IRMutator::visit(const Not *op) {
        Expr a = mutate(op->a);
        if (a.sameAs(op->a)) expr = a;
        else expr = new Not(a);
    }

    void IRMutator::visit(const Select *op)  {
        Expr cond = mutate(op->condition);
        Expr t = mutate(op->true_value);
        Expr f = mutate(op->false_value);
        if (cond.sameAs(op->condition) && 
            t.sameAs(op->true_value) && 
            f.sameAs(op->false_value)) expr = op;
        else expr = new Select(cond, t, f);
    }

    void IRMutator::visit(const Load *op) {
        Expr index = mutate(op->index);
        if (index.sameAs(op->index)) expr = op;
        else expr = new Load(op->type, op->buffer, index);
    }

    void IRMutator::visit(const Ramp *op) {
        Expr base = mutate(op->base);
        Expr stride = mutate(op->stride);
        if (base.sameAs(op->base) &&
            stride.sameAs(op->stride)) expr = op;
        else expr = new Ramp(base, stride, op->width);
    }

    void IRMutator::visit(const Broadcast *op) {
        Expr value = mutate(op->value);
        if (value.sameAs(op->value)) expr = op;
        else expr = new Broadcast(value, op->width);
    }

    void IRMutator::visit(const Call *op) {
        vector<Expr > new_args(op->args.size());
        bool changed = false;

        // Mutate the args
        for (size_t i = 0; i < op->args.size(); i++) {
            Expr old_arg = op->args[i];
            Expr new_arg = mutate(old_arg);
            if (!new_arg.sameAs(old_arg)) changed = true;
            new_args[i] = new_arg;
        }

        if (!changed) expr = op;
        else expr = new Call(op->type, op->buffer, new_args, op->call_type);
    }

    void IRMutator::visit(const Let *op) {
        Expr value = mutate(op->value);
        Expr body = mutate(op->body);
        if (value.sameAs(op->value) &&
            body.sameAs(op->body)) expr = op;
        else expr = new Let(op->name, value, body);
    }

    void IRMutator::visit(const LetStmt *op) {
        Expr value = mutate(op->value);
        Stmt body = mutate(op->body);
        if (value.sameAs(op->value) &&
            body.sameAs(op->body)) stmt = op;
        else stmt = new LetStmt(op->name, value, body);
    }

    void IRMutator::visit(const PrintStmt *op) {
        vector<Expr > new_args(op->args.size());
        bool args_changed = false;

        // Mutate the args
        for (size_t i = 0; i < op->args.size(); i++) {
            Expr old_arg = op->args[i];
            Expr new_arg = mutate(old_arg);
            if (!new_arg.sameAs(old_arg)) args_changed = true;
            new_args[i] = new_arg;
        }

        if (!args_changed) stmt = op;
        else stmt = new PrintStmt(op->prefix, new_args);
    }

    void IRMutator::visit(const AssertStmt *op) {
        Expr condition = mutate(op->condition);
        if (condition.sameAs(op->condition)) stmt = op;
        else stmt = new AssertStmt(condition, op->message);
    }

    void IRMutator::visit(const Pipeline *op) {
        Stmt produce = mutate(op->produce);
        Stmt update = mutate(op->update);
        Stmt consume = mutate(op->consume);
        if (produce.sameAs(op->produce) &&
            update.sameAs(op->update) &&
            consume.sameAs(op->consume)) {
            stmt = op;
        } else {
            stmt = new Pipeline(op->buffer, produce, update, consume);
        }
    }

    void IRMutator::visit(const For *op) {
        Expr min = mutate(op->min);
        Expr extent = mutate(op->extent);
        Stmt body = mutate(op->body);
        if (min.sameAs(op->min) &&
            extent.sameAs(op->extent) &&
            body.sameAs(op->body)) {
            stmt = op;
        } else {
            stmt = new For(op->name, min, extent, op->for_type, body);
        }
    }

    void IRMutator::visit(const Store *op) {
        Expr value = mutate(op->value);
        Expr index = mutate(op->index);
        if (value.sameAs(op->value) && 
            index.sameAs(op->index)) stmt = op;
        else stmt = new Store(op->buffer, value, index);            
    }

    void IRMutator::visit(const Provide *op) {
        vector<Expr > new_args(op->args.size());
        bool args_changed = false;

        // Mutate the args
        for (size_t i = 0; i < op->args.size(); i++) {
            Expr old_arg = op->args[i];
            Expr new_arg = mutate(old_arg);
            if (!new_arg.sameAs(old_arg)) args_changed = true;
            new_args[i] = new_arg;
        }

        Expr value = mutate(op->value);
            
        if (!args_changed && value.sameAs(op->value)) stmt = op;
        else stmt = new Provide(op->buffer, value, new_args);
    }

    void IRMutator::visit(const Allocate *op) {
        Expr size = mutate(op->size);
        Stmt body = mutate(op->body);
        if (size.sameAs(op->size) && body.sameAs(op->body)) stmt = op;
        else stmt = new Allocate(op->buffer, op->type, size, body);
    }
        
    void IRMutator::visit(const Realize *op) {
        vector<pair<Expr, Expr> > new_bounds(op->bounds.size());
        bool bounds_changed = false;

        // Mutate the bounds
        for (size_t i = 0; i < op->bounds.size(); i++) {
            Expr old_min    = op->bounds[i].first;
            Expr old_extent = op->bounds[i].second;
            Expr new_min    = mutate(old_min);
            Expr new_extent = mutate(old_extent);
            if (!new_min.sameAs(old_min))       bounds_changed = true;
            if (!new_extent.sameAs(old_extent)) bounds_changed = true;
            new_bounds[i] = make_pair(new_min, new_extent);
        }

        Stmt body = mutate(op->body);
        if (!bounds_changed && body.sameAs(op->body)) stmt = op;
        else stmt = new Realize(op->buffer, op->type, new_bounds, body);
    }

    void IRMutator::visit(const Block *op) {
        Stmt first = mutate(op->first);
        Stmt rest = mutate(op->rest);
        if (first.sameAs(op->first) && rest.sameAs(op->rest)) stmt = op;
        else stmt = new Block(first, rest);
    }
}
