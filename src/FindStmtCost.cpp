#include "FindStmtCost.h"
#include "Error.h"

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
        f.body.accept(this);
    }
}
void CostPreProcessor::traverse(const Stmt &s) {
    s.accept(this);
}

int CostPreProcessor::get_lock_access_count(const string name) const {
    auto it = lock_access_counts.find(name);
    if (it == lock_access_counts.end()) {
        internal_error << "\n"
                       << "CostPreProcessor::get_lock_access_count: name (`" << name
                       << "`) not found in `lock_access_counts`"
                       << "\n\n";
        return 0;
    }
    return it->second;
}

void CostPreProcessor::increase_count(const string name) {
    auto it = lock_access_counts.find(name);
    if (it == lock_access_counts.end()) {
        lock_access_counts.emplace(name, 1);
    } else {
        it->second += 1;
    }
}

void CostPreProcessor::visit(const Acquire *op) {
    stringstream name;
    name << op->semaphore;
    increase_count(name.str());
}

void CostPreProcessor::visit(const Atomic *op) {
    stringstream name;
    name << op->producer_name;
    increase_count(name.str());
}

/*
 * FindStmtCost class
 */
void FindStmtCost::generate_costs(const Module &m) {
    cost_preprocessor.traverse(m);
    traverse(m);
    set_max_costs();
}
void FindStmtCost::generate_costs(const Stmt &stmt) {
    cost_preprocessor.traverse(stmt);
    stmt.accept(this);
    set_max_costs();
}

int FindStmtCost::get_computation_color_range(const IRNode *op, bool inclusive) const {
    int range_size;
    int cost;
    int range;

    // divide max cost by NUMBER_COST_COLORS and round up to get range size
    if (inclusive) {
        range_size = (max_computation_cost_inclusive / NUMBER_COST_COLORS) + 1;
        cost = get_computation_cost(op, true);
        range = cost / range_size;

    } else {
        range_size = (max_computation_cost_exclusive / NUMBER_COST_COLORS) + 1;
        cost = get_computation_cost(op, false);
        range = cost / range_size;
    }
    return range;
}
int FindStmtCost::get_data_movement_color_range(const IRNode *op, bool inclusive) const {
    int range_size;
    int cost;
    int range;

    // divide max cost by NUMBER_COST_COLORS and round up to get range size
    if (inclusive) {
        range_size = (max_data_movement_cost_inclusive / NUMBER_COST_COLORS) + 1;
        cost = get_data_movement_cost(op, true);
        range = cost / range_size;
    } else {
        range_size = (max_data_movement_cost_exclusive / NUMBER_COST_COLORS) + 1;
        cost = get_data_movement_cost(op, false);
        range = cost / range_size;
    }
    return range;
}

