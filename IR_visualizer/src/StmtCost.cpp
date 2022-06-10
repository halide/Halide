// #include "IRVisitor.h"
// #include <Halide.h>

// #include "ExternFuncArgument.h"
// #include "Function.h"

#include "StmtCost.h"
#include "../../src/IRVisitor.h"
#include <Halide.h>

#include "../../src/ExternFuncArgument.h"
#include "../../src/Function.h"

#include <stdexcept>
#include <unordered_map>

using namespace Halide;
using namespace Internal;

// calculates the total cost of a stmt
int FindStmtCost::get_total_cost(const IRNode *node) const {
    auto it = stmt_cost.find(node);
    if (it == stmt_cost.end()) {
        assert(false);
        return 0;
    }
    int cost = it->second.cost;
    int depth = it->second.depth;

    return cost + DEPTH_COST * depth;
}

// TODO: decide if count of 1 or 0
void FindStmtCost::visit(const IntImm *op) {
    set_cost(op, 1);
}

void FindStmtCost::visit(const UIntImm *op) {
    set_cost(op, 1);
}

void FindStmtCost::visit(const FloatImm *op) {
    set_cost(op, 1);
}

void FindStmtCost::visit(const StringImm *op) {
    set_cost(op, 1);
}

void FindStmtCost::visit(const Cast *op) {
    op->value.accept(this);
    int tempVal = get_cost(op->value.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const Variable *op) {
    set_cost(op, 1);
}

void FindStmtCost::visit(const Add *op) {
    op->a.accept(this);
    op->b.accept(this);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const Sub *op) {
    op->a.accept(this);
    op->b.accept(this);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const Mul *op) {
    op->a.accept(this);
    op->b.accept(this);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const Div *op) {
    op->a.accept(this);
    op->b.accept(this);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const Mod *op) {
    op->a.accept(this);
    op->b.accept(this);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const Min *op) {
    op->a.accept(this);
    op->b.accept(this);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const Max *op) {
    op->a.accept(this);
    op->b.accept(this);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const EQ *op) {
    op->a.accept(this);
    op->b.accept(this);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const NE *op) {
    op->a.accept(this);
    op->b.accept(this);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const LT *op) {
    op->a.accept(this);
    op->b.accept(this);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const LE *op) {
    op->a.accept(this);
    op->b.accept(this);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const GT *op) {
    op->a.accept(this);
    op->b.accept(this);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const GE *op) {
    op->a.accept(this);
    op->b.accept(this);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const And *op) {
    op->a.accept(this);
    op->b.accept(this);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const Or *op) {
    op->a.accept(this);
    op->b.accept(this);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const Not *op) {
    op->a.accept(this);
    int tempVal = get_cost(op->a.get());
    set_cost(op, 1 + tempVal);
}

// TODO: do we agree on my counts?
void FindStmtCost::visit(const Select *op) {
    op->condition.accept(this);
    op->true_value.accept(this);
    op->false_value.accept(this);

    int tempVal = get_cost(op->condition.get()) + get_cost(op->true_value.get()) + get_cost(op->false_value.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const Load *op) {
    assert(false);
    // op->predicate.accept(this);
    // op->index.accept(this);
    // int tempVal = get_cost(op->predicate.get()) + get_cost(op->index.get());
    // set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const Ramp *op) {
    assert(false);
    // op->base.accept(this);
    // op->stride.accept(this);
    // int tempVal = get_cost(op->base.get()) + get_cost(op->stride.get());
    // set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const Broadcast *op) {
    assert(false);
    // op->value.accept(this);
    // int tempVal = get_cost(op->value.get());
    // set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const Call *op) {
    int tempVal = 0;
    for (const auto &arg : op->args) {
        arg.accept(this);
        tempVal += get_cost(arg.get());
    }

    // Consider extern call args
    if (op->func.defined()) {
        Function f(op->func);
        if (op->call_type == Call::Halide && f.has_extern_definition()) {
            for (const auto &arg : f.extern_arguments()) {
                if (arg.is_expr()) {
                    arg.expr.accept(this);
                    tempVal += get_cost(arg.expr.get());
                }
            }
        }
    }
    set_cost(op, tempVal);
}

void FindStmtCost::visit(const Let *op) {
    op->value.accept(this);
    op->body.accept(this);
    int tempVal = get_cost(op->value.get()) + get_cost(op->body.get());
    set_cost(op, tempVal);
}

void FindStmtCost::visit(const LetStmt *op) {
    op->value.accept(this);
    op->body.accept(this);
    int tempVal = get_cost(op->value.get()) + get_cost(op->body.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const AssertStmt *op) {
    op->condition.accept(this);
    op->message.accept(this);
    int tempVal = get_cost(op->condition.get()) + get_cost(op->message.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const ProducerConsumer *op) {
    op->body.accept(this);
    int tempVal = get_cost(op->body.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const For *op) {
    current_loop_depth += 1;

    op->min.accept(this);
    op->extent.accept(this);
    op->body.accept(this);

    current_loop_depth -= 1;

    int bodyCost = get_cost(op->body.get());

    // TODO: how to take into account the different types of for loops?
    // if (op->for_type == ForType::Serial) {

    // }
    if (op->for_type == ForType::Parallel) {
        assert(false);
    }
    if (op->for_type == ForType::Unrolled) {
        assert(false);
    }
    if (op->for_type == ForType::Vectorized) {
        assert(false);
    }
    set_cost(op, 1 + bodyCost);
}

void FindStmtCost::visit(const Acquire *op) {
    op->semaphore.accept(this);
    op->count.accept(this);
    op->body.accept(this);
    int tempVal = get_cost(op->semaphore.get()) + get_cost(op->count.get()) + get_cost(op->body.get());
    set_cost(op, tempVal);
}

void FindStmtCost::visit(const Store *op) {
    op->predicate.accept(this);
    op->value.accept(this);
    op->index.accept(this);
    int tempVal = get_cost(op->predicate.get()) + get_cost(op->value.get()) + get_cost(op->index.get());
    set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const Provide *op) {
    assert(false);
    // op->predicate.accept(this);
    // int tempVal = get_cost(op->predicate.get());
    // for (const auto &value : op->values) {
    //     value.accept(this);
    //     tempVal += get_cost(value.get());
    // }
    // for (const auto &arg : op->args) {
    //     arg.accept(this);
    //     tempVal += get_cost(arg.get());
    // }
    // set_cost(op, 1 + tempVal);
}

void FindStmtCost::visit(const Allocate *op) {
    assert(false);
    // int tempVal = 0;
    // for (const auto &extent : op->extents) {
    //     extent.accept(this);
    //     tempVal += get_cost(extent.get());
    // }
    // op->condition.accept(this);
    // tempVal += get_cost(op->condition.get());

    // if (op->new_expr.defined()) {
    //     op->new_expr.accept(this);
    //     tempVal += get_cost(op->new_expr.get());
    // }
    // op->body.accept(this);
    // tempVal += get_cost(op->body.get());
    // set_cost(op, tempVal);
}

void FindStmtCost::visit(const Free *op) {
    // TODO: i feel like this should be more than cost 1, but the only
    //       vars it has is the name, which isn't helpful in determining
    //       the cost of the free
    set_cost(op, 1);
}

void FindStmtCost::visit(const Realize *op) {
    assert(false);
    // TODO: is this the same logic as For, where I add the depth?
    // int tempVal = 0;
    // for (const auto &bound : op->bounds) {
    //     bound.min.accept(this);
    //     bound.extent.accept(this);
    //     tempVal += get_cost(bound.min.get()) + get_cost(bound.extent.get());
    // }
    // op->condition.accept(this);
    // op->body.accept(this);
    // tempVal += get_cost(op->condition.get()) + get_cost(op->body.get());
    // set_cost(op, tempVal);
}

void FindStmtCost::visit(const Prefetch *op) {
    assert(false);
    // TODO: similar question as one above
    // int tempVal = 0;
    // for (const auto &bound : op->bounds) {
    //     bound.min.accept(this);
    //     bound.extent.accept(this);
    //     tempVal += get_cost(bound.min.get()) + get_cost(bound.extent.get());
    // }
    // op->condition.accept(this);
    // op->body.accept(this);
    // tempVal += get_cost(op->condition.get()) + get_cost(op->body.get());
    // set_cost(op, tempVal);
}

void FindStmtCost::visit(const Block *op) {
    assert(false);
    // int tempVal = 0;
    // op->first.accept(this);
    // tempVal += get_cost(op->first.get());
    // if (op->rest.defined()) {
    //     op->rest.accept(this);
    //     tempVal += get_cost(op->rest.get());
    // }
    // set_cost(op, tempVal);
}

void FindStmtCost::visit(const Fork *op) {
    assert(false);
    // int tempVal = 0;
    // op->first.accept(this);
    // tempVal += get_cost(op->first.get());
    // if (op->rest.defined()) {
    //     op->rest.accept(this);
    //     tempVal += get_cost(op->rest.get());
    // }
    // set_cost(op, tempVal);
}

void FindStmtCost::visit(const IfThenElse *op) {
    // TODO: is this correct, based on discussion about if-then-else, as
    //       compared to Select?
    op->condition.accept(this);
    op->then_case.accept(this);
    int tempVal = get_cost(op->condition.get()) + get_cost(op->then_case.get());
    if (op->else_case.defined()) {
        op->else_case.accept(this);
        tempVal += get_cost(op->else_case.get());
    }
    set_cost(op, tempVal);
}

void FindStmtCost::visit(const Evaluate *op) {
    op->value.accept(this);
    int tempVal = get_cost(op->value.get());
    set_cost(op, tempVal);
}

void FindStmtCost::visit(const Shuffle *op) {
    assert(false);
    // int tempVal = 0;
    // for (const Expr &i : op->vectors) {
    //     i.accept(this);
    //     tempVal += get_cost(i.get());
    // }
    // set_cost(op, tempVal);
}

void FindStmtCost::visit(const VectorReduce *op) {
    assert(false);
    // op->value.accept(this);
    // int tempVal = get_cost(op->value.get());
    // set_cost(op, tempVal);
}

void FindStmtCost::visit(const Atomic *op) {
    assert(false);
    // op->body.accept(this);
    // int tempVal = get_cost(op->body.get());
    // set_cost(op, tempVal);
}
