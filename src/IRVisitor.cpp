#include "IRVisitor.h"

#include "ExternFuncArgument.h"
#include "Function.h"

namespace Halide {
namespace Internal {

void IRVisitor::visit(const IntImm *) {
}

void IRVisitor::visit(const UIntImm *) {
}

void IRVisitor::visit(const FloatImm *) {
}

void IRVisitor::visit(const StringImm *) {
}

void IRVisitor::visit(const Cast *op) {
    op->value.accept(this);
}

void IRVisitor::visit(const Reinterpret *op) {
    op->value.accept(this);
}

void IRVisitor::visit(const Variable *) {
}

void IRVisitor::visit(const Add *op) {
    op->a.accept(this);
    op->b.accept(this);
}

void IRVisitor::visit(const Sub *op) {
    op->a.accept(this);
    op->b.accept(this);
}

void IRVisitor::visit(const Mul *op) {
    op->a.accept(this);
    op->b.accept(this);
}

void IRVisitor::visit(const Div *op) {
    op->a.accept(this);
    op->b.accept(this);
}

void IRVisitor::visit(const Mod *op) {
    op->a.accept(this);
    op->b.accept(this);
}

void IRVisitor::visit(const Min *op) {
    op->a.accept(this);
    op->b.accept(this);
}

void IRVisitor::visit(const Max *op) {
    op->a.accept(this);
    op->b.accept(this);
}

void IRVisitor::visit(const EQ *op) {
    op->a.accept(this);
    op->b.accept(this);
}

void IRVisitor::visit(const NE *op) {
    op->a.accept(this);
    op->b.accept(this);
}

void IRVisitor::visit(const LT *op) {
    op->a.accept(this);
    op->b.accept(this);
}

void IRVisitor::visit(const LE *op) {
    op->a.accept(this);
    op->b.accept(this);
}

void IRVisitor::visit(const GT *op) {
    op->a.accept(this);
    op->b.accept(this);
}

void IRVisitor::visit(const GE *op) {
    op->a.accept(this);
    op->b.accept(this);
}

void IRVisitor::visit(const And *op) {
    op->a.accept(this);
    op->b.accept(this);
}

void IRVisitor::visit(const Or *op) {
    op->a.accept(this);
    op->b.accept(this);
}

void IRVisitor::visit(const Not *op) {
    op->a.accept(this);
}

void IRVisitor::visit(const Select *op) {
    op->condition.accept(this);
    op->true_value.accept(this);
    op->false_value.accept(this);
}

void IRVisitor::visit(const Load *op) {
    op->predicate.accept(this);
    op->index.accept(this);
}

void IRVisitor::visit(const Ramp *op) {
    op->base.accept(this);
    op->stride.accept(this);
}

void IRVisitor::visit(const Broadcast *op) {
    op->value.accept(this);
}

void IRVisitor::visit(const Call *op) {
    for (const auto &arg : op->args) {
        arg.accept(this);
    }

    // Consider extern call args
    if (op->func.defined()) {
        Function f(op->func);
        if (op->call_type == Call::Halide && f.has_extern_definition()) {
            for (const auto &arg : f.extern_arguments()) {
                if (arg.is_expr()) {
                    arg.expr.accept(this);
                }
            }
        }
    }
}

void IRVisitor::visit(const Let *op) {
    op->value.accept(this);
    op->body.accept(this);
}

void IRVisitor::visit(const LetStmt *op) {
    op->value.accept(this);
    op->body.accept(this);
}

void IRVisitor::visit(const AssertStmt *op) {
    op->condition.accept(this);
    op->message.accept(this);
}

void IRVisitor::visit(const ProducerConsumer *op) {
    op->body.accept(this);
}

void IRVisitor::visit(const For *op) {
    op->min.accept(this);
    op->extent.accept(this);
    op->body.accept(this);
}

void IRVisitor::visit(const Acquire *op) {
    op->semaphore.accept(this);
    op->count.accept(this);
    op->body.accept(this);
}

void IRVisitor::visit(const Store *op) {
    op->predicate.accept(this);
    op->value.accept(this);
    op->index.accept(this);
}

void IRVisitor::visit(const Provide *op) {
    op->predicate.accept(this);
    for (const auto &value : op->values) {
        value.accept(this);
    }
    for (const auto &arg : op->args) {
        arg.accept(this);
    }
}

void IRVisitor::visit(const Allocate *op) {
    for (const auto &extent : op->extents) {
        extent.accept(this);
    }
    op->condition.accept(this);
    if (op->new_expr.defined()) {
        op->new_expr.accept(this);
    }
    op->body.accept(this);
}

void IRVisitor::visit(const Free *op) {
}

void IRVisitor::visit(const Realize *op) {
    for (const auto &bound : op->bounds) {
        bound.min.accept(this);
        bound.extent.accept(this);
    }
    op->condition.accept(this);
    op->body.accept(this);
}

void IRVisitor::visit(const Prefetch *op) {
    for (const auto &bound : op->bounds) {
        bound.min.accept(this);
        bound.extent.accept(this);
    }
    op->condition.accept(this);
    op->body.accept(this);
}

void IRVisitor::visit(const Block *op) {
    op->first.accept(this);
    if (op->rest.defined()) {
        op->rest.accept(this);
    }
}

void IRVisitor::visit(const Fork *op) {
    op->first.accept(this);
    if (op->rest.defined()) {
        op->rest.accept(this);
    }
}

void IRVisitor::visit(const IfThenElse *op) {
    op->condition.accept(this);
    op->then_case.accept(this);
    if (op->else_case.defined()) {
        op->else_case.accept(this);
    }
}

void IRVisitor::visit(const Evaluate *op) {
    op->value.accept(this);
}

void IRVisitor::visit(const Shuffle *op) {
    for (const Expr &i : op->vectors) {
        i.accept(this);
    }
}

void IRVisitor::visit(const VectorReduce *op) {
    op->value.accept(this);
}

void IRVisitor::visit(const Atomic *op) {
    op->body.accept(this);
}

void IRVisitor::visit(const HoistedStorage *op) {
    op->body.accept(this);
}

void IRGraphVisitor::include(const Expr &e) {
    auto r = visited.insert(e.get());
    if (r.second) {
        // Was newly inserted
        e.accept(this);
    }
}

void IRGraphVisitor::include(const Stmt &s) {
    auto r = visited.insert(s.get());
    if (r.second) {
        // Was newly inserted
        s.accept(this);
    }
}

void IRGraphVisitor::visit(const IntImm *) {
}

void IRGraphVisitor::visit(const UIntImm *) {
}

void IRGraphVisitor::visit(const FloatImm *) {
}

void IRGraphVisitor::visit(const StringImm *) {
}

void IRGraphVisitor::visit(const Cast *op) {
    include(op->value);
}

void IRGraphVisitor::visit(const Reinterpret *op) {
    include(op->value);
}

void IRGraphVisitor::visit(const Variable *op) {
}

void IRGraphVisitor::visit(const Add *op) {
    include(op->a);
    include(op->b);
}

void IRGraphVisitor::visit(const Sub *op) {
    include(op->a);
    include(op->b);
}

void IRGraphVisitor::visit(const Mul *op) {
    include(op->a);
    include(op->b);
}

void IRGraphVisitor::visit(const Div *op) {
    include(op->a);
    include(op->b);
}

void IRGraphVisitor::visit(const Mod *op) {
    include(op->a);
    include(op->b);
}

void IRGraphVisitor::visit(const Min *op) {
    include(op->a);
    include(op->b);
}

void IRGraphVisitor::visit(const Max *op) {
    include(op->a);
    include(op->b);
}

void IRGraphVisitor::visit(const EQ *op) {
    include(op->a);
    include(op->b);
}

void IRGraphVisitor::visit(const NE *op) {
    include(op->a);
    include(op->b);
}

void IRGraphVisitor::visit(const LT *op) {
    include(op->a);
    include(op->b);
}

void IRGraphVisitor::visit(const LE *op) {
    include(op->a);
    include(op->b);
}

void IRGraphVisitor::visit(const GT *op) {
    include(op->a);
    include(op->b);
}

void IRGraphVisitor::visit(const GE *op) {
    include(op->a);
    include(op->b);
}

void IRGraphVisitor::visit(const And *op) {
    include(op->a);
    include(op->b);
}

void IRGraphVisitor::visit(const Or *op) {
    include(op->a);
    include(op->b);
}

void IRGraphVisitor::visit(const Not *op) {
    include(op->a);
}

void IRGraphVisitor::visit(const Select *op) {
    include(op->condition);
    include(op->true_value);
    include(op->false_value);
}

void IRGraphVisitor::visit(const Load *op) {
    include(op->predicate);
    include(op->index);
}

void IRGraphVisitor::visit(const Ramp *op) {
    include(op->base);
    include(op->stride);
}

void IRGraphVisitor::visit(const Broadcast *op) {
    include(op->value);
}

void IRGraphVisitor::visit(const Call *op) {
    for (const auto &arg : op->args) {
        include(arg);
    }
}

void IRGraphVisitor::visit(const Let *op) {
    include(op->value);
    include(op->body);
}

void IRGraphVisitor::visit(const LetStmt *op) {
    include(op->value);
    include(op->body);
}

void IRGraphVisitor::visit(const AssertStmt *op) {
    include(op->condition);
    include(op->message);
}

void IRGraphVisitor::visit(const ProducerConsumer *op) {
    include(op->body);
}

void IRGraphVisitor::visit(const For *op) {
    include(op->min);
    include(op->extent);
    include(op->body);
}

void IRGraphVisitor::visit(const Acquire *op) {
    include(op->semaphore);
    include(op->count);
    include(op->body);
}

void IRGraphVisitor::visit(const Store *op) {
    include(op->predicate);
    include(op->value);
    include(op->index);
}

void IRGraphVisitor::visit(const Provide *op) {
    for (const auto &value : op->values) {
        include(value);
    }
    for (const auto &arg : op->args) {
        include(arg);
    }
}

void IRGraphVisitor::visit(const Allocate *op) {
    for (const auto &extent : op->extents) {
        include(extent);
    }
    include(op->condition);
    if (op->new_expr.defined()) {
        include(op->new_expr);
    }
    include(op->body);
}

void IRGraphVisitor::visit(const Free *op) {
}

void IRGraphVisitor::visit(const Realize *op) {
    for (const auto &bound : op->bounds) {
        include(bound.min);
        include(bound.extent);
    }
    include(op->condition);
    include(op->body);
}

void IRGraphVisitor::visit(const Prefetch *op) {
    for (const auto &bound : op->bounds) {
        include(bound.min);
        include(bound.extent);
    }
    include(op->condition);
    include(op->body);
}

void IRGraphVisitor::visit(const Block *op) {
    include(op->first);
    include(op->rest);
}

void IRGraphVisitor::visit(const Fork *op) {
    include(op->first);
    include(op->rest);
}

void IRGraphVisitor::visit(const IfThenElse *op) {
    include(op->condition);
    include(op->then_case);
    if (op->else_case.defined()) {
        include(op->else_case);
    }
}

void IRGraphVisitor::visit(const Evaluate *op) {
    include(op->value);
}

void IRGraphVisitor::visit(const Shuffle *op) {
    for (const Expr &i : op->vectors) {
        include(i);
    }
}

void IRGraphVisitor::visit(const VectorReduce *op) {
    include(op->value);
}

void IRGraphVisitor::visit(const Atomic *op) {
    include(op->body);
}

void IRGraphVisitor::visit(const HoistedStorage *op) {
    include(op->body);
}

}  // namespace Internal
}  // namespace Halide