int FindStmtCost::get_depth(const IRNode *node) const {
    if (node == nullptr) {
        internal_error << "\n"
                       << "FindStmtCost::get_depth: node is nullptr"
                       << "\n\n";
    }
    auto it = stmt_cost.find(node);
    if (it == stmt_cost.end()) {

        // sometimes, these constant values are created on the whim in
        // StmtToViz.cpp - return 1 to avoid crashing
        IRNodeType type = node->node_type;
        if (type == IRNodeType::IntImm || type == IRNodeType::UIntImm ||
            type == IRNodeType::FloatImm || type == IRNodeType::StringImm) {
            return 1;
        } else {
            internal_error << "\n"
                           << "FindStmtCost::get_depth: " << print_node(node)
                           << "node not found in stmt_cost map"
                           << "\n\n";
            return 0;
        }
    }

    StmtCost cost_node = it->second;

    return cost_node.depth;
}
int FindStmtCost::get_computation_cost(const IRNode *node, bool inclusive) const {
    if (node == nullptr) {
        internal_error << "\n"
                       << "FindStmtCost::get_computation_cost: node is nullptr"
                       << "\n\n";
    }
    auto it = stmt_cost.find(node);
    StmtCost cost_node;

    if (it == stmt_cost.end()) {
        // sometimes, these constant values are created on the whim in
        // StmtToViz.cpp - set cost_node to be fresh StmtCost to avoid crashing
        IRNodeType type = node->node_type;
        if (type == IRNodeType::IntImm || type == IRNodeType::UIntImm ||
            type == IRNodeType::FloatImm || type == IRNodeType::StringImm) {
            cost_node = StmtCost{0, 1, 0, 1, 0};  // TODO: confirm this
        } else {
            internal_error << "\n"
                           << "FindStmtCost::get_computation_cost: " << print_node(node)
                           << "node not found in stmt_cost map"
                           << "\n\n";
            return 0;
        }
    } else {
        cost_node = it->second;
    }

    int cost;
    if (inclusive) {
        cost = cost_node.computation_cost_inclusive;
        // if value is negative, means it wasn't set
        if (cost < 0) {
            internal_error << "\n"
                           << "FindStmtCost::get_computation_cost: " << print_node(node)
                           << "computation_cost_inclusive not set"
                           << "\n\n";
        }
        return cost;
    } else {
        cost = cost_node.computation_cost_exclusive;
        // if value is negative, means it wasn't set
        if (cost < 0) {
            internal_error << "\n"
                           << "FindStmtCost::get_computation_cost: " << print_node(node)
                           << "computation_cost_exclusive not set"
                           << "\n\n";
        }
        return cost;
    }
}
int FindStmtCost::get_data_movement_cost(const IRNode *node, bool inclusive) const {
    if (node == nullptr) {
        internal_error << "\n"
                       << "FindStmtCost::get_data_movement_cost: node is nullptr"
                       << "\n\n";
    }
    auto it = stmt_cost.find(node);
    StmtCost cost_node;

    if (it == stmt_cost.end()) {
        // sometimes, these constant values are created on the whim in
        // StmtToViz.cpp - set cost_node to be fresh StmtCost to avoid crashing
        IRNodeType type = node->node_type;
        if (type == IRNodeType::IntImm || type == IRNodeType::UIntImm ||
            type == IRNodeType::FloatImm || type == IRNodeType::StringImm) {
            cost_node = StmtCost{0, 1, 0, 1, 0};  // TODO: confirm this
        } else {
            internal_error << "\n"
                           << "FindStmtCost::get_data_movement_cost: " << print_node(node)
                           << "node not found in stmt_cost map"
                           << "\n\n";
            return 0;
        }
    } else {
        cost_node = it->second;
    }

    int cost;
    if (inclusive) {
        cost = cost_node.data_movement_cost_inclusive;
        // if value is negative, means it wasn't set
        if (cost < 0) {
            internal_error << "\n"
                           << "FindStmtCost::get_data_movement_cost: " << print_node(node)
                           << "data_movement_cost_inclusive not set"
                           << "\n\n";
        }
        return cost;
    } else {
        cost = cost_node.data_movement_cost_exclusive;
        // if value is negative, means it wasn't set
        if (cost < 0) {
            internal_error << "\n"
                           << "FindStmtCost::get_data_movement_cost: " << print_node(node)
                           << "data_movement_cost_exclusive not set"
                           << "\n\n";
        }
        return cost;
    }
}

bool FindStmtCost::is_local_variable(const string &name) const {
    for (auto const &allocate_name : allocate_variables) {
        if (allocate_name == name) {
            return true;
        }
    }
    return false;
}

void FindStmtCost::traverse(const Module &m) {
    // recursively traverse all submodules
    for (const auto &s : m.submodules()) {
        traverse(s);
    }

    // traverse all functions
    for (const auto &f : m.functions()) {
        f.body.accept(this);
    }

    return;
}

