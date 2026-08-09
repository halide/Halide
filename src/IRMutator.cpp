#include "IRMutator.h"

namespace Halide {
namespace Internal {

using std::vector;

Expr IRMutator::mutate(const Expr &e) {
    return e.defined() ? e.get()->mutate_expr(this) : Expr();
}

Stmt IRMutator::mutate(const Stmt &s) {
    return s.defined() ? s.get()->mutate_stmt(this) : Stmt();
}

Expr IRMutator::visit(const IntImm *op) {
    return op;
}
Expr IRMutator::visit(const UIntImm *op) {
    return op;
}
Expr IRMutator::visit(const FloatImm *op) {
    return op;
}
Expr IRMutator::visit(const StringImm *op) {
    return op;
}
Expr IRMutator::visit(const Variable *op) {
    return op;
}

Expr IRMutator::visit(const Cast *op) {
    Expr value = mutate(op->value);
    if (value.same_as(op->value)) {
        return op;
    }
    return Cast::make(op->type, std::move(value));
}

Expr IRMutator::visit(const Reinterpret *op) {
    Expr value = mutate(op->value);
    if (value.same_as(op->value)) {
        return op;
    }
    return Reinterpret::make(op->type, std::move(value));
}

#define mutate_binary_operator \
    Expr a = mutate(op->a);    \
    Expr b = mutate(op->b);    \
    if (a.same_as(op->a) &&    \
        b.same_as(op->b)) {    \
        return op;             \
    }                          \
    return std::decay_t<decltype(*op)>::make(std::move(a), std::move(b))

Expr IRMutator::visit(const Add *op) {
    mutate_binary_operator;
}
Expr IRMutator::visit(const Sub *op) {
    mutate_binary_operator;
}
Expr IRMutator::visit(const Mul *op) {
    mutate_binary_operator;
}
Expr IRMutator::visit(const Div *op) {
    mutate_binary_operator;
}
Expr IRMutator::visit(const Mod *op) {
    mutate_binary_operator;
}
Expr IRMutator::visit(const Min *op) {
    mutate_binary_operator;
}
Expr IRMutator::visit(const Max *op) {
    mutate_binary_operator;
}
Expr IRMutator::visit(const EQ *op) {
    mutate_binary_operator;
}
Expr IRMutator::visit(const NE *op) {
    mutate_binary_operator;
}
Expr IRMutator::visit(const LT *op) {
    mutate_binary_operator;
}
Expr IRMutator::visit(const LE *op) {
    mutate_binary_operator;
}
Expr IRMutator::visit(const GT *op) {
    mutate_binary_operator;
}
Expr IRMutator::visit(const GE *op) {
    mutate_binary_operator;
}
Expr IRMutator::visit(const And *op) {
    mutate_binary_operator;
}
Expr IRMutator::visit(const Or *op) {
    mutate_binary_operator;
}

Expr IRMutator::visit(const Not *op) {
    Expr a = mutate(op->a);
    if (a.same_as(op->a)) {
        return op;
    }
    return Not::make(std::move(a));
}

Expr IRMutator::visit(const Select *op) {
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

Expr IRMutator::visit(const Load *op) {
    Expr predicate = mutate(op->predicate);
    Expr index = mutate(op->index);
    return op->with(index, predicate, op->alignment);
}

Expr IRMutator::visit(const Ramp *op) {
    Expr base = mutate(op->base);
    Expr stride = mutate(op->stride);
    if (base.same_as(op->base) &&
        stride.same_as(op->stride)) {
        return op;
    }
    return Ramp::make(std::move(base), std::move(stride), op->lanes);
}

Expr IRMutator::visit(const Broadcast *op) {
    Expr value = mutate(op->value);
    if (value.same_as(op->value)) {
        return op;
    }
    return Broadcast::make(std::move(value), op->lanes);
}

Expr IRMutator::visit(const Call *op) {
    return op->with(mutate_with_changes(op->args).first);
}

Expr IRMutator::visit(const Let *op) {
    Expr value = mutate(op->value);
    Expr body = mutate(op->body);
    return op->with(value, body);
}

Stmt IRMutator::visit(const LetStmt *op) {
    Expr value = mutate(op->value);
    Stmt body = mutate(op->body);
    return op->with(value, body);
}

Stmt IRMutator::visit(const AssertStmt *op) {
    Expr condition = mutate(op->condition);
    Expr message = mutate(op->message);
    return op->with(condition, message);
}

Stmt IRMutator::visit(const ProducerConsumer *op) {
    Stmt body = mutate(op->body);
    return op->with(body);
}

Stmt IRMutator::visit(const For *op) {
    Expr min = mutate(op->min);
    Expr max = mutate(op->max);
    Stmt body = mutate(op->body);
    return op->with(min, max, body);
}

Stmt IRMutator::visit(const Store *op) {
    Expr predicate = mutate(op->predicate);
    Expr value = mutate(op->value);
    Expr index = mutate(op->index);
    return op->with(value, index, predicate, op->alignment);
}

Stmt IRMutator::visit(const Provide *op) {
    // Mutate the args
    std::vector<Expr> new_args = mutate_with_changes(op->args).first;
    std::vector<Expr> new_values = mutate_with_changes(op->values).first;
    Expr new_predicate = mutate(op->predicate);
    return op->with(new_values, new_args, new_predicate);
}

Stmt IRMutator::visit(const Allocate *op) {
    auto [new_extents, changed] = mutate_with_changes(op->extents);
    Stmt body = mutate(op->body);
    Expr condition = mutate(op->condition);
    Expr new_expr;
    if (op->new_expr.defined()) {
        new_expr = mutate(op->new_expr);
    }
    if (!changed &&
        body.same_as(op->body) &&
        condition.same_as(op->condition) &&
        new_expr.same_as(op->new_expr)) {
        return op;
    }
    return Allocate::make(op->name, op->type, op->memory_type,
                          new_extents, std::move(condition),
                          std::move(body), std::move(new_expr), op->free_function, op->padding);
}

Stmt IRMutator::visit(const Free *op) {
    return op;
}

Stmt IRMutator::visit(const Realize *op) {
    // Mutate the bounds
    Region new_bounds = mutate_region(this, op->bounds).first;

    Stmt body = mutate(op->body);
    Expr condition = mutate(op->condition);
    return op->with(new_bounds, condition, body);
}

Stmt IRMutator::visit(const Prefetch *op) {
    Stmt body = mutate(op->body);
    Expr condition = mutate(op->condition);

    // Mutate the bounds
    Region new_bounds = mutate_region(this, op->bounds).first;
    return op->with(new_bounds, condition, body);
}

Stmt IRMutator::visit(const Block *op) {
    Stmt first = mutate(op->first);
    Stmt rest = mutate(op->rest);
    return op->with(first, rest);
}

Stmt IRMutator::visit(const IfThenElse *op) {
    Expr condition = mutate(op->condition);
    Stmt then_case = mutate(op->then_case);
    Stmt else_case = mutate(op->else_case);
    return op->with(condition, then_case, else_case);
}

Stmt IRMutator::visit(const Evaluate *op) {
    return op->with(mutate(op->value));
}

Expr IRMutator::visit(const Shuffle *op) {
    auto [new_vectors, changed] = mutate_with_changes(op->vectors);
    if (!changed) {
        return op;
    }
    return Shuffle::make(new_vectors, op->indices);
}

Expr IRMutator::visit(const VectorReduce *op) {
    Expr value = mutate(op->value);
    if (value.same_as(op->value)) {
        return op;
    }
    return VectorReduce::make(op->op, std::move(value), op->type.lanes());
}

Stmt IRMutator::visit(const Fork *op) {
    Stmt first = mutate(op->first);
    Stmt rest = mutate(op->rest);
    return op->with(first, rest);
}

Stmt IRMutator::visit(const Acquire *op) {
    Expr sema = mutate(op->semaphore);
    Expr count = mutate(op->count);
    Stmt body = mutate(op->body);
    return op->with(sema, count, body);
}

Stmt IRMutator::visit(const Atomic *op) {
    Stmt body = mutate(op->body);
    return op->with(body);
}

Stmt IRMutator::visit(const StreamingStore *op) {
    Stmt body = mutate(op->body);
    if (body.same_as(op->body)) {
        return op;
    } else {
        return StreamingStore::make(op->producer_name, std::move(body));
    }
}

Stmt IRMutator::visit(const StreamingLoads *op) {
    Stmt body = mutate(op->body);
    if (body.same_as(op->body)) {
        return op;
    } else {
        return StreamingLoads::make(op->names, std::move(body));
    }
}

Stmt IRMutator::visit(const HoistedStorage *op) {
    Stmt body = mutate(op->body);
    return op->with(body);
}

Stmt IRGraphMutator::mutate(const Stmt &s) {
    if (s.is_sole_reference()) {
        // There's no point in caching mutations of this Stmt. We can never
        // possibly see it again, and it can't be in the cache already if this
        // is the sole reference. Doing this here and in the Expr mutate method
        // below speeds up lowering by about 5%
        return IRMutator::mutate(s);
    } else {
        auto p = stmt_replacements.emplace(s, Stmt());
        if (p.second) {
            // N.B: Inserting into a map (as the recursive mutate call
            // does), does not invalidate existing iterators.
            p.first->second = IRMutator::mutate(s);
        }
        return p.first->second;
    }
}

Expr IRGraphMutator::mutate(const Expr &e) {
    if (e.is_sole_reference()) {
        return IRMutator::mutate(e);
    } else {
        auto p = expr_replacements.emplace(e, Expr());
        if (p.second) {
            p.first->second = IRMutator::mutate(e);
        }
        return p.first->second;
    }
}

std::pair<std::vector<Expr>, bool> IRMutator::mutate_with_changes(const std::vector<Expr> &old_exprs) {
    vector<Expr> new_exprs(old_exprs.size());
    bool changed = false;

    // Mutate the args
    for (size_t i = 0; i < old_exprs.size(); i++) {
        const Expr &old_e = old_exprs[i];
        Expr new_e = mutate(old_e);
        if (!new_e.same_as(old_e)) {
            changed = true;
        }
        new_exprs[i] = std::move(new_e);
    }

    return {std::move(new_exprs), changed};
}

}  // namespace Internal
}  // namespace Halide
