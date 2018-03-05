#include "IRMutator.h"

namespace Halide {
namespace Internal {

using std::pair;
using std::vector;

IRMutator::IRMutator() {
}

IRMutator::~IRMutator() {
}

Expr IRMutator::mutate(const Expr &e) {
    if (e.defined()) {
        e.accept(this);
    } else {
        expr = Expr();
    }
    stmt = Stmt();
    return std::move(expr);
}

Stmt IRMutator::mutate(const Stmt &s) {
    if (s.defined()) {
        s.accept(this);
    } else {
        stmt = Stmt();
    }
    expr = Expr();
    return std::move(stmt);
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
        *expr = T::make(std::move(a), std::move(b));
    }
    *stmt = nullptr;
}

template<typename Mutator>
pair<Region, bool> mutate_region(Mutator *mutator, const Region &bounds) {
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
        expr = Cast::make(op->type, std::move(value));
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
    if (a.same_as(op->a)) {
        expr = op;
    } else {
        expr = Not::make(std::move(a));
    }
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
        expr = Select::make(std::move(cond), std::move(t), std::move(f));
    }
}

void IRMutator::visit(const Load *op) {
    Expr predicate = mutate(op->predicate);
    Expr index = mutate(op->index);
    if (predicate.same_as(op->predicate) && index.same_as(op->index)) {
        expr = op;
    } else {
        expr = Load::make(op->type, op->name, std::move(index),
                          op->image, op->param, std::move(predicate));
    }
}

void IRMutator::visit(const Ramp *op) {
    Expr base = mutate(op->base);
    Expr stride = mutate(op->stride);
    if (base.same_as(op->base) &&
        stride.same_as(op->stride)) {
        expr = op;
    } else {
        expr = Ramp::make(std::move(base), std::move(stride), op->lanes);
    }
}

void IRMutator::visit(const Broadcast *op) {
    Expr value = mutate(op->value);
    if (value.same_as(op->value)) {
        expr = op;
    } else {
        expr = Broadcast::make(std::move(value), op->lanes);
    }
}

void IRMutator::visit(const Call *op) {
    vector<Expr> new_args(op->args.size());
    bool changed = false;

    // Mutate the args
    for (size_t i = 0; i < op->args.size(); i++) {
        const Expr &old_arg = op->args[i];
        Expr new_arg = mutate(old_arg);
        if (!new_arg.same_as(old_arg)) changed = true;
        new_args[i] = std::move(new_arg);
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
        expr = Let::make(op->name, std::move(value), std::move(body));
    }
}

void IRMutator::visit(const LetStmt *op) {
    Expr value = mutate(op->value);
    Stmt body = mutate(op->body);
    if (value.same_as(op->value) &&
        body.same_as(op->body)) {
        stmt = op;
    } else {
        stmt = LetStmt::make(op->name, std::move(value), std::move(body));
    }
}

void IRMutator::visit(const AssertStmt *op) {
    Expr condition = mutate(op->condition);
    Expr message = mutate(op->message);

    if (condition.same_as(op->condition) && message.same_as(op->message)) {
        stmt = op;
    } else {
        stmt = AssertStmt::make(std::move(condition), std::move(message));
    }
}

void IRMutator::visit(const ProducerConsumer *op) {
    Stmt body = mutate(op->body);
    if (body.same_as(op->body)) {
        stmt = op;
    } else {
        stmt = ProducerConsumer::make(op->name, op->is_producer, std::move(body));
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
        stmt = For::make(op->name, std::move(min), std::move(extent),
                         op->for_type, op->device_api, std::move(body));
    }
}

void IRMutator::visit(const Store *op) {
    Expr predicate = mutate(op->predicate);
    Expr value = mutate(op->value);
    Expr index = mutate(op->index);
    if (predicate.same_as(op->predicate) && value.same_as(op->value) && index.same_as(op->index)) {
        stmt = op;
    } else {
        stmt = Store::make(op->name, std::move(value), std::move(index), op->param, std::move(predicate));
    }
}