vector<int> FindStmtCost::get_costs_children(const IRNode *parent, vector<const IRNode *> children,
                                             bool inclusive) const {
    int children_cc = 0;
    int children_dmc = 0;

    for (const IRNode *child : children) {
        children_cc += get_computation_cost(child, inclusive);
        children_dmc += get_data_movement_cost(child, inclusive);
    }

    vector<int> costs_children = {children_cc, children_dmc};

    if (costs_children.size() != 2) {
        internal_error << "\n\n"
                       << "FindStmtCost::get_costs_children: costs_children.size() != 2"
                       << "\n\n";
    }
    return costs_children;
}

void FindStmtCost::set_inclusive_costs(const IRNode *node, vector<const IRNode *> children,
                                       int node_cc = NORMAL_NODE_CC, int node_dmc = NORMAL_NODE_DMC,
                                       int scalingFactor_dmc = NORMAL_SCALE_FACTOR_DMC) {

    vector<int> costs_children = get_costs_children(node, children, true);

    int computation_cost = (node_cc * current_loop_depth) + costs_children[0];
    int data_movement_cost =
        scalingFactor_dmc * ((node_dmc * current_loop_depth) + costs_children[1]);

    auto it = stmt_cost.find(node);
    if (it == stmt_cost.end()) {
        stmt_cost.emplace(
            node, StmtCost{current_loop_depth, computation_cost, data_movement_cost, -1, -1});
    } else {
        it->second.computation_cost_inclusive = computation_cost;
        it->second.data_movement_cost_inclusive = data_movement_cost;
    }
}
void FindStmtCost::set_exclusive_costs(const IRNode *node, vector<const IRNode *> children,
                                       int node_cc = NORMAL_NODE_CC, int node_dmc = NORMAL_NODE_DMC,
                                       int scalingFactor_dmc = 1) {
    vector<int> costs_children = get_costs_children(node, children, false);

    int computation_cost = (node_cc * current_loop_depth) + costs_children[0];
    int data_movement_cost =
        scalingFactor_dmc * ((node_dmc * current_loop_depth) + costs_children[1]);

    auto it = stmt_cost.find(node);
    if (it == stmt_cost.end()) {
        stmt_cost.emplace(
            node, StmtCost{current_loop_depth, -1, -1, computation_cost, data_movement_cost});
    } else {
        it->second.computation_cost_exclusive = computation_cost;
        it->second.data_movement_cost_exclusive = data_movement_cost;
    }
}

void FindStmtCost::set_max_costs() {
    int max_cost;

    // max_computation_cost_inclusive
    max_cost = 0;
    for (auto const &pair : stmt_cost) {
        int cost = get_computation_cost(pair.first, true);
        if (cost > max_cost) {
            max_cost = cost;
        }
    }
    max_computation_cost_inclusive = max_cost;

    // max_data_movement_cost_inclusive
    max_cost = 0;
    for (auto const &pair : stmt_cost) {
        int cost = get_data_movement_cost(pair.first, true);
        if (cost > max_cost) {
            max_cost = cost;
        }
    }
    max_data_movement_cost_inclusive = max_cost;

    // max_computation_cost_exclusive
    max_cost = 0;
    for (auto const &pair : stmt_cost) {
        int cost = get_computation_cost(pair.first, false);
        if (cost > max_cost) {
            max_cost = cost;
        }
    }
    max_computation_cost_exclusive = max_cost;

    // max_data_movement_cost_exclusive
    max_cost = 0;
    for (auto const &pair : stmt_cost) {
        int cost = get_data_movement_cost(pair.first, false);
        if (cost > max_cost) {
            max_cost = cost;
        }
    }
    max_data_movement_cost_exclusive = max_cost;
}

int FindStmtCost::get_scaling_factor(uint8_t bits, uint16_t lanes) const {
    int bitsFactor = bits / 8;
    int lanesFactor = lanes / 8;
    if (bitsFactor == 0) {
        bitsFactor = 1;
    }
    if (lanesFactor == 0) {
        lanesFactor = 1;
    }
    return bitsFactor * lanesFactor;
}

