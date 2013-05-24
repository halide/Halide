#include "IRMutator.h"

namespace Halide {
namespace Internal {

using std::vector;

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
void mutate_binary_operator(IRMutator *mutator, const T *op, Expr *expr, Stmt *stmt) {
    Expr a = mutator->mutate(op->a);
    Expr b = mutator->mutate(op->b);
    if (a.same_as(op->a) && 
        b.same_as(op->b)) *expr = op;
    else *expr = new T(a, b);            
    *stmt = NULL;
}

void IRMutator::visit(const IntImm *op)   {expr = op;}
void IRMutator::visit(const FloatImm *op) {expr = op;}
void IRMutator::visit(const Variable *op) {expr = op;}

void IRMutator::visit(const Cast *op) {
    Expr value = mutate(op->value);
    if (value.same_as(op->value)) expr = op;
    else expr = new Cast(op->type, value);
}

void IRMutator::visit(const Add *op)     {mutate_binary_operator(this, op, &expr, &stmt);}
void IRMutator::visit(const Sub *op)     {mutate_binary_operator(this, op, &expr, &stmt);}
void IRMutator::visit(const Mul *op)     {mutate_binary_operator(this, op, &expr, &stmt);}
void IRMutator::visit(const Div *op)     {mutate_binary_operator(this, op, &expr, &stmt);}
void IRMutator::visit(const Mod *op)     {mutate_binary_operator(this, op, &expr, &stmt);}
void IRMutator::visit(const Min *op)     {mutate_binary_operator(this, op, &expr, &stmt);}
void IRMutator::visit(const Max *op)     {mutate_binary_operator(this, op, &expr, &stmt);}
void IRMutator::visit(const EQ *op)      {mutate_binary_operator(this, op, &expr, &stmt);}
void IRMutator::visit(const NE *op)      {mutate_binary_operator(this, op, &expr, &stmt);}
void IRMutator::visit(const LT *op)      {mutate_binary_operator(this, op, &expr, &stmt);}
void IRMutator::visit(const LE *op)      {mutate_binary_operator(this, op, &expr, &stmt);}
void IRMutator::visit(const GT *op)      {mutate_binary_operator(this, op, &expr, &stmt);}
void IRMutator::visit(const GE *op)      {mutate_binary_operator(this, op, &expr, &stmt);}
void IRMutator::visit(const And *op)     {mutate_binary_operator(this, op, &expr, &stmt);}
void IRMutator::visit(const Or *op)      {mutate_binary_operator(this, op, &expr, &stmt);}

void IRMutator::visit(const Not *op) {
    Expr a = mutate(op->a);
    if (a.same_as(op->a)) expr = op;
    else expr = new Not(a);
}

void IRMutator::visit(const Select *op)  {
    Expr cond = mutate(op->condition);
    Expr t = mutate(op->true_value);
    Expr f = mutate(op->false_value);
    if (cond.same_as(op->condition) && 
        t.same_as(op->true_value) && 
        f.same_as(op->false_value)) expr = op;
    else expr = new Select(cond, t, f);
}

void IRMutator::visit(const Load *op) {
    Expr index = mutate(op->index);
    if (index.same_as(op->index)) expr = op;
    else expr = new Load(op->type, op->name, index, op->image, op->param);
}

void IRMutator::visit(const Ramp *op) {
    Expr base = mutate(op->base);
    Expr stride = mutate(op->stride);
    if (base.same_as(op->base) &&
        stride.same_as(op->stride)) expr = op;
    else expr = new Ramp(base, stride, op->width);
}

void IRMutator::visit(const Broadcast *op) {
    Expr value = mutate(op->value);
    if (value.same_as(op->value)) expr = op;
    else expr = new Broadcast(value, op->width);
}

void IRMutator::visit(const Call *op) {
    vector<Expr > new_args(op->args.size());
    bool changed = false;

    // Mutate the args
    for (size_t i = 0; i < op->args.size(); i++) {
        Expr old_arg = op->args[i];
        Expr new_arg = mutate(old_arg);
        if (!new_arg.same_as(old_arg)) changed = true;
        new_args[i] = new_arg;
    }

    if (!changed) expr = op;
    else expr = new Call(op->type, op->name, new_args, op->call_type, op->func, op->image, op->param);
}

void IRMutator::visit(const Let *op) {
    Expr value = mutate(op->value);
    Expr body = mutate(op->body);
    if (value.same_as(op->value) &&
        body.same_as(op->body)) expr = op;
    else expr = new Let(op->name, value, body);
}

void IRMutator::visit(const LetStmt *op) {
    Expr value = mutate(op->value);
    Stmt body = mutate(op->body);
    if (value.same_as(op->value) &&
        body.same_as(op->body)) stmt = op;
    else stmt = new LetStmt(op->name, value, body);
}

void IRMutator::visit(const PrintStmt *op) {
    vector<Expr > new_args(op->args.size());
    bool args_changed = false;

    // Mutate the args
    for (size_t i = 0; i < op->args.size(); i++) {
        Expr old_arg = op->args[i];
        Expr new_arg = mutate(old_arg);
        if (!new_arg.same_as(old_arg)) args_changed = true;
        new_args[i] = new_arg;
    }

    if (!args_changed) stmt = op;
    else stmt = new PrintStmt(op->prefix, new_args);
}

void IRMutator::visit(const AssertStmt *op) {
    Expr condition = mutate(op->condition);
    if (condition.same_as(op->condition)) stmt = op;
    else stmt = new AssertStmt(condition, op->message);
}

void IRMutator::visit(const Pipeline *op) {
    Stmt produce = mutate(op->produce);
    Stmt update = mutate(op->update);
    Stmt consume = mutate(op->consume);
    if (produce.same_as(op->produce) &&
        update.same_as(op->update) &&
        consume.same_as(op->consume)) {
        stmt = op;
    } else {
        stmt = new Pipeline(op->name, produce, update, consume);
    }
}

void IRMutator::visit(const For *op) {
    Expr min = mutate(op->min);
    Expr extent = mutate(op->extent);
    Stmt body = mutate(op->body);
    if (min.same_as(op->min) &&
        extent.same_as(op->extent) &&
        body.same_as(op->body)) {
        stmt = op;
    } else {
        stmt = new For(op->name, min, extent, op->for_type, body);
    }
}

void IRMutator::visit(const Store *op) {
    Expr value = mutate(op->value);
    Expr index = mutate(op->index);
    if (value.same_as(op->value) && 
        index.same_as(op->index)) stmt = op;
    else stmt = new Store(op->name, value, index);            
}

void IRMutator::visit(const Provide *op) {
    vector<Expr > new_args(op->args.size());
    bool args_changed = false;

    // Mutate the args
    for (size_t i = 0; i < op->args.size(); i++) {
        Expr old_arg = op->args[i];
        Expr new_arg = mutate(old_arg);
        if (!new_arg.same_as(old_arg)) args_changed = true;
        new_args[i] = new_arg;
    }

    Expr value = mutate(op->value);
            
    if (!args_changed && value.same_as(op->value)) stmt = op;
    else stmt = new Provide(op->name, value, new_args);
}

void IRMutator::visit(const Allocate *op) {
    Expr size = mutate(op->size);
    Stmt body = mutate(op->body);
    if (size.same_as(op->size) && body.same_as(op->body)) stmt = op;
    else stmt = new Allocate(op->name, op->type, size, body);
}
        
void IRMutator::visit(const Realize *op) {
    Region new_bounds(op->bounds.size());
    bool bounds_changed = false;

    // Mutate the bounds
    for (size_t i = 0; i < op->bounds.size(); i++) {
        Expr old_min    = op->bounds[i].min;
        Expr old_extent = op->bounds[i].extent;
        Expr new_min    = mutate(old_min);
        Expr new_extent = mutate(old_extent);
        if (!new_min.same_as(old_min))       bounds_changed = true;
        if (!new_extent.same_as(old_extent)) bounds_changed = true;
        new_bounds[i] = Range(new_min, new_extent);
    }

    Stmt body = mutate(op->body);
    if (!bounds_changed && body.same_as(op->body)) stmt = op;
    else stmt = new Realize(op->name, op->type, new_bounds, body);
}

void IRMutator::visit(const Block *op) {
    Stmt first = mutate(op->first);
    Stmt rest = mutate(op->rest);
    if (first.same_as(op->first) && rest.same_as(op->rest)) stmt = op;
    else stmt = new Block(first, rest);
}
}
}
