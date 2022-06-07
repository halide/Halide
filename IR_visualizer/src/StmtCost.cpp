#include "IRVisitor.h"
#include <Halide.h>

#include "ExternFuncArgument.h"
#include "Function.h"

using namespace Halide;
using namespace Internal;

struct StmtCost {
    int cost;
    // add other costs later, like integer-ALU cost, float-ALU cost, memory cost, etc.
};

class FindStmtCost : public IRVisitor {

    // create variable that will hold mapping of stmt to cost
    std::unordered_map<const IRNode *, StmtCost> stmt_cost;

    int get_cost(const IRNode *node) const {
        auto it = stmt_cost.find(node);
        if (it == stmt_cost.end()) {
            assert(false);
            return 0;
        }
        return it->second.cost;
    }

    // TODO: decide if count of 1 or 0
    void visit(const IntImm *op) override {
        stmt_cost.emplace(op, 1);
    }

    void visit(const UIntImm *op) override {
        stmt_cost.emplace(op, 1);
    }

    void visit(const FloatImm *op) override {
        stmt_cost.emplace(op, 1);
    }

    void visit(const StringImm *op) override {
        stmt_cost.emplace(op, 1);
    }

    void visit(const Cast *op) override {
        op->value.accept(this);
    }

    void visit(const Variable *op) override {
    }