void FindStmtCost::visit_binary_op(const IRNode *op, const Expr &a, const Expr &b) {
    a.accept(this);
    b.accept(this);

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, {a.get(), b.get()});
    set_exclusive_costs(op, {a.get(), b.get()});
}

void FindStmtCost::visit(const IntImm *op) {
    set_inclusive_costs(op, {});
    set_exclusive_costs(op, {});
}

void FindStmtCost::visit(const UIntImm *op) {
    set_inclusive_costs(op, {});
    set_exclusive_costs(op, {});
}

void FindStmtCost::visit(const FloatImm *op) {
    set_inclusive_costs(op, {});
    set_exclusive_costs(op, {});
}

void FindStmtCost::visit(const StringImm *op) {
    set_inclusive_costs(op, {});
    set_exclusive_costs(op, {});
}

void FindStmtCost::visit(const Cast *op) {
    op->value.accept(this);

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, {op->value.get()});
    set_exclusive_costs(op, {op->value.get()});
}

void FindStmtCost::visit(const Reinterpret *op) {
    op->value.accept(this);

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, {op->value.get()});
    set_exclusive_costs(op, {op->value.get()});
}
void FindStmtCost::visit(const Variable *op) {
    set_inclusive_costs(op, {});
    set_exclusive_costs(op, {});
}

void FindStmtCost::visit(const Add *op) {
    visit_binary_op(op, op->a, op->b);
}

void FindStmtCost::visit(const Sub *op) {
    visit_binary_op(op, op->a, op->b);
}

void FindStmtCost::visit(const Mul *op) {
    visit_binary_op(op, op->a, op->b);
}

void FindStmtCost::visit(const Div *op) {
    visit_binary_op(op, op->a, op->b);
}

void FindStmtCost::visit(const Mod *op) {
    visit_binary_op(op, op->a, op->b);
}

void FindStmtCost::visit(const Min *op) {
    visit_binary_op(op, op->a, op->b);
}

void FindStmtCost::visit(const Max *op) {
    visit_binary_op(op, op->a, op->b);
}

void FindStmtCost::visit(const EQ *op) {
    visit_binary_op(op, op->a, op->b);
}

void FindStmtCost::visit(const NE *op) {
    visit_binary_op(op, op->a, op->b);
}

void FindStmtCost::visit(const LT *op) {
    visit_binary_op(op, op->a, op->b);
}

void FindStmtCost::visit(const LE *op) {
    visit_binary_op(op, op->a, op->b);
}

void FindStmtCost::visit(const GT *op) {
    visit_binary_op(op, op->a, op->b);
}

void FindStmtCost::visit(const GE *op) {
    visit_binary_op(op, op->a, op->b);
}

void FindStmtCost::visit(const And *op) {
    visit_binary_op(op, op->a, op->b);
}

void FindStmtCost::visit(const Or *op) {
    visit_binary_op(op, op->a, op->b);
}

void FindStmtCost::visit(const Not *op) {
    op->a.accept(this);

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, {op->a.get()});
    set_exclusive_costs(op, {op->a.get()});
}

void FindStmtCost::visit(const Select *op) {
    op->condition.accept(this);
    op->true_value.accept(this);
    op->false_value.accept(this);

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, {op->condition.get(), op->true_value.get(), op->false_value.get()});
    set_exclusive_costs(op, {op->condition.get(), op->true_value.get(), op->false_value.get()});
}

void FindStmtCost::visit(const Load *op) {
    op->predicate.accept(this);
    op->index.accept(this);

    // TODO: does node need scaling factor? how does it combine with store?

    uint8_t bits = op->type.bits();
    uint16_t lanes = op->type.lanes();
    int scalingFactor_dmc = get_scaling_factor(bits, lanes);

    // see if op->name is a global variable or not, and adjust accordingly
    if (is_local_variable(op->name)) {
        scalingFactor_dmc *= LOAD_LOCAL_VAR_COST;
    } else {
        scalingFactor_dmc *= LOAD_GLOBAL_VAR_COST;
    }

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, {op->predicate.get(), op->index.get()}, NORMAL_NODE_CC, LOAD_DM_COST,
                        scalingFactor_dmc);
    set_exclusive_costs(op, {op->predicate.get(), op->index.get()}, NORMAL_NODE_CC, LOAD_DM_COST,
                        scalingFactor_dmc);
}