void IRMutator::visit(const Provide *op) {
    vector<Expr> new_args(op->args.size());
    vector<Expr> new_values(op->values.size());
    bool changed = false;

    // Mutate the args
    for (size_t i = 0; i < op->args.size(); i++) {
        const Expr &old_arg = op->args[i];
        Expr new_arg = mutate(old_arg);
        if (!new_arg.same_as(old_arg)) changed = true;
        new_args[i] = new_arg;
    }

    for (size_t i = 0; i < op->values.size(); i++) {
        const Expr &old_value = op->values[i];
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
        stmt = Allocate::make(op->name, op->type, op->memory_type, new_extents, std::move(condition),
                              std::move(body), std::move(new_expr), op->free_function);
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
        stmt = Realize::make(op->name, op->types, op->memory_type, new_bounds,
                             std::move(condition), std::move(body));
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
        stmt = Block::make(std::move(first), std::move(rest));
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
        stmt = IfThenElse::make(std::move(condition), std::move(then_case), std::move(else_case));
    }
}

void IRMutator::visit(const Evaluate *op) {
    Expr v = mutate(op->value);
    if (v.same_as(op->value)) {
        stmt = op;
    } else {
        stmt = Evaluate::make(std::move(v));
    }
}

void IRMutator::visit(const Shuffle *op) {
    vector<Expr> new_vectors(op->vectors.size());
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


IRMutator2::IRMutator2() {
}

IRMutator2::~IRMutator2() {
}

Expr IRMutator2::mutate(const Expr &e) {
    return e.defined() ? ((const BaseExprNode *)e.get())->mutate_expr(this) : Expr();
}

Stmt IRMutator2::mutate(const Stmt &s) {
    return s.defined() ? ((const BaseStmtNode *)s.get())->mutate_stmt(this) : Stmt();
}

Expr IRMutator2::visit(const IntImm *op)   {return op;}
Expr IRMutator2::visit(const UIntImm *op)   {return op;}
Expr IRMutator2::visit(const FloatImm *op) {return op;}
Expr IRMutator2::visit(const StringImm *op) {return op;}
Expr IRMutator2::visit(const Variable *op) {return op;}

Expr IRMutator2::visit(const Cast *op) {
    Expr value = mutate(op->value);
    if (value.same_as(op->value)) {
        return op;
    }
    return Cast::make(op->type, std::move(value));
}

namespace {
template<typename T>
Expr mutate_binary_operator(IRMutator2 *mutator, const T *op) {
    Expr a = mutator->mutate(op->a);
    Expr b = mutator->mutate(op->b);
    if (a.same_as(op->a) &&
        b.same_as(op->b)) {
        return op;
    }
    return T::make(std::move(a), std::move(b));
}
}  // namespace

Expr IRMutator2::visit(const Add *op)     {return mutate_binary_operator(this, op);}
Expr IRMutator2::visit(const Sub *op)     {return mutate_binary_operator(this, op);}
Expr IRMutator2::visit(const Mul *op)     {return mutate_binary_operator(this, op);}
Expr IRMutator2::visit(const Div *op)     {return mutate_binary_operator(this, op);}
Expr IRMutator2::visit(const Mod *op)     {return mutate_binary_operator(this, op);}
Expr IRMutator2::visit(const Min *op)     {return mutate_binary_operator(this, op);}
Expr IRMutator2::visit(const Max *op)     {return mutate_binary_operator(this, op);}
Expr IRMutator2::visit(const EQ *op)      {return mutate_binary_operator(this, op);}
Expr IRMutator2::visit(const NE *op)      {return mutate_binary_operator(this, op);}
Expr IRMutator2::visit(const LT *op)      {return mutate_binary_operator(this, op);}
Expr IRMutator2::visit(const LE *op)      {return mutate_binary_operator(this, op);}
Expr IRMutator2::visit(const GT *op)      {return mutate_binary_operator(this, op);}
Expr IRMutator2::visit(const GE *op)      {return mutate_binary_operator(this, op);}
Expr IRMutator2::visit(const And *op)     {return mutate_binary_operator(this, op);}
Expr IRMutator2::visit(const Or *op)      {return mutate_binary_operator(this, op);}

Expr IRMutator2::visit(const Not *op) {
    Expr a = mutate(op->a);
    if (a.same_as(op->a)) {
        return op;
    }
    return Not::make(std::move(a));
}

Expr IRMutator2::visit(const Select *op)  {
    Expr cond = mutate(op->condition);
    Expr t = mutate(op->true_value);
    Expr f = mutate(op->false_value);
    if (cond.same_as(op->condition) &&
        t.same_as(op->true_value) &&
        f.same_as(op->false_value)) {
        return op;
    }
    return Select::make(std::move(cond), std::move(t), std::move(f));
}

Expr IRMutator2::visit(const Load *op) {
    Expr predicate = mutate(op->predicate);
    Expr index = mutate(op->index);
    if (predicate.same_as(op->predicate) && index.same_as(op->index)) {
        return op;
    }
    return Load::make(op->type, op->name, std::move(index),
                          op->image, op->param, std::move(predicate));
}

Expr IRMutator2::visit(const Ramp *op) {
    Expr base = mutate(op->base);
    Expr stride = mutate(op->stride);
    if (base.same_as(op->base) &&
        stride.same_as(op->stride)) {
        return op;
    }
    return Ramp::make(std::move(base), std::move(stride), op->lanes);
}

Expr IRMutator2::visit(const Broadcast *op) {
    Expr value = mutate(op->value);
    if (value.same_as(op->value)) {
        return op;
    }
    return Broadcast::make(std::move(value), op->lanes);
}

Expr IRMutator2::visit(const Call *op) {
    vector<Expr> new_args(op->args.size());
    bool changed = false;

    // Mutate the args
    for (size_t i = 0; i < op->args.size(); i++) {
        const Expr &old_arg = op->args[i];
        Expr new_arg = mutate(old_arg);
        if (!new_arg.same_as(old_arg)) changed = true;
        new_args[i] = std::move(new_arg);
    }

    if (!changed) {
        return op;
    }
    return Call::make(op->type, op->name, new_args, op->call_type,
                          op->func, op->value_index, op->image, op->param);
}

Expr IRMutator2::visit(const Let *op) {
    Expr value = mutate(op->value);
    Expr body = mutate(op->body);
    if (value.same_as(op->value) &&
        body.same_as(op->body)) {
        return op;
    }
    return Let::make(op->name, std::move(value), std::move(body));
}

Stmt IRMutator2::visit(const LetStmt *op) {
    Expr value = mutate(op->value);
    Stmt body = mutate(op->body);
    if (value.same_as(op->value) &&
        body.same_as(op->body)) {
        return op;
    }
    return LetStmt::make(op->name, std::move(value), std::move(body));
}

Stmt IRMutator2::visit(const AssertStmt *op) {
    Expr condition = mutate(op->condition);
    Expr message = mutate(op->message);

    if (condition.same_as(op->condition) && message.same_as(op->message)) {
        return op;
    }
    return AssertStmt::make(std::move(condition), std::move(message));
}

Stmt IRMutator2::visit(const ProducerConsumer *op) {
    Stmt body = mutate(op->body);
    if (body.same_as(op->body)) {
        return op;
    }
    return ProducerConsumer::make(op->name, op->is_producer, std::move(body));
}

Stmt IRMutator2::visit(const For *op) {
    Expr min = mutate(op->min);
    Expr extent = mutate(op->extent);
    Stmt body = mutate(op->body);
    if (min.same_as(op->min) &&
        extent.same_as(op->extent) &&
        body.same_as(op->body)) {
        return op;
    }
    return For::make(op->name, std::move(min), std::move(extent),
                         op->for_type, op->device_api, std::move(body));
}

Stmt IRMutator2::visit(const Store *op) {
    Expr predicate = mutate(op->predicate);
    Expr value = mutate(op->value);
    Expr index = mutate(op->index);
    if (predicate.same_as(op->predicate) && value.same_as(op->value) && index.same_as(op->index)) {
        return op;
    }
    return Store::make(op->name, std::move(value), std::move(index), op->param, std::move(predicate));
}

Stmt IRMutator2::visit(const Provide *op) {
    vector<Expr> new_args(op->args.size());
    vector<Expr> new_values(op->values.size());
    bool changed = false;

    // Mutate the args
    for (size_t i = 0; i < op->args.size(); i++) {
        const Expr &old_arg = op->args[i];
        Expr new_arg = mutate(old_arg);
        if (!new_arg.same_as(old_arg)) changed = true;
        new_args[i] = new_arg;
    }

    for (size_t i = 0; i < op->values.size(); i++) {
        const Expr &old_value = op->values[i];
        Expr new_value = mutate(old_value);
        if (!new_value.same_as(old_value)) changed = true;
        new_values[i] = new_value;
    }

    if (!changed) {
        return op;
    }
    return Provide::make(op->name, new_values, new_args);
}

Stmt IRMutator2::visit(const Allocate *op) {
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
        return op;
    }
    return Allocate::make(op->name, op->type, op->memory_type,
                          new_extents, std::move(condition),
                          std::move(body), std::move(new_expr), op->free_function);
}

