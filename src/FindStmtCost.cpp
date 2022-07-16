#include "FindStmtCost.h"

using namespace Halide;
using namespace Internal;

void FindStmtCost::traverse(const Module &m) {
    // recursively traverse all submodules
    for (const auto &s : m.submodules()) {
        traverse(s);
    }

    // traverse all functions
    for (const auto &f : m.functions()) {
        mutate(f.body);
    }

    return;
}

Expr FindStmtCost::mutate(const Expr &expr) {
    return IRMutator::mutate(expr);
}

Stmt FindStmtCost::mutate(const Stmt &stmt) {
    return IRMutator::mutate(stmt);
}

// returns the range of node based on its
int FindStmtCost::get_range(const IRNode *op) const {
    if (op == nullptr) {
        cout << "OH NO ITS NULL!!!!! " << endl;
        return -1;
    }

    // get max value of cost in stmt_cost map
    int max_cost = 0;
    for (auto const &pair : stmt_cost) {
        if (calculate_cost(pair.second) > max_cost) {
            max_cost = calculate_cost(pair.second);
        }
    }

    // divide max cost by 8 and round up to get ranges
    int range_size = (max_cost / NUMBER_COST_COLORS) + 1;
    int cost = get_total_cost(op);
    int range = cost / range_size;
    return range;
}

int FindStmtCost::get_total_cost(const IRNode *node) const {
    auto it = stmt_cost.find(node);
    if (it == stmt_cost.end()) {
        m_assert(false, "node not found in stmt_cost");
        return 0;
    }

    StmtCost cost_node = it->second;

    return calculate_cost(cost_node);
}

int FindStmtCost::get_depth(const IRNode *node) const {
    auto it = stmt_cost.find(node);
    if (it == stmt_cost.end()) {
        m_assert(false, "node not found in stmt_cost");
        return 0;
    }

    StmtCost cost_node = it->second;

    return cost_node.depth;
}

int FindStmtCost::get_cost(const IRNode *node) const {
    auto it = stmt_cost.find(node);
    if (it == stmt_cost.end()) {
        m_assert(false, "node not found in stmt_cost");
        return 0;
    }
    return it->second.cost;
}

void FindStmtCost::set_cost(const IRNode *node, int cost) {
    auto it = stmt_cost.find(node);
    if (it == stmt_cost.end()) {
        stmt_cost.emplace(node, StmtCost{cost, current_loop_depth});
    } else {
        it->second.cost = cost;
        it->second.depth = current_loop_depth;
    }
}

int FindStmtCost::calculate_cost(StmtCost cost_node) const {
    int cost = cost_node.cost;
    int depth = cost_node.depth;

    return cost + DEPTH_COST * depth;
}

void FindStmtCost::print_map(unordered_map<const IRNode *, StmtCost> const &m) {
    for (auto const &pair : m) {
        cout << "{" << pair.first << ": " << pair.second.cost << "}\n";
    }
}

Expr FindStmtCost::visit(const IntImm *op) {
    set_cost(op, 1);
    return op;
}

Expr FindStmtCost::visit(const UIntImm *op) {
    set_cost(op, 1);
    return op;
}

Expr FindStmtCost::visit(const FloatImm *op) {
    set_cost(op, 1);
    return op;
}

Expr FindStmtCost::visit(const StringImm *op) {
    set_cost(op, 1);
    return op;
}

