#include "IRVisitor.h"
#include "IR.h"

namespace Halide { 
namespace Internal {

IRVisitor::~IRVisitor() {
}
    
void IRVisitor::visit(const IntImm *) {
}
    
void IRVisitor::visit(const FloatImm *) {
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
}

void IRVisitor::visit(const Let *op) {
    op->value.accept(this);
    op->body.accept(this);
}

void IRVisitor::visit(const LetStmt *op) {
    op->value.accept(this);
    op->body.accept(this);
}

void IRVisitor::visit(const PrintStmt *op) {
    for (size_t i = 0; i < op->args.size(); i++) {
        op->args[i].accept(this);
    }
}

void IRVisitor::visit(const AssertStmt *op) {
    op->condition.accept(this);
}

void IRVisitor::visit(const Pipeline *op) {
    op->produce.accept(this);
    if (op->update.defined()) op->update.accept(this);
    op->consume.accept(this);
}

void IRVisitor::visit(const For *op) {
    op->min.accept(this);
    op->extent.accept(this);
    op->body.accept(this);
}

void IRVisitor::visit(const Store *op) {
    op->value.accept(this);
    op->index.accept(this);
}

void IRVisitor::visit(const Provide *op) {
    op->value.accept(this);
    for (size_t i = 0; i < op->args.size(); i++) {
        op->args[i].accept(this);
    }
}

void IRVisitor::visit(const Allocate *op) {
    op->size.accept(this);
    op->body.accept(this);
}

void IRVisitor::visit(const Realize *op) {
    for (size_t i = 0; i < op->bounds.size(); i++) {
        op->bounds[i].min.accept(this);
        op->bounds[i].extent.accept(this);
    }
    op->body.accept(this);
}

void IRVisitor::visit(const Block *op) {
    op->first.accept(this);
    if (op->rest.defined()) op->rest.accept(this);
}

}
}


