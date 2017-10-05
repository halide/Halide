#include "IRVisitor.h"

namespace Halide {
namespace Internal {

IRVisitor::~IRVisitor() {
}

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
    for (size_t i = 0; i < op->args.size(); i++) {
        op->args[i].accept(this);
    }

    // Consider extern call args
    if (op->func.defined()) {
        Function f(op->func);
        if (op->call_type == Call::Halide && f.has_extern_definition()) {
            for (size_t i = 0; i < f.extern_arguments().size(); i++) {
                ExternFuncArgument arg = f.extern_arguments()[i];
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

void IRVisitor::visit(const Store *op) {
    op->predicate.accept(this);
    op->value.accept(this);
    op->index.accept(this);
}

void IRVisitor::visit(const Provide *op) {
    for (size_t i = 0; i < op->values.size(); i++) {
        op->values[i].accept(this);
    }
    for (size_t i = 0; i < op->args.size(); i++) {
        op->args[i].accept(this);
    }
}

void IRVisitor::visit(const Allocate *op) {
    for (size_t i = 0; i < op->extents.size(); i++) {
      op->extents[i].accept(this);
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
    for (size_t i = 0; i < op->bounds.size(); i++) {
        op->bounds[i].min.accept(this);
        op->bounds[i].extent.accept(this);
    }
    op->condition.accept(this);
    op->body.accept(this);
}

void IRVisitor::visit(const Prefetch *op) {
    for (size_t i = 0; i < op->bounds.size(); i++) {
        op->bounds[i].min.accept(this);
        op->bounds[i].extent.accept(this);
    }
}

void IRVisitor::visit(const Block *op) {
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
    for (Expr i : op->vectors) {
        i.accept(this);
    }
}

void IRGraphVisitor::include(const Expr &e) {
    if (visited.count(e.get())) {
        return;
    } else {
        visited.insert(e.get());
        e.accept(this);
        return;
    }
}

void IRGraphVisitor::include(const Stmt &s) {
    if (visited.count(s.get())) {
        return;
    } else {
        visited.insert(s.get());
        s.accept(this);
        return;
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
    for (size_t i = 0; i < op->args.size(); i++) {
        include(op->args[i]);
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

void IRGraphVisitor::visit(const Store *op) {
    include(op->predicate);
    include(op->value);
    include(op->index);
}

void IRGraphVisitor::visit(const Provide *op) {
    for (size_t i = 0; i < op->values.size(); i++) {
        include(op->values[i]);
    }
    for (size_t i = 0; i < op->args.size(); i++) {
        include(op->args[i]);
    }
}

void IRGraphVisitor::visit(const Allocate *op) {
    for (size_t i = 0; i < op->extents.size(); i++) {
        include(op->extents[i]);
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
    for (size_t i = 0; i < op->bounds.size(); i++) {
        include(op->bounds[i].min);
        include(op->bounds[i].extent);
    }
    include(op->condition);
    include(op->body);
}

void IRGraphVisitor::visit(const Prefetch *op) {
    for (size_t i = 0; i < op->bounds.size(); i++) {
        include(op->bounds[i].min);
        include(op->bounds[i].extent);
    }
}

void IRGraphVisitor::visit(const Block *op) {
    include(op->first);
    if (op->rest.defined()) include(op->rest);
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
    for (Expr i : op->vectors) {
        include(i);
    }
}

}
}