Expr FindStmtCost::visit(const Cast *op) {
    mutate(op->value);
    int tempVal = get_cost(op->value.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const Variable *op) {
    set_cost(op, 1);
    return op;
}

Expr FindStmtCost::visit(const Add *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const Sub *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const Mul *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const Div *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const Mod *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const Min *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const Max *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const EQ *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const NE *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const LT *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const LE *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const GT *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const GE *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const And *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const Or *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const Not *op) {
    mutate(op->a);
    int tempVal = get_cost(op->a.get());
    set_cost(op, 1 + tempVal);
    return op;
}

// TODO: do we agree on my counts?
Expr FindStmtCost::visit(const Select *op) {
    mutate(op->condition);
    mutate(op->true_value);
    mutate(op->false_value);

    int tempVal = get_cost(op->condition.get()) + get_cost(op->true_value.get()) + get_cost(op->false_value.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const Load *op) {
    mutate(op->predicate);
    mutate(op->index);
    int tempVal = get_cost(op->predicate.get()) + get_cost(op->index.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const Ramp *op) {
    mutate(op->base);
    mutate(op->stride);
    int tempVal = get_cost(op->base.get()) + get_cost(op->stride.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const Broadcast *op) {
    mutate(op->value);
    int tempVal = get_cost(op->value.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const Call *op) {
    int tempVal = 0;
    for (const auto &arg : op->args) {
        mutate(arg);
        tempVal += get_cost(arg.get());
    }

    // Consider extern call args
    if (op->func.defined()) {
        Function f(op->func);
        if (op->call_type == Call::Halide && f.has_extern_definition()) {
            for (const auto &arg : f.extern_arguments()) {
                if (arg.is_expr()) {
                    mutate(arg.expr);
                    tempVal += get_cost(arg.expr.get());
                }
            }
        }
    }
    set_cost(op, 1 + tempVal);
    return op;
}

Expr FindStmtCost::visit(const Let *op) {
    mutate(op->value);
    mutate(op->body);
    int tempVal = get_cost(op->value.get()) + get_cost(op->body.get());
    set_cost(op, tempVal);
    return op;
}

Expr FindStmtCost::visit(const Shuffle *op) {
    int tempVal = 0;
    for (const Expr &i : op->vectors) {
        mutate(i);
        tempVal += get_cost(i.get());
    }
    set_cost(op, tempVal);
    return op;
}

Expr FindStmtCost::visit(const VectorReduce *op) {
    mutate(op->value);
    int tempVal = get_cost(op->value.get());
    int countCost = op->value.type().lanes() - 1;

    set_cost(op, tempVal + countCost);
    return op;
}

Stmt FindStmtCost::visit(const LetStmt *op) {
    mutate(op->value);
    mutate(op->body);
    int tempVal = get_cost(op->value.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Stmt FindStmtCost::visit(const AssertStmt *op) {
    mutate(op->condition);
    mutate(op->message);
    int tempVal = get_cost(op->condition.get()) + get_cost(op->message.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Stmt FindStmtCost::visit(const ProducerConsumer *op) {
    mutate(op->body);
    int tempVal = get_cost(op->body.get());
    set_cost(op, tempVal);
    return op;
}

Stmt FindStmtCost::visit(const For *op) {
    current_loop_depth += 1;

    mutate(op->min);
    mutate(op->extent);
    mutate(op->body);

    current_loop_depth -= 1;

    int bodyCost = get_cost(op->body.get());

    // TODO: how to take into account the different types of for loops?
    if (op->for_type == ForType::Parallel) {
        m_assert(false, "Parallel for loops are not supported yet");
    }
    if (op->for_type == ForType::Unrolled) {
        m_assert(false, "Unrolled for loops are not supported yet");
    }
    if (op->for_type == ForType::Vectorized) {
        m_assert(false, "Vectorized for loops are not supported yet");
    }
    set_cost(op, 1 + bodyCost);
    return op;
}

Stmt FindStmtCost::visit(const Acquire *op) {
    /*
        TODO: change this

                * depends on contention (how many other accesses are there to this
                  particular semaphore?)
                * need to have separate FindStmtCost::visitor that FindStmtCost::visits everything and es
                  the number of times each lock is accessed, and also keep track of the depth
                  of said lock (the deeper, the more times it will be accessed)
    */
    mutate(op->semaphore);
    mutate(op->count);
    mutate(op->body);
    int tempVal = get_cost(op->semaphore.get()) + get_cost(op->count.get()) + get_cost(op->body.get());
    set_cost(op, tempVal);
    return op;
}

Stmt FindStmtCost::visit(const Store *op) {
    mutate(op->predicate);
    mutate(op->value);
    mutate(op->index);

    int tempVal = get_cost(op->predicate.get()) + get_cost(op->value.get()) + get_cost(op->index.get());
    set_cost(op, 1 + tempVal);
    return op;
}

Stmt FindStmtCost::visit(const Provide *op) {
    int tempVal = get_cost(op->predicate.get());
    for (const auto &value : op->values) {
        mutate(value);
        tempVal += get_cost(value.get());
    }
    for (const auto &arg : op->args) {
        mutate(arg);
        tempVal += get_cost(arg.get());
    }
    set_cost(op, tempVal);
    return op;
}

Stmt FindStmtCost::visit(const Allocate *op) {
    /*
        TODO: treat this node differently

            * loop depth is important
            * type of allocation is especially important (heap vs stack)
                  this can be found MemoryType of the Allocate node (might need
                  some nesting to find the node with this type)
            * could FindStmtCost::visit `extents` for costs, and n`
            * (in case of GPUShared type) visualize size of allocation in case
              the size of shared memory and goes into main memory
    */
    int tempVal = 0;
    for (const auto &extent : op->extents) {
        mutate(extent);
        tempVal += get_cost(extent.get());
    }
    mutate(op->condition);
    tempVal += get_cost(op->condition.get());

    if (op->new_expr.defined()) {
        mutate(op->new_expr);
        tempVal += get_cost(op->new_expr.get());
    }
    mutate(op->body);
    tempVal += get_cost(op->body.get());
    set_cost(op, tempVal);
    return op;
}

Stmt FindStmtCost::visit(const Free *op) {
    // TODO: i feel like this should be more than cost 1, but the only
    //       vars it has is the name, which isn't helpful in determining
    //       the cost of the free
    set_cost(op, 1);
    return op;
}

Stmt FindStmtCost::visit(const Realize *op) {
    // TODO: is this the same logic as For, where I add the depth?
    int tempVal = 0;
    for (const auto &bound : op->bounds) {
        mutate(bound.min);
        mutate(bound.extent);
        tempVal += get_cost(bound.min.get()) + get_cost(bound.extent.get());
    }
    mutate(op->condition);
    mutate(op->body);
    tempVal += get_cost(op->condition.get()) + get_cost(op->body.get());
    set_cost(op, tempVal);
    return op;
}

Stmt FindStmtCost::visit(const Prefetch *op) {
    /*
        TODO: like caching? # of memory stores
    */
    int tempVal = 0;
    for (const auto &bound : op->bounds) {
        mutate(bound.min);
        mutate(bound.extent);

        tempVal += get_cost(bound.min.get()) + get_cost(bound.extent.get());
    }
    mutate(op->condition);
    mutate(op->body);
    tempVal += get_cost(op->condition.get()) + get_cost(op->body.get());
    set_cost(op, tempVal);
    return op;
}

Stmt FindStmtCost::visit(const Block *op) {
    // TODO: making this cost 1 is wrong - need to change this
    int tempVal = 0;
    mutate(op->first);
    tempVal += get_cost(op->first.get());
    if (op->rest.defined()) {
        mutate(op->rest);
        tempVal += get_cost(op->rest.get());
    }
    // set_cost(op, tempVal);
    set_cost(op, 1);
    return op;
}

Stmt FindStmtCost::visit(const Fork *op) {
    int tempVal = 0;
    mutate(op->first);
    tempVal += get_cost(op->first.get());
    if (op->rest.defined()) {
        mutate(op->rest);
        tempVal += get_cost(op->rest.get());
    }
    set_cost(op, tempVal);
    return op;
}

Stmt FindStmtCost::visit(const IfThenElse *op) {
    mutate(op->condition);
    mutate(op->then_case);

    int tempVal = get_cost(op->condition.get()) + get_cost(op->then_case.get());
    if (op->else_case.defined()) {
        mutate(op->else_case);
        tempVal += get_cost(op->else_case.get());
    }
    set_cost(op, tempVal);
    return op;
}

Stmt FindStmtCost::visit(const Evaluate *op) {
    mutate(op->value);
    int tempVal = get_cost(op->value.get());
    set_cost(op, tempVal);
    return op;
}

Stmt FindStmtCost::visit(const Atomic *op) {
    /*
        TODO: change this

                * make it similar to acquire
                * parallel vs vector is important
    */
    mutate(op->body);
    int tempVal = get_cost(op->body.get());
    set_cost(op, tempVal);
    return op;
}