void FindStmtCost::visit(const Ramp *op) {
    op->base.accept(this);
    op->stride.accept(this);

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, {op->base.get(), op->stride.get()});
    set_exclusive_costs(op, {op->base.get(), op->stride.get()});
}

void FindStmtCost::visit(const Broadcast *op) {
    op->value.accept(this);

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, {op->value.get()});
    set_exclusive_costs(op, {op->value.get()});
}

void FindStmtCost::visit(const Call *op) {
    /*
        TODO: take into account this message from Maaz:

                > there are instructions in Halide IR that are not compute instructions but
                  data-movement instructions. Such as Shuffle::interleave or
       Shuffle::concatenate. I would ignore this for now but you should know about them.

    */
    vector<const IRNode *> children;

    for (const auto &arg : op->args) {
        arg.accept(this);
        children.push_back(arg.get());
    }

    // Consider extern call args
    if (op->func.defined()) {
        Function f(op->func);
        if (op->call_type == Call::Halide && f.has_extern_definition()) {
            for (const auto &arg : f.extern_arguments()) {
                if (arg.is_expr()) {
                    arg.expr.accept(this);
                    children.push_back(arg.expr.get());
                }
            }
        }
    }

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, children);
    set_exclusive_costs(op, children);
}

void FindStmtCost::visit(const Let *op) {

    op->value.accept(this);
    op->body.accept(this);

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, {op->value.get(), op->body.get()});
    set_exclusive_costs(op, {op->value.get(), op->body.get()});
}

void FindStmtCost::visit(const Shuffle *op) {
    vector<const IRNode *> children;
    for (const Expr &i : op->vectors) {
        i.accept(this);
        children.push_back(i.get());
    }

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, children);
    set_exclusive_costs(op, children);
}

void FindStmtCost::visit(const VectorReduce *op) {
    op->value.accept(this);

    int countCost = op->value.type().lanes() - 1;  // TODO: i forget what this does lol

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, {op->value.get()}, countCost);
    set_exclusive_costs(op, {op->value.get()}, countCost);
}

void FindStmtCost::visit(const LetStmt *op) {
    op->value.accept(this);
    op->body.accept(this);

    set_inclusive_costs(op, {op->value.get(), op->body.get()});
    set_exclusive_costs(op, {op->value.get()});
}

void FindStmtCost::visit(const AssertStmt *op) {
    op->condition.accept(this);
    op->message.accept(this);

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, {op->condition.get(), op->message.get()});
    set_exclusive_costs(op, {op->condition.get(), op->message.get()});
}

void FindStmtCost::visit(const ProducerConsumer *op) {
    op->body.accept(this);

    set_inclusive_costs(op, {op->body.get()});
    set_exclusive_costs(op, {});
}

void FindStmtCost::visit(const For *op) {
    current_loop_depth += 1;

    op->min.accept(this);
    op->extent.accept(this);
    op->body.accept(this);

    current_loop_depth -= 1;

    set_inclusive_costs(op, {op->min.get(), op->extent.get(), op->body.get()});
    set_exclusive_costs(op, {op->min.get(), op->extent.get()});

    // TODO: how to take into account the different types of for loops?
    if (op->for_type == ForType::Parallel) {
        internal_error << "\n"
                       << "FindStmtCost::visit: Parallel for loops are not supported yet"
                       << "\n\n";
    }
    if (op->for_type == ForType::Unrolled) {
        internal_error << "\n"
                       << "FindStmtCost::visit: Unrolled for loops are not supported yet"
                       << "\n\n";
    }
    if (op->for_type == ForType::Vectorized) {
        internal_error << "\n"
                       << "FindStmtCost::visit: Vectorized for loops are not supported yet"
                       << "\n\n";
    }
}

