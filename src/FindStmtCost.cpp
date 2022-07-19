#include "FindStmtCost.h"

using namespace Halide;
using namespace Internal;

/*
 * CostPreProcessor class
 */
void CostPreProcessor::traverse(const Module &m) {
    // recursively traverse all submodules
    for (const auto &s : m.submodules()) {
        traverse(s);
    }

    // traverse all functions
    for (const auto &f : m.functions()) {
        mutate(f.body);
    }
}

Stmt CostPreProcessor::visit(const Acquire *op) {
    stringstream name;
    name << op->semaphore;
    increase_count(name.str());
    return op;
}

Stmt CostPreProcessor::visit(const Atomic *op) {
    stringstream name;
    name << op->producer_name;
    increase_count(name.str());
    return op;
}

void CostPreProcessor::increase_count(const string name) {
    auto it = lock_access_counts.find(name);
    if (it == lock_access_counts.end()) {
        lock_access_counts.emplace(name, 1);
    } else {
        it->second += 1;
    }
}

int CostPreProcessor::get_count(const string name) const {
    auto it = lock_access_counts.find(name);
    if (it == lock_access_counts.end()) {
        m_assert(false, "node not found in `lock_access_counts`");
        return 0;
    }
    return it->second;
}

/*
 * CostPreProcessor class
 */
void FindStmtCost::generate_costs(const Module &m) {

    cost_preprocessor.traverse(m);
    traverse(m);
}
void FindStmtCost::generate_costs(const Stmt &stmt) {

    cost_preprocessor.mutate(stmt);
    mutate(stmt);
}

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

int FindStmtCost::get_computation_range(const IRNode *op) const {
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
    int cost = get_computation_cost(op);
    int range = cost / range_size;
    return range;
}

int FindStmtCost::get_data_movement_range(const IRNode *op) const {
    if (op == nullptr) {
        cout << "OH NO ITS NULL!!!!! " << endl;
        return -1;
    }

    // get max value of cost in stmt_cost map
    int max_cost = 0;
    for (auto const &pair : stmt_cost) {
        if (pair.second.data_movement_cost > max_cost) {
            max_cost = pair.second.data_movement_cost;
        }
    }

    // divide max cost by 8 and round up to get ranges
    int range_size = (max_cost / NUMBER_COST_COLORS) + 1;
    int cost = get_data_movement_cost(op);
    int range = cost / range_size;
    return range;
}

int FindStmtCost::get_computation_cost(const IRNode *node) const {
    auto it = stmt_cost.find(node);
    if (it == stmt_cost.end()) {
        m_assert(false, "node not found in stmt_cost");
        return 0;
    }

    StmtCost cost_node = it->second;

    return calculate_cost(cost_node);
}

int FindStmtCost::get_data_movement_cost(const IRNode *node) const {
    auto it = stmt_cost.find(node);
    if (it == stmt_cost.end()) {
        m_assert(false, "node not found in stmt_cost");
        return 0;
    }

    return it->second.data_movement_cost;
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
    return it->second.computation_cost;
}

void FindStmtCost::set_costs(const IRNode *node, int computation_cost, int data_movement_cost) {
    auto it = stmt_cost.find(node);
    if (it == stmt_cost.end()) {
        stmt_cost.emplace(node, StmtCost{current_loop_depth, computation_cost, data_movement_cost});
    } else {
        it->second.depth = current_loop_depth;
        it->second.computation_cost = computation_cost;
        it->second.data_movement_cost = data_movement_cost;
    }
}

int FindStmtCost::calculate_cost(StmtCost cost_node) const {
    int cost = cost_node.computation_cost;
    int depth = cost_node.depth;

    return cost + DEPTH_COST * depth;
}

void FindStmtCost::print_map(unordered_map<const IRNode *, StmtCost> const &m) {
    for (auto const &pair : m) {
        cout << "{" << pair.first << ": " << pair.second.computation_cost << "}\n";
    }
}

Expr FindStmtCost::visit(const IntImm *op) {
    set_costs(op, 1, 0);
    return op;
}

Expr FindStmtCost::visit(const UIntImm *op) {
    set_costs(op, 1, 0);
    return op;
}

Expr FindStmtCost::visit(const FloatImm *op) {
    set_costs(op, 1, 0);
    return op;
}

Expr FindStmtCost::visit(const StringImm *op) {
    set_costs(op, 1, 0);
    return op;
}

