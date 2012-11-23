#include "IRMutator.h"

namespace HalideInternal {

    const Expr *IRMutator::mutate(const Expr *e) {
        if (e) {
            e->accept(this);
        } else {
            expr = NULL;
        }
        stmt = NULL;
        return expr;
    }

    const Stmt *IRMutator::mutate(const Stmt *s) {
        if (s) {
            s->accept(this);
        } else {
            stmt = NULL;
        }
        expr = NULL;
        return stmt;
    }

    template<typename T> void mutateBinaryOperator(IRMutator *mutator, const T *op) {
        const Expr *a = mutator->mutate(op->a);
        const Expr *b = mutator->mutate(op->b);
        if (a == op->a && 
            b == op->b) mutator->expr = op;
        else mutator->expr = new T(a, b);            
        mutator->stmt = NULL;
    }

    void IRMutator::visit(const IntImm *op)   {expr = op;}
    void IRMutator::visit(const FloatImm *op) {expr = op;}
    void IRMutator::visit(const Var *op) {expr = op;}

    void IRMutator::visit(const Cast *op) {
        const Expr *value = mutate(op->value);
        if (value == op->value) expr = op;
        else expr = new Cast(op->type, value);
    }

    void IRMutator::visit(const Add *op)     {mutateBinaryOperator(this, op);}
    void IRMutator::visit(const Sub *op)     {mutateBinaryOperator(this, op);}
    void IRMutator::visit(const Mul *op)     {mutateBinaryOperator(this, op);}
    void IRMutator::visit(const Div *op)     {mutateBinaryOperator(this, op);}
    void IRMutator::visit(const Mod *op)     {mutateBinaryOperator(this, op);}
    void IRMutator::visit(const Min *op)     {mutateBinaryOperator(this, op);}
    void IRMutator::visit(const Max *op)     {mutateBinaryOperator(this, op);}
    void IRMutator::visit(const EQ *op)      {mutateBinaryOperator(this, op);}
    void IRMutator::visit(const NE *op)      {mutateBinaryOperator(this, op);}
    void IRMutator::visit(const LT *op)      {mutateBinaryOperator(this, op);}
    void IRMutator::visit(const LE *op)      {mutateBinaryOperator(this, op);}
    void IRMutator::visit(const GT *op)      {mutateBinaryOperator(this, op);}
    void IRMutator::visit(const GE *op)      {mutateBinaryOperator(this, op);}
    void IRMutator::visit(const And *op)     {mutateBinaryOperator(this, op);}
    void IRMutator::visit(const Or *op)      {mutateBinaryOperator(this, op);}

    void IRMutator::visit(const Not *op) {
        const Expr *a = mutate(op->a);
        if (a == op->a) expr = a;
        else expr = new Not(a);
    }

    void IRMutator::visit(const Select *op)  {
        const Expr *cond = mutate(op->condition);
        const Expr *t = mutate(op->true_value);
        const Expr *f = mutate(op->false_value);
        if (cond == op->condition && 
            t == op->true_value && 
            f == op->false_value) expr = op;
        else expr = new Select(cond, t, f);
    }

    void IRMutator::visit(const Load *op) {
        const Expr *index = mutate(op->index);
        if (index == op->index) expr = op;
        else expr = new Load(op->type, op->buffer, index);
    }

    void IRMutator::visit(const Ramp *op) {
        const Expr *base = mutate(op->base);
        const Expr *stride = mutate(op->stride);
        if (base == op->base && stride == op->stride) expr = op;
        else expr = new Ramp(base, stride, op->width);
    }

    void IRMutator::visit(const Call *op) {
        vector<const Expr *> new_args(op->args.size());
        bool changed = false;

        // Mutate the args
        for (size_t i = 0; i < op->args.size(); i++) {
            const Expr *old_arg = op->args[i];
            const Expr *new_arg = mutate(old_arg);
            if (new_arg != old_arg) changed = true;
            new_args[i] = new_arg;
        }

        if (!changed) expr = op;
        else expr = new Call(op->type, op->buffer, new_args, op->call_type);
    }