Stmt IRMutator2::visit(const Free *op) {
    return op;
}

Stmt IRMutator2::visit(const Realize *op) {
    Region new_bounds;
    bool bounds_changed;

    // Mutate the bounds
    std::tie(new_bounds, bounds_changed) = mutate_region(this, op->bounds);

    Stmt body = mutate(op->body);
    Expr condition = mutate(op->condition);
    if (!bounds_changed &&
        body.same_as(op->body) &&
        condition.same_as(op->condition)) {
        return op;
    }
    return Realize::make(op->name, op->types, op->memory_type, new_bounds,
                         std::move(condition), std::move(body));
}

Stmt IRMutator2::visit(const Prefetch *op) {
    Region new_bounds;
    bool bounds_changed;

    // Mutate the bounds
    std::tie(new_bounds, bounds_changed) = mutate_region(this, op->bounds);

    if (!bounds_changed) {
        return op;
    }
    return Prefetch::make(op->name, op->types, new_bounds, op->param);
}

Stmt IRMutator2::visit(const Block *op) {
    Stmt first = mutate(op->first);
    Stmt rest = mutate(op->rest);
    if (first.same_as(op->first) &&
        rest.same_as(op->rest)) {
        return op;
    }
    return Block::make(std::move(first), std::move(rest));
}