Expr FindStmtCost::visit(const Cast *op) {
    mutate(op->value);
    int tempVal = get_cost(op->value.get());
    int dataMovementCost = get_data_movement_cost(op->value.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const Variable *op) {
    set_costs(op, 1, 0);
    return op;
}

Expr FindStmtCost::visit(const Add *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    int dataMovementCost = get_data_movement_cost(op->a.get()) + get_data_movement_cost(op->b.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const Sub *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    int dataMovementCost = get_data_movement_cost(op->a.get()) + get_data_movement_cost(op->b.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const Mul *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    int dataMovementCost = get_data_movement_cost(op->a.get()) + get_data_movement_cost(op->b.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const Div *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    int dataMovementCost = get_data_movement_cost(op->a.get()) + get_data_movement_cost(op->b.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const Mod *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    int dataMovementCost = get_data_movement_cost(op->a.get()) + get_data_movement_cost(op->b.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const Min *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    int dataMovementCost = get_data_movement_cost(op->a.get()) + get_data_movement_cost(op->b.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const Max *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    int dataMovementCost = get_data_movement_cost(op->a.get()) + get_data_movement_cost(op->b.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const EQ *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    int dataMovementCost = get_data_movement_cost(op->a.get()) + get_data_movement_cost(op->b.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const NE *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    int dataMovementCost = get_data_movement_cost(op->a.get()) + get_data_movement_cost(op->b.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const LT *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    int dataMovementCost = get_data_movement_cost(op->a.get()) + get_data_movement_cost(op->b.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const LE *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    int dataMovementCost = get_data_movement_cost(op->a.get()) + get_data_movement_cost(op->b.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const GT *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    int dataMovementCost = get_data_movement_cost(op->a.get()) + get_data_movement_cost(op->b.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const GE *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    int dataMovementCost = get_data_movement_cost(op->a.get()) + get_data_movement_cost(op->b.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const And *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    int dataMovementCost = get_data_movement_cost(op->a.get()) + get_data_movement_cost(op->b.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const Or *op) {
    mutate(op->a);
    mutate(op->b);
    int tempVal = get_cost(op->a.get()) + get_cost(op->b.get());
    int dataMovementCost = get_data_movement_cost(op->a.get()) + get_data_movement_cost(op->b.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const Not *op) {
    mutate(op->a);
    int tempVal = get_cost(op->a.get());
    int dataMovementCost = get_data_movement_cost(op->a.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

// TODO: do we agree on my counts?
Expr FindStmtCost::visit(const Select *op) {
    mutate(op->condition);
    mutate(op->true_value);
    mutate(op->false_value);

    int tempVal = get_cost(op->condition.get()) + get_cost(op->true_value.get()) + get_cost(op->false_value.get());
    int dataMovementCost = get_data_movement_cost(op->condition.get()) + get_data_movement_cost(op->true_value.get()) + get_data_movement_cost(op->false_value.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const Load *op) {
    mutate(op->predicate);
    mutate(op->index);
    int tempVal = get_cost(op->predicate.get()) + get_cost(op->index.get());
    int dataMovementCost = get_data_movement_cost(op->predicate.get()) + get_data_movement_cost(op->index.get());
    dataMovementCost += LOAD_COST;
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const Ramp *op) {
    mutate(op->base);
    mutate(op->stride);
    int tempVal = get_cost(op->base.get()) + get_cost(op->stride.get());
    int dataMovementCost = get_data_movement_cost(op->base.get()) + get_data_movement_cost(op->stride.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const Broadcast *op) {
    mutate(op->value);
    int tempVal = get_cost(op->value.get());
    int dataMovementCost = get_data_movement_cost(op->value.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const Call *op) {
    int tempVal = 0;
    int dataMovementCost = 0;
    for (const auto &arg : op->args) {
        mutate(arg);
        tempVal += get_cost(arg.get());
        dataMovementCost += get_data_movement_cost(arg.get());
    }

    // Consider extern call args
    if (op->func.defined()) {
        Function f(op->func);
        if (op->call_type == Call::Halide && f.has_extern_definition()) {
            for (const auto &arg : f.extern_arguments()) {
                if (arg.is_expr()) {
                    mutate(arg.expr);
                    tempVal += get_cost(arg.expr.get());
                    dataMovementCost += get_data_movement_cost(arg.expr.get());
                }
            }
        }
    }
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const Let *op) {
    mutate(op->value);
    mutate(op->body);
    int tempVal = get_cost(op->value.get()) + get_cost(op->body.get());
    int dataMovementCost = get_data_movement_cost(op->value.get()) + get_data_movement_cost(op->body.get());
    set_costs(op, tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const Shuffle *op) {
    int tempVal = 0;
    int dataMovementCost = 0;
    for (const Expr &i : op->vectors) {
        mutate(i);
        tempVal += get_cost(i.get());
        dataMovementCost += get_data_movement_cost(i.get());
    }
    set_costs(op, tempVal, dataMovementCost);
    return op;
}

Expr FindStmtCost::visit(const VectorReduce *op) {
    mutate(op->value);
    int tempVal = get_cost(op->value.get());
    int countCost = op->value.type().lanes() - 1;
    int dataMovementCost = get_data_movement_cost(op->value.get());

    set_costs(op, tempVal + countCost, dataMovementCost);
    return op;
}

Stmt FindStmtCost::visit(const LetStmt *op) {
    mutate(op->value);
    mutate(op->body);
    int tempVal = get_cost(op->value.get());
    int dataMovementCost = get_data_movement_cost(op->value.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Stmt FindStmtCost::visit(const AssertStmt *op) {
    mutate(op->condition);
    mutate(op->message);
    int tempVal = get_cost(op->condition.get()) + get_cost(op->message.get());
    int dataMovementCost = get_data_movement_cost(op->condition.get()) + get_data_movement_cost(op->message.get());
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Stmt FindStmtCost::visit(const ProducerConsumer *op) {
    mutate(op->body);
    int tempVal = get_cost(op->body.get());
    int dataMovementCost = get_data_movement_cost(op->body.get());
    set_costs(op, tempVal, dataMovementCost);
    return op;
}

Stmt FindStmtCost::visit(const For *op) {
    current_loop_depth += 1;

    mutate(op->min);
    mutate(op->extent);
    mutate(op->body);

    current_loop_depth -= 1;

    int bodyCost = get_cost(op->body.get());
    int dataMovementCost = get_data_movement_cost(op->body.get());

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
    set_costs(op, 1 + bodyCost, dataMovementCost);
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

                * do we need to recurse on body???
    */
    m_assert(false, "reached Acquire! take a look at its use - visit(Acquire) is not fully implemented");

    stringstream name;
    name << op->semaphore;
    int lock_cost = cost_preprocessor.get_count(name.str());
    set_costs(op, lock_cost, 0);  // this is to remove the error of unused variable
    // TODO: do something with lock cost

    mutate(op->semaphore);
    mutate(op->count);
    mutate(op->body);
    int tempVal = get_cost(op->semaphore.get()) + get_cost(op->count.get()) + get_cost(op->body.get());
    int dataMovementCost = get_data_movement_cost(op->semaphore.get()) + get_data_movement_cost(op->count.get()) + get_data_movement_cost(op->body.get());
    set_costs(op, tempVal, dataMovementCost);
    return op;
}

Stmt FindStmtCost::visit(const Store *op) {
    mutate(op->predicate);
    mutate(op->value);
    mutate(op->index);

    int tempVal = get_cost(op->predicate.get()) + get_cost(op->value.get()) + get_cost(op->index.get());
    int dataMovementCost = get_data_movement_cost(op->predicate.get()) + get_data_movement_cost(op->value.get()) + get_data_movement_cost(op->index.get());
    dataMovementCost += STORE_COST;
    set_costs(op, 1 + tempVal, dataMovementCost);
    return op;
}

Stmt FindStmtCost::visit(const Provide *op) {
    int tempVal = get_cost(op->predicate.get());
    int dataMovementCost = get_computation_cost(op->predicate.get());
    for (const auto &value : op->values) {
        mutate(value);
        tempVal += get_cost(value.get());
        dataMovementCost += get_computation_cost(value.get());
    }
    for (const auto &arg : op->args) {
        mutate(arg);
        tempVal += get_cost(arg.get());
        dataMovementCost += get_computation_cost(arg.get());
    }
    set_costs(op, tempVal, dataMovementCost);
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

            * do we need to recurse on body???
    */
    int tempVal = 0;
    int dataMovementCost = 0;
    for (const auto &extent : op->extents) {
        mutate(extent);
        tempVal += get_cost(extent.get());
        dataMovementCost += get_data_movement_cost(extent.get());
    }
    mutate(op->condition);
    tempVal += get_cost(op->condition.get());
    dataMovementCost += get_data_movement_cost(op->condition.get());

    if (op->new_expr.defined()) {
        mutate(op->new_expr);
        tempVal += get_cost(op->new_expr.get());
        dataMovementCost += get_data_movement_cost(op->new_expr.get());
    }
    mutate(op->body);
    tempVal += get_cost(op->body.get());
    dataMovementCost += get_data_movement_cost(op->body.get());
    set_costs(op, tempVal, dataMovementCost);
    return op;
}

Stmt FindStmtCost::visit(const Free *op) {
    // TODO: i feel like this should be more than cost 1, but the only
    //       vars it has is the name, which isn't helpful in determining
    //       the cost of the free
    set_costs(op, 1, 0);
    return op;
}

Stmt FindStmtCost::visit(const Realize *op) {
    // TODO: is this the same logic as For, where I add the depth?
    int tempVal = 0;
    int dataMovementCost = 0;
    for (const auto &bound : op->bounds) {
        mutate(bound.min);
        mutate(bound.extent);
        tempVal += get_cost(bound.min.get()) + get_cost(bound.extent.get());
        dataMovementCost += get_data_movement_cost(bound.min.get()) + get_data_movement_cost(bound.extent.get());
    }
    mutate(op->condition);
    mutate(op->body);
    tempVal += get_cost(op->condition.get()) + get_cost(op->body.get());
    dataMovementCost += get_data_movement_cost(op->condition.get()) + get_data_movement_cost(op->body.get());
    set_costs(op, tempVal, dataMovementCost);
    return op;
}

Stmt FindStmtCost::visit(const Prefetch *op) {
    /*
        TODO: like caching? # of memory stores
    */
    int tempVal = 0;
    int dataMovementCost = 0;
    for (const auto &bound : op->bounds) {
        mutate(bound.min);
        mutate(bound.extent);

        tempVal += get_cost(bound.min.get()) + get_cost(bound.extent.get());
        dataMovementCost += get_data_movement_cost(bound.min.get()) + get_data_movement_cost(bound.extent.get());
    }
    mutate(op->condition);
    mutate(op->body);
    tempVal += get_cost(op->condition.get()) + get_cost(op->body.get());
    dataMovementCost += get_data_movement_cost(op->condition.get()) + get_data_movement_cost(op->body.get());
    set_costs(op, tempVal, dataMovementCost);
    return op;
}

Stmt FindStmtCost::visit(const Block *op) {
    // TODO: making this cost 1 is wrong - need to change this
    int tempVal = 0;
    int dataMovementCost = 0;
    mutate(op->first);
    tempVal += get_cost(op->first.get());
    dataMovementCost += get_data_movement_cost(op->first.get());
    if (op->rest.defined()) {
        mutate(op->rest);
        tempVal += get_cost(op->rest.get());
        dataMovementCost += get_data_movement_cost(op->rest.get());
    }
    // set_costs(op, tempVal, dataMovementCost);
    set_costs(op, 1, 0);
    return op;
}

Stmt FindStmtCost::visit(const Fork *op) {
    int tempVal = 0;
    int dataMovementCost = 0;
    mutate(op->first);
    tempVal += get_cost(op->first.get());
    if (op->rest.defined()) {
        mutate(op->rest);
        tempVal += get_cost(op->rest.get());
        dataMovementCost += get_data_movement_cost(op->rest.get());
    }
    set_costs(op, tempVal, dataMovementCost);
    return op;
}

Stmt FindStmtCost::visit(const IfThenElse *op) {
    mutate(op->condition);
    mutate(op->then_case);

    int tempVal = get_cost(op->condition.get()) + get_cost(op->then_case.get());
    int dataMovementCost = get_data_movement_cost(op->condition.get()) + get_data_movement_cost(op->then_case.get());
    if (op->else_case.defined()) {
        mutate(op->else_case);
        tempVal += get_cost(op->else_case.get());
        dataMovementCost += get_data_movement_cost(op->else_case.get());
    }
    set_costs(op, tempVal, dataMovementCost);
    return op;
}

Stmt FindStmtCost::visit(const Evaluate *op) {
    mutate(op->value);
    int tempVal = get_cost(op->value.get());
    int dataMovementCost = get_data_movement_cost(op->value.get());
    set_costs(op, tempVal, dataMovementCost);
    return op;
}

Stmt FindStmtCost::visit(const Atomic *op) {
    /*
        TODO: change this

                * make it similar to acquire
                * parallel vs vector is important
    */
    m_assert(false, "reached Atomic! take a look at its use - visit(Atomic) is not fully implemented");
    mutate(op->body);

    stringstream name;
    name << op->producer_name;
    int lock_cost = cost_preprocessor.get_count(name.str());
    set_costs(op, lock_cost, 0);  // this is to remove the error of unused variable
    // TODO: do something with lock cost

    int tempVal = get_cost(op->body.get());
    int dataMovementCost = get_data_movement_cost(op->body.get());
    set_costs(op, tempVal, dataMovementCost);
    return op;
}