void FindStmtCost::visit(const Acquire *op) {
    /*
        TODO: change this

                * depends on contention (how many other accesses are there to this
                  particular semaphore?)
                * need to have separate FindStmtCost::visitor that FindStmtCost::visits
       everything and counts the number of times each lock is accessed, and also keep track of the
       depth of said lock (the deeper, the more times it will be accessed)

                * do we need to recurse on body???
    */
    internal_error
        << "\n\n"
        << "reached Acquire! take a look at its use - visit(Acquire) is not fully implemented yet"
        << "\n\n";

    stringstream name;
    name << op->semaphore;
    int lock_cost = cost_preprocessor.get_lock_access_count(name.str());

    op->semaphore.accept(this);
    op->count.accept(this);
    op->body.accept(this);

    set_inclusive_costs(op, {op->semaphore.get(), op->count.get(), op->body.get()}, lock_cost);
    set_exclusive_costs(op, {op->semaphore.get(), op->count.get()}, lock_cost);
}

void FindStmtCost::visit(const Store *op) {

    op->predicate.accept(this);
    op->index.accept(this);
    op->value.accept(this);

    uint8_t bits = op->index.type().bits();
    uint16_t lanes = op->index.type().lanes();
    int scalingFactor_dmc = get_scaling_factor(bits, lanes);

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, {op->predicate.get(), op->index.get(), op->value.get()}, NORMAL_NODE_CC,
                        STORE_DM_COST, scalingFactor_dmc);
    set_exclusive_costs(op, {op->predicate.get(), op->index.get(), op->value.get()}, NORMAL_NODE_CC,
                        STORE_DM_COST, scalingFactor_dmc);
}

void FindStmtCost::visit(const Provide *op) {
    op->predicate.accept(this);

    vector<const IRNode *> children;
    children.push_back(op->predicate.get());

    for (const auto &value : op->values) {
        value.accept(this);
        children.push_back(value.get());
    }
    for (const auto &arg : op->args) {
        arg.accept(this);
        children.push_back(arg.get());
    }

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, children);
    set_exclusive_costs(op, children);
}

void FindStmtCost::visit(const Allocate *op) {
    /*
        TODO: treat this node differently

            * loop depth is important
            * type of allocation is especially important (heap vs stack)
                  this can be found MemoryType of the Allocate node (might need
                  some nesting to find the node with this type)
            * could FindStmtCost::visit `extents` for costs, and n`

            * do we need to recurse on body???
    */

    // to determine if IRNodeType::Variable op is local or global later on
    allocate_variables.push_back(op->name);

    vector<const IRNode *> children;

    for (const auto &extent : op->extents) {
        extent.accept(this);
        children.push_back(extent.get());
    }

    op->condition.accept(this);
    children.push_back(op->condition.get());

    if (op->new_expr.defined()) {
        op->new_expr.accept(this);
        children.push_back(op->new_expr.get());
    }

    set_exclusive_costs(op, children);

    op->body.accept(this);
    children.push_back(op->body.get());

    set_inclusive_costs(op, children);
}

void FindStmtCost::visit(const Free *op) {
    // TODO: i feel like this should be more than cost 1, but the only
    //       vars it has is the name, which isn't helpful in determining
    //       the cost of the free

    set_inclusive_costs(op, {});
    set_exclusive_costs(op, {});
}

void FindStmtCost::visit(const Realize *op) {

    internal_error
        << "\n\n"
        << "reached Realize! take a look at its use - visit(Realize) is not fully implemented yet"
        << "\n\n";

    vector<const IRNode *> children;

    for (const auto &bound : op->bounds) {
        bound.min.accept(this);
        bound.extent.accept(this);
        children.push_back(bound.min.get());
        children.push_back(bound.extent.get());
    }

    op->condition.accept(this);
    children.push_back(op->condition.get());

    set_exclusive_costs(op, children);

    op->body.accept(this);
    children.push_back(op->body.get());

    set_inclusive_costs(op, children);
}