    void visit(const Add *op) override {
        // TODO: ask about why i need to cast to (IRNode *) and why static_cast doesn't work
        //       comment above `get`: Override get() to return a BaseExprNode * instead of an IRNode *
        op->a.accept(this);
        op->b.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get()) + get_cost((IRNode *)op->b.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const Sub *op) override {
        op->a.accept(this);
        op->b.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get()) + get_cost((IRNode *)op->b.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const Mul *op) override {
        op->a.accept(this);
        op->b.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get()) + get_cost((IRNode *)op->b.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const Div *op) override {
        op->a.accept(this);
        op->b.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get()) + get_cost((IRNode *)op->b.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const Mod *op) override {
        op->a.accept(this);
        op->b.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get()) + get_cost((IRNode *)op->b.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const Min *op) override {
        op->a.accept(this);
        op->b.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get()) + get_cost((IRNode *)op->b.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const Max *op) override {
        op->a.accept(this);
        op->b.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get()) + get_cost((IRNode *)op->b.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const EQ *op) override {
        op->a.accept(this);
        op->b.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get()) + get_cost((IRNode *)op->b.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const NE *op) override {
        op->a.accept(this);
        op->b.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get()) + get_cost((IRNode *)op->b.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const LT *op) override {
        op->a.accept(this);
        op->b.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get()) + get_cost((IRNode *)op->b.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const LE *op) override {
        op->a.accept(this);
        op->b.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get()) + get_cost((IRNode *)op->b.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const GT *op) override {
        op->a.accept(this);
        op->b.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get()) + get_cost((IRNode *)op->b.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const GE *op) override {
        op->a.accept(this);
        op->b.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get()) + get_cost((IRNode *)op->b.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const And *op) override {
        op->a.accept(this);
        op->b.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get()) + get_cost((IRNode *)op->b.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const Or *op) override {
        op->a.accept(this);
        op->b.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get()) + get_cost((IRNode *)op->b.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const Not *op) override {
        op->a.accept(this);
        int tempVal = get_cost((IRNode *)op->a.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    // TODO: do we agree on my counts?
    void visit(const Select *op) override {
        op->condition.accept(this);

        op->true_value.accept(this);
        op->false_value.accept(this);
        int tempVal = get_cost((IRNode *)op->condition.get()) + get_cost((IRNode *)op->true_value.get()) + get_cost((IRNode *)op->false_value.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const Load *op) override {
        op->predicate.accept(this);
        op->index.accept(this);
        int tempVal = get_cost((IRNode *)op->predicate.get()) + get_cost((IRNode *)op->index.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const Ramp *op) override {
        op->base.accept(this);
        op->stride.accept(this);
        int tempVal = get_cost((IRNode *)op->base.get()) + get_cost((IRNode *)op->stride.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const Broadcast *op) override {
        op->value.accept(this);
        int tempVal = get_cost((IRNode *)op->value.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const Call *op) override {
        int tempVal = 0;
        for (const auto &arg : op->args) {
            arg.accept(this);
            tempVal += get_cost((IRNode *)arg.get());
        }

        // Consider extern call args
        if (op->func.defined()) {
            Function f(op->func);
            if (op->call_type == Call::Halide && f.has_extern_definition()) {
                for (const auto &arg : f.extern_arguments()) {
                    if (arg.is_expr()) {
                        arg.expr.accept(this);
                        tempVal += get_cost((IRNode *)arg.expr.get());
                    }
                }
            }
        }
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const Let *op) override {
        op->value.accept(this);
        op->body.accept(this);
        int tempVal = get_cost((IRNode *)op->value.get()) + get_cost((IRNode *)op->body.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const LetStmt *op) override {
        op->value.accept(this);
        op->body.accept(this);
        int tempVal = get_cost((IRNode *)op->value.get()) + get_cost((IRNode *)op->body.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const AssertStmt *op) override {
        op->condition.accept(this);
        op->message.accept(this);
        int tempVal = get_cost((IRNode *)op->condition.get()) + get_cost((IRNode *)op->message.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const ProducerConsumer *op) override {
        op->body.accept(this);
        int tempVal = get_cost((IRNode *)op->body.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const For *op) override {
        op->min.accept(this);
        op->extent.accept(this);
        op->body.accept(this);
        int bodyCost = get_cost((IRNode *)op->body.get());

        // FIXME: how to take into account the different types of for loops?
        if (op->for_type == ForType::Parallel) {
            // TODO: complete
        }
        if (op->for_type == ForType::Serial) {
            // TODO: complete
        }
        if (op->for_type == ForType::Unrolled) {
            // TODO: complete
        }
        if (op->for_type == ForType::Vectorized) {
            // TODO: complete
        }
        // TODO: somehow have to get the range of the loop so that you can times the cost of the body by loop length
        //       maybe have to estimate it? how?
    }

    void visit(const Acquire *op) override {
        op->semaphore.accept(this);
        op->count.accept(this);
        op->body.accept(this);
        int tempVal = get_cost((IRNode *)op->semaphore.get()) + get_cost((IRNode *)op->count.get()) + get_cost((IRNode *)op->body.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    void visit(const Store *op) override {
        op->predicate.accept(this);
        op->value.accept(this);
        op->index.accept(this);
        int tempVal = get_cost((IRNode *)op->predicate.get()) + get_cost((IRNode *)op->value.get()) + get_cost((IRNode *)op->index.get());
        stmt_cost.emplace(op, 1 + tempVal);
    }

    // TODO: don't understand this one
    void visit(const Provide *op) override {
        op->predicate.accept(this);
        for (const auto &value : op->values) {
            value.accept(this);
        }
        for (const auto &arg : op->args) {
            arg.accept(this);
        }
    }

    void visit(const Allocate *op) override {
        int tempVal = 0;
        for (const auto &extent : op->extents) {
            extent.accept(this);
            tempVal += get_cost((IRNode *)extent.get());
        }
        op->condition.accept(this);
        tempVal += get_cost((IRNode *)op->condition.get());

        if (op->new_expr.defined()) {
            op->new_expr.accept(this);
            tempVal += get_cost((IRNode *)op->new_expr.get());
        }
        op->body.accept(this);
        tempVal += get_cost((IRNode *)op->body.get());
        stmt_cost.emplace(op, tempVal);
    }

    void visit(const Free *op) override {
        stmt_cost.emplace(op, 1);
    }

    // TODO: don't understand this one
    void visit(const Realize *op) override {
        for (const auto &bound : op->bounds) {
            bound.min.accept(this);
            bound.extent.accept(this);
        }
        op->condition.accept(this);
        op->body.accept(this);
    }

    // TODO: don't understand this one
    void visit(const Prefetch *op) override {
        for (const auto &bound : op->bounds) {
            bound.min.accept(this);
            bound.extent.accept(this);
        }
        op->condition.accept(this);
        op->body.accept(this);
    }

    // TODO: don't understand this one
    void visit(const Block *op) override {
        op->first.accept(this);
        if (op->rest.defined()) {
            op->rest.accept(this);
        }
    }

    // TODO: don't understand this one
    void visit(const Fork *op) override {
        op->first.accept(this);
        if (op->rest.defined()) {
            op->rest.accept(this);
        }
    }

    void visit(const IfThenElse *op) override {
        op->condition.accept(this);
        op->then_case.accept(this);
        int tempVal = get_cost((IRNode *)op->condition.get()) + get_cost((IRNode *)op->then_case.get());
        if (op->else_case.defined()) {
            op->else_case.accept(this);
            tempVal += get_cost((IRNode *)op->else_case.get());
        }
        stmt_cost.emplace(op, tempVal);
    }

    void visit(const Evaluate *op) override {
        op->value.accept(this);
        int tempVal = get_cost((IRNode *)op->value.get());
        stmt_cost.emplace(op, tempVal);
    }

    void visit(const Shuffle *op) override {
        int tempVal = 0;
        for (const Expr &i : op->vectors) {
            i.accept(this);
            tempVal += get_cost((IRNode *)i.get());
        }
        stmt_cost.emplace(op, tempVal);
    }

    void visit(const VectorReduce *op) override {
        op->value.accept(this);
        int tempVal = get_cost((IRNode *)op->value.get());
        stmt_cost.emplace(op, tempVal);
    }

    // TODO: don't understand this one
    void visit(const Atomic *op) override {
        op->body.accept(this);
        int tempVal = get_cost((IRNode *)op->body.get());
        stmt_cost.emplace(op, tempVal);
    }
};
