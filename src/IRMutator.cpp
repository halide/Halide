#include "IRMutator.h"

namespace Halide {
namespace Internal {

using std::pair;
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

namespace {
template<typename T>
void mutate_binary_operator(IRMutator *mutator, const T *op, Expr *expr, Stmt *stmt) {
    Expr a = mutator->mutate(op->a);
    Expr b = mutator->mutate(op->b);
    if (a.same_as(op->a) &&
        b.same_as(op->b)) {
        *expr = op;
    } else {
        *expr = T::make(a, b);
    }
    *stmt = nullptr;
}

pair<Region, bool> mutate_region(IRMutator *mutator, const Region &bounds) {
    Region new_bounds(bounds.size());
    bool bounds_changed = false;

    for (size_t i = 0; i < bounds.size(); i++) {
        Expr old_min = bounds[i].min;
        Expr old_extent = bounds[i].extent;
        Expr new_min = mutator->mutate(old_min);
        Expr new_extent = mutator->mutate(old_extent);
        if (!new_min.same_as(old_min)) {
            bounds_changed = true;
        }
        if (!new_extent.same_as(old_extent)) {
            bounds_changed = true;
        }
        new_bounds[i] = Range(new_min, new_extent);
    }
    return {new_bounds, bounds_changed};
}

}

void IRMutator::visit(const IntImm *op)   {expr = op;}
void IRMutator::visit(const UIntImm *op)   {expr = op;}
void IRMutator::visit(const FloatImm *op) {expr = op;}
void IRMutator::visit(const StringImm *op) {expr = op;}
void IRMutator::visit(const Variable *op) {expr = op;}

void IRMutator::visit(const Cast *op) {
    Expr value = mutate(op->value);
    if (value.same_as(op->value)) {
        expr = op;
    } else {
        expr = Cast::make(op->type, value);
    }
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
    else expr = Not::make(a);
}

void IRMutator::visit(const Select *op)  {
    Expr cond = mutate(op->condition);
    Expr t = mutate(op->true_value);
    Expr f = mutate(op->false_value);
    if (cond.same_as(op->condition) &&
        t.same_as(op->true_value) &&
        f.same_as(op->false_value)) {
        expr = op;
    } else {
        expr = Select::make(cond, t, f);
    }
}

void IRMutator::visit(const Load *op) {
    Expr predicate = mutate(op->predicate);
    Expr index = mutate(op->index);
    if (predicate.same_as(op->predicate) && index.same_as(op->index)) {
        expr = op;
    } else {
        expr = Load::make(op->type, op->name, index, op->image, op->param, predicate);
    }
}

void IRMutator::visit(const Ramp *op) {
    Expr base = mutate(op->base);
    Expr stride = mutate(op->stride);
    if (base.same_as(op->base) &&
        stride.same_as(op->stride)) {
        expr = op;
    } else {
        expr = Ramp::make(base, stride, op->lanes);
    }
}

void IRMutator::visit(const Broadcast *op) {
    Expr value = mutate(op->value);
    if (value.same_as(op->value)) expr = op;
    else expr = Broadcast::make(value, op->lanes);
}

void IRMutator::visit(const Call *op) {
    vector<Expr> new_args(op->args.size());
    bool changed = false;

    // Mutate the args
    for (size_t i = 0; i < op->args.size(); i++) {
        Expr old_arg = op->args[i];
        Expr new_arg = mutate(old_arg);
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

void IRMutator::visit(const Let *op) {
    Expr value = mutate(op->value);
    Expr body = mutate(op->body);
    if (value.same_as(op->value) &&
        body.same_as(op->body)) {
        expr = op;
    } else {
        expr = Let::make(op->name, value, body);
    }
}

void IRMutator::visit(const LetStmt *op) {
    Expr value = mutate(op->value);
    Stmt body = mutate(op->body);
    if (value.same_as(op->value) &&
        body.same_as(op->body)) {
        stmt = op;
    } else {
        stmt = LetStmt::make(op->name, value, body);
    }
}

void IRMutator::visit(const AssertStmt *op) {
    Expr condition = mutate(op->condition);
    Expr message = mutate(op->message);

    if (condition.same_as(op->condition) && message.same_as(op->message)) {
        stmt = op;
    } else {
        stmt = AssertStmt::make(condition, message);
    }
}

void IRMutator::visit(const ProducerConsumer *op) {
    Stmt body = mutate(op->body);
    if (body.same_as(op->body)) {
        stmt = op;
    } else {
        stmt = ProducerConsumer::make(op->name, op->is_producer, body);
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
        stmt = For::make(op->name, min, extent, op->for_type, op->device_api, body);
    }
}

void IRMutator::visit(const Store *op) {
    Expr predicate = mutate(op->predicate);
    Expr value = mutate(op->value);
    Expr index = mutate(op->index);
    if (predicate.same_as(op->predicate) && value.same_as(op->value) && index.same_as(op->index)) {
        stmt = op;
    } else {
        stmt = Store::make(op->name, value, index, op->param, predicate);
    }
}

void IRMutator::visit(const Provide *op) {
    vector<Expr> new_args(op->args.size());
    vector<Expr> new_values(op->values.size());
    bool changed = false;

    // Mutate the args
    for (size_t i = 0; i < op->args.size(); i++) {
        Expr old_arg = op->args[i];
        Expr new_arg = mutate(old_arg);
        if (!new_arg.same_as(old_arg)) changed = true;
        new_args[i] = new_arg;
    }

    for (size_t i = 0; i < op->values.size(); i++) {
        Expr old_value = op->values[i];
        Expr new_value = mutate(old_value);
        if (!new_value.same_as(old_value)) changed = true;
        new_values[i] = new_value;
    }

    if (!changed) {
        stmt = op;
    } else {
        stmt = Provide::make(op->name, new_values, new_args);
    }
}

void IRMutator::visit(const Allocate *op) {
    std::vector<Expr> new_extents;
    bool all_extents_unmodified = true;
    for (size_t i = 0; i < op->extents.size(); i++) {
        new_extents.push_back(mutate(op->extents[i]));
        all_extents_unmodified &= new_extents[i].same_as(op->extents[i]);
    }
    Stmt body = mutate(op->body);
    Expr condition = mutate(op->condition);
    Expr new_expr;
    if (op->new_expr.defined()) {
        new_expr = mutate(op->new_expr);
    }
    if (all_extents_unmodified &&
        body.same_as(op->body) &&
        condition.same_as(op->condition) &&
        new_expr.same_as(op->new_expr)) {
        stmt = op;
    } else {
        stmt = Allocate::make(op->name, op->type, new_extents, condition, body, new_expr, op->free_function);
    }
}

void IRMutator::visit(const Free *op) {
    stmt = op;
}

void IRMutator::visit(const Realize *op) {
    Region new_bounds;
    bool bounds_changed;

    // Mutate the bounds
    std::tie(new_bounds, bounds_changed) = mutate_region(this, op->bounds);

    Stmt body = mutate(op->body);
    Expr condition = mutate(op->condition);
    if (!bounds_changed &&
        body.same_as(op->body) &&
        condition.same_as(op->condition)) {
        stmt = op;
    } else {
        stmt = Realize::make(op->name, op->types, new_bounds,
                             condition, body);
    }
}

void IRMutator::visit(const Prefetch *op) {
    Region new_bounds;
    bool bounds_changed;

    // Mutate the bounds
    std::tie(new_bounds, bounds_changed) = mutate_region(this, op->bounds);

    if (!bounds_changed) {
        stmt = op;
    } else {
        stmt = Prefetch::make(op->name, op->types, new_bounds, op->param);
    }
}

void IRMutator::visit(const Block *op) {
    Stmt first = mutate(op->first);
    Stmt rest = mutate(op->rest);
    if (first.same_as(op->first) &&
        rest.same_as(op->rest)) {
        stmt = op;
    } else {
        stmt = Block::make(first, rest);
    }
}

void IRMutator::visit(const IfThenElse *op) {
    Expr condition = mutate(op->condition);
    Stmt then_case = mutate(op->then_case);
    Stmt else_case = mutate(op->else_case);
    if (condition.same_as(op->condition) &&
        then_case.same_as(op->then_case) &&
        else_case.same_as(op->else_case)) {
        stmt = op;
    } else {
        stmt = IfThenElse::make(condition, then_case, else_case);
    }
}

void IRMutator::visit(const Evaluate *op) {
    Expr v = mutate(op->value);
    if (v.same_as(op->value)) {
        stmt = op;
    } else {
        stmt = Evaluate::make(v);
    }
}

void IRMutator::visit(const Shuffle *op) {
    vector<Expr > new_vectors(op->vectors.size());
    bool changed = false;

    for (size_t i = 0; i < op->vectors.size(); i++) {
        Expr old_vector = op->vectors[i];
        Expr new_vector = mutate(old_vector);
        if (!new_vector.same_as(old_vector)) changed = true;
        new_vectors[i] = new_vector;
    }

    if (!changed) {
        expr = op;
    } else {
        expr = Shuffle::make(new_vectors, op->indices);
    }
}


Stmt IRGraphMutator::mutate(Stmt s) {
    auto iter = stmt_replacements.find(s);
    if (iter != stmt_replacements.end()) {
        return iter->second;
    }
    Stmt new_s = IRMutator::mutate(s);
    stmt_replacements[s] = new_s;
    return new_s;
}

Expr IRGraphMutator::mutate(Expr e) {
    auto iter = expr_replacements.find(e);
    if (iter != expr_replacements.end()) {
        return iter->second;
    }
    Expr new_e = IRMutator::mutate(e);
    expr_replacements[e] = new_e;
    return new_e;
}

}
}