Stmt IRMutator2::visit(const IfThenElse *op) {
    Expr condition = mutate(op->condition);
    Stmt then_case = mutate(op->then_case);
    Stmt else_case = mutate(op->else_case);
    if (condition.same_as(op->condition) &&
        then_case.same_as(op->then_case) &&
        else_case.same_as(op->else_case)) {
        return op;
    }
    return IfThenElse::make(std::move(condition), std::move(then_case), std::move(else_case));
}

Stmt IRMutator2::visit(const Evaluate *op) {
    Expr v = mutate(op->value);
    if (v.same_as(op->value)) {
        return op;
    }
    return Evaluate::make(std::move(v));
}

Expr IRMutator2::visit(const Shuffle *op) {
    vector<Expr> new_vectors(op->vectors.size());
    bool changed = false;

    for (size_t i = 0; i < op->vectors.size(); i++) {
        Expr old_vector = op->vectors[i];
        Expr new_vector = mutate(old_vector);
        if (!new_vector.same_as(old_vector)) changed = true;
        new_vectors[i] = new_vector;
    }

    if (!changed) {
        return op;
    }
    return Shuffle::make(new_vectors, op->indices);
}

Stmt IRGraphMutator2::mutate(const Stmt &s) {
    auto iter = stmt_replacements.find(s);
    if (iter != stmt_replacements.end()) {
        return iter->second;
    }
    Stmt new_s = IRMutator2::mutate(s);
    stmt_replacements[s] = new_s;
    return new_s;
}

Expr IRGraphMutator2::mutate(const Expr &e) {
    auto iter = expr_replacements.find(e);
    if (iter != expr_replacements.end()) {
        return iter->second;
    }
    Expr new_e = IRMutator2::mutate(e);
    expr_replacements[e] = new_e;
    return new_e;
}

}  // namespace Internal
}  // namespace Halide