void FindStmtCost::visit(const Prefetch *op) {
    vector<const IRNode *> children;

    for (const auto &bound : op->bounds) {
        bound.min.accept(this);
        bound.extent.accept(this);

        children.push_back(bound.min.get());
        children.push_back(bound.extent.get());
    }

    op->condition.accept(this);
    children.push_back(op->condition.get());

    set_exclusive_costs(op, children);

    op->body.accept(this);
    children.push_back(op->body.get());

    set_inclusive_costs(op, children);
}

void FindStmtCost::visit(const Block *op) {
    vector<const IRNode *> children;

    op->first.accept(this);
    children.push_back(op->first.get());

    if (op->rest.defined()) {
        op->rest.accept(this);
        children.push_back(op->rest.get());
    }

    set_inclusive_costs(op, children);

    // exclusive costs are 0 - there is no exclusive computation or data movement for Block
    set_exclusive_costs(op, {});
}

void FindStmtCost::visit(const Fork *op) {
    internal_error
        << "\n\n"
        << "reached Fork! take a look at its use - visit(Fork) is not fully implemented yet"
        << "\n\n";

    op->first.accept(this);

    vector<const IRNode *> children;
    children.push_back(op->first.get());

    if (op->rest.defined()) {
        op->rest.accept(this);
        children.push_back(op->rest.get());
    }

    // TODO: is this right??
    set_inclusive_costs(op, children);
    set_exclusive_costs(op, children);
}

void FindStmtCost::visit(const IfThenElse *op) {
    op->condition.accept(this);
    op->then_case.accept(this);

    set_exclusive_costs(op, {op->condition.get()});
    set_inclusive_costs(op, {op->condition.get(), op->then_case.get()});

    // recurse on else case, but don't include as child - only want inclusive cost to have then case
    if (op->else_case.defined()) {
        op->else_case.accept(this);
    }
}

void FindStmtCost::visit(const Evaluate *op) {
    op->value.accept(this);

    vector<int> costs_children = get_costs_children(op, {op->value.get()}, true);

    // inclusive and exclusive costs are the same
    set_inclusive_costs(op, {op->value.get()});
    set_exclusive_costs(op, {op->value.get()});
}

void FindStmtCost::visit(const Atomic *op) {
    /*
        TODO: change this
                * make it similar to acquire
                * parallel vs vector is important
    */
    internal_error
        << "\n"
        << "reached Atomic! take a look at its use - visit(Atomic) is not fully implemented"
        << "\n\n";

    op->body.accept(this);

    stringstream name;
    name << op->producer_name;
    int lock_cost = cost_preprocessor.get_lock_access_count(name.str());

    set_inclusive_costs(op, {op->body.get()}, lock_cost);
    set_exclusive_costs(op, {}, lock_cost);
}