    void IRMutator::visit(const Let *op) {
        const Expr *value = mutate(op->value);
        const Expr *body = mutate(op->body);
        if (value == op->value && body == op->body) expr = op;
        else expr = new Let(op->name, value, body);
    }

    void IRMutator::visit(const LetStmt *op) {
        const Expr *value = mutate(op->value);
        const Stmt *body = mutate(op->body);
        if (value == op->value && body == op->body) stmt = op;
        else stmt = new LetStmt(op->name, value, body);
    }

    void IRMutator::visit(const PrintStmt *op) {
        vector<const Expr *> new_args(op->args.size());
        bool args_changed = false;

        // Mutate the args
        for (size_t i = 0; i < op->args.size(); i++) {
            const Expr *old_arg = op->args[i];
            const Expr *new_arg = mutate(old_arg);
            if (new_arg != old_arg) args_changed = true;
            new_args[i] = new_arg;
        }

        if (!args_changed) stmt = op;
        else stmt = new PrintStmt(op->prefix, new_args);
    }

    void IRMutator::visit(const AssertStmt *op) {
        const Expr *condition = mutate(op->condition);
        if (condition == op->condition) stmt = op;
        else stmt = new AssertStmt(condition, op->message);
    }

    void IRMutator::visit(const Pipeline *op) {
        const Stmt *produce = mutate(op->produce);
        const Stmt *update = mutate(op->update);
        const Stmt *consume = mutate(op->consume);
        if (produce == op->produce &&
            update == op->update &&
            consume == op->consume) {
            stmt = op;
        } else {
            stmt = new Pipeline(op->buffer, produce, update, consume);
        }
    }

    void IRMutator::visit(const For *op) {
        const Expr *min = mutate(op->min);
        const Expr *extent = mutate(op->extent);
        const Stmt *body = mutate(op->body);
        if (min == op->min &&
            extent == op->extent &&
            body == op->body) {
            stmt = op;
        } else {
            stmt = new For(op->name, min, extent, op->for_type, body);
        }
    }

    void IRMutator::visit(const Store *op) {
        const Expr *value = mutate(op->value);
        const Expr *index = mutate(op->index);
        if (value == op->value && index == op->index) stmt = op;
        else stmt = new Store(op->buffer, value, index);            
    }

    void IRMutator::visit(const Provide *op) {
        vector<const Expr *> new_args(op->args.size());
        bool args_changed = false;

        // Mutate the args
        for (size_t i = 0; i < op->args.size(); i++) {
            const Expr *old_arg = op->args[i];
            const Expr *new_arg = mutate(old_arg);
            if (new_arg != old_arg) args_changed = true;
            new_args[i] = new_arg;
        }

        const Expr *value = mutate(op->value);
            
        if (!args_changed && value == op->value) stmt = op;
        else stmt = new Provide(op->buffer, value, new_args);
    }

    void IRMutator::visit(const Allocate *op) {
        const Expr *size = mutate(op->size);
        const Stmt *body = mutate(op->body);
        if (size == op->size && body == op->body) stmt = op;
        else stmt = new Allocate(op->buffer, op->type, size, body);
    }
        
    void IRMutator::visit(const Realize *op) {
        vector<pair<const Expr *, const Expr *> > new_bounds(op->bounds.size());
        bool bounds_changed = false;

        // Mutate the bounds
        for (size_t i = 0; i < op->bounds.size(); i++) {
            const Expr *old_min = op->bounds[i].first;
            const Expr *old_extent = op->bounds[i].second;
            const Expr *new_min = mutate(old_min);
            const Expr *new_extent = mutate(old_extent);
            if (new_min != old_min) bounds_changed = true;
            if (new_extent != old_extent) bounds_changed = true;
            new_bounds[i] = make_pair(new_min, new_extent);
        }

        const Stmt *body = mutate(op->body);
        if (!bounds_changed && body == op->body) stmt = op;
        else stmt = new Realize(op->buffer, op->type, new_bounds, body);
    }

    void IRMutator::visit(const Block *op) {
        const Stmt *first = mutate(op->first);
        const Stmt *rest = mutate(op->rest);
        if (first == op->first && rest == op->rest) stmt = op;
        else stmt = new Block(first, rest);
    }
}