string FindStmtCost::print_node(const IRNode *node) const {
    stringstream s;
    s << "Node in question has type: ";
    IRNodeType type = node->node_type;
    if (type == IRNodeType::IntImm) {
        s << "IntImm type" << endl;
        auto node1 = dynamic_cast<const IntImm *>(node);
        s << "value: " << node1->value << endl;
    } else if (type == IRNodeType::UIntImm) {
        s << "UIntImm type" << endl;
    } else if (type == IRNodeType::FloatImm) {
        s << "FloatImm type" << endl;
    } else if (type == IRNodeType::StringImm) {
        s << "StringImm type" << endl;
    } else if (type == IRNodeType::Broadcast) {
        s << "Broadcast type" << endl;
    } else if (type == IRNodeType::Cast) {
        s << "Cast type" << endl;
    } else if (type == IRNodeType::Variable) {
        auto node1 = dynamic_cast<const Variable *>(node);
        s << "Variable type - " << node1->name << endl;
    } else if (type == IRNodeType::Add) {
        s << "Add type" << endl;
    } else if (type == IRNodeType::Sub) {
        s << "Sub type" << endl;
    } else if (type == IRNodeType::Mod) {
        s << "Mod type" << endl;
    } else if (type == IRNodeType::Mul) {
        s << "Mul type" << endl;
    } else if (type == IRNodeType::Div) {
        s << "Div type" << endl;
    } else if (type == IRNodeType::Min) {
        s << "Min type" << endl;
    } else if (type == IRNodeType::Max) {
        s << "Max type" << endl;
    } else if (type == IRNodeType::EQ) {
        s << "EQ type" << endl;
    } else if (type == IRNodeType::NE) {
        s << "NE type" << endl;
    } else if (type == IRNodeType::LT) {
        s << "LT type" << endl;
    } else if (type == IRNodeType::LE) {
        s << "LE type" << endl;
    } else if (type == IRNodeType::GT) {
        s << "GT type" << endl;
    } else if (type == IRNodeType::GE) {
        s << "GE type" << endl;
    } else if (type == IRNodeType::And) {
        s << "And type" << endl;
    } else if (type == IRNodeType::Or) {
        s << "Or type" << endl;
    } else if (type == IRNodeType::Not) {
        s << "Not type" << endl;
    } else if (type == IRNodeType::Select) {
        s << "Select type" << endl;
    } else if (type == IRNodeType::Load) {
        s << "Load type: ";
        auto node1 = dynamic_cast<const Load *>(node);
        s << node1->name << ", index: " << node1->index << endl;
    } else if (type == IRNodeType::Ramp) {
        s << "Ramp type" << endl;
    } else if (type == IRNodeType::Call) {
        s << "Call type" << endl;
    } else if (type == IRNodeType::Let) {
        s << "Let type" << endl;
    } else if (type == IRNodeType::Shuffle) {
        s << "Shuffle type" << endl;
    } else if (type == IRNodeType::VectorReduce) {
        s << "VectorReduce type" << endl;
    } else if (type == IRNodeType::LetStmt) {
        s << "LetStmt type" << endl;
    } else if (type == IRNodeType::AssertStmt) {
        s << "AssertStmt type" << endl;
    } else if (type == IRNodeType::ProducerConsumer) {
        s << "ProducerConsumer type" << endl;
    } else if (type == IRNodeType::For) {
        s << "For type" << endl;
    } else if (type == IRNodeType::Acquire) {
        s << "Acquire type" << endl;
    } else if (type == IRNodeType::Store) {
        s << "Store type: ";
        auto node1 = dynamic_cast<const Store *>(node);
        s << node1->name << ", index: " << node1->index;
        s << ", value: " << node1->value << endl;
    } else if (type == IRNodeType::Provide) {
        s << "Provide type" << endl;
    } else if (type == IRNodeType::Allocate) {
        s << "Allocate type" << endl;
    } else if (type == IRNodeType::Free) {
        s << "Free type" << endl;
    } else if (type == IRNodeType::Realize) {
        s << "Realize type" << endl;
    } else if (type == IRNodeType::Block) {
        s << "Block type" << endl;
    } else if (type == IRNodeType::Fork) {
        s << "Fork type" << endl;
    } else if (type == IRNodeType::IfThenElse) {
        s << "IfThenElse type" << endl;
    } else if (type == IRNodeType::Evaluate) {
        s << "Evaluate type" << endl;
    } else if (type == IRNodeType::Prefetch) {
        s << "Prefetch type" << endl;
    } else if (type == IRNodeType::Atomic) {
        s << "Atomic type" << endl;
    } else if (type == IRNodeType::Reinterpret) {
        s << "Reinterpret type" << endl;
    } else {
        s << "Unknown type" << endl;
    }

    return s.str();
}
