#include "FindStmtCost.h"
#include "Error.h"

using namespace Halide;
using namespace Internal;

/*
 * FindStmtCost class
 */
void FindStmtCost::generate_costs(const Module &m) {
    traverse(m);
    set_max_costs(m);
}

int FindStmtCost::get_cost(const IRNode *node, bool inclusive, bool is_computation) const {
    if (node->node_type == IRNodeType::IfThenElse) {
        return get_if_node_cost(node, inclusive, is_computation);
    } else if (is_computation) {
        return get_computation_cost(node, inclusive);
    } else {
        return get_data_movement_cost(node, inclusive);
    }
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
        }

        // this happens when visualizing cost of else_case in StmtToViz.cpp
        else if (type == IRNodeType::IfThenElse) {
            Stmt then_case = ((const IfThenElse *)node)->then_case;
            return get_depth(then_case.get());
        }

        else {
            internal_error << "\n"
                           << "FindStmtCost::get_depth: " << print_node(node)
                           << "node not found in stmt_cost map"
                           << "\n\n";
            return 0;
        }
    }

    return it->second.depth;
}

int FindStmtCost::get_max_cost(bool inclusive, bool is_computation) const {
    if (is_computation) {
        if (inclusive) {
            return max_computation_cost_inclusive;
        } else {
            return max_computation_cost_exclusive;
        }
    } else {
        if (inclusive) {
            return max_data_movement_cost_inclusive;
        } else {
            return max_data_movement_cost_exclusive;
        }
    }
}

void FindStmtCost::traverse(const Module &m) {

    // traverse all functions
    for (const auto &f : m.functions()) {
        f.body.accept(this);
    }

    return;
}

int FindStmtCost::get_computation_cost(const IRNode *node, bool inclusive) const {
    if (node == nullptr) {
        internal_error << "\n"
                       << "FindStmtCost::get_computation_cost: node is nullptr"
                       << "\n\n";
    }

    auto it = stmt_cost.find(node);
    IRNodeType type = node->node_type;
    int cost = -1;

    if (it == stmt_cost.end()) {
        // sometimes, these constant values are created on the whim in
        // StmtToViz.cpp - set cost_node to be fresh StmtCost to avoid crashing
        if (type == IRNodeType::IntImm || type == IRNodeType::UIntImm ||
            type == IRNodeType::FloatImm || type == IRNodeType::StringImm) {
            cost = NORMAL_NODE_CC;
        }

        // this happens when visualizing cost of else_case in StmtToViz.cpp
        else if (type == IRNodeType::Variable) {
            const Variable *var = (const Variable *)node;
            if (var->name == "canIgnoreVariableName") {
                cost = NORMAL_NODE_CC;
            }
        }

        // else, error
        else {
            internal_error << "\n"
                           << "FindStmtCost::get_computation_cost: " << print_node(node)
                           << "node not found in stmt_cost map"
                           << "\n\n";
            return 0;
        }
    } else {
        if (inclusive) cost = it->second.computation_cost_inclusive;
        else
            cost = it->second.computation_cost_exclusive;
    }

    if (cost < 0) {
        internal_error << "\n"
                       << "FindStmtCost::get_computation_cost: " << print_node(node)
                       << "computation_cost_exclusive not set (cost is: " << cost << ")"
                       << "\n\n";
    }

    return cost;
}
int FindStmtCost::get_data_movement_cost(const IRNode *node, bool inclusive) const {
    if (node == nullptr) {
        internal_error << "\n"
                       << "FindStmtCost::get_data_movement_cost: node is nullptr"
                       << "\n\n";
    }

    auto it = stmt_cost.find(node);
    IRNodeType type = node->node_type;
    int cost = -1;

    if (it == stmt_cost.end()) {
        // sometimes, these constant values are created on the whim in
        // StmtToViz.cpp - set cost_node to be fresh StmtCost to avoid crashing
        if (type == IRNodeType::IntImm || type == IRNodeType::UIntImm ||
            type == IRNodeType::FloatImm || type == IRNodeType::StringImm) {
            cost = NORMAL_NODE_DMC;
        }

        // this happens when visualizing cost of else_case in StmtToViz.cpp
        else if (type == IRNodeType::Variable) {
            const Variable *var = (const Variable *)node;
            if (var->name == "canIgnoreVariableName") {
                cost = NORMAL_NODE_DMC;
            }
        }

        // else, error
        else {
            internal_error << "\n"
                           << "FindStmtCost::get_data_movement_cost: " << print_node(node)
                           << "node not found in stmt_cost map"
                           << "\n\n";
            return 0;
        }
    } else {
        if (inclusive) cost = it->second.data_movement_cost_inclusive;
        else
            cost = it->second.data_movement_cost_exclusive;
    }

    if (cost < 0) {
        internal_error << "\n"
                       << "FindStmtCost::get_data_movement_cost: " << print_node(node)
                       << "data_movement_cost_exclusive not set (cost is: " << cost << ")"
                       << "\n\n";
    }

    return cost;
}

int FindStmtCost::get_if_node_cost(const IRNode *op, bool inclusive, bool is_computation) const {
    if (op->node_type != IRNodeType::IfThenElse) {
        internal_error << "\n"
                       << "FindStmtCost::get_if_node_cost: " << print_node(op)
                       << "node is not IfThenElse"
                       << "\n\n";
    }

    int cost;
    const IfThenElse *if_then_else = dynamic_cast<const IfThenElse *>(op);

    if (is_computation) {
        if (inclusive) {
            int computation_cost = get_computation_cost(if_then_else->condition.get(), inclusive) +
                                   get_computation_cost(if_then_else->then_case.get(), inclusive);
            cost = computation_cost;
        } else {
            cost = NORMAL_NODE_CC;
        }
    } else {
        if (inclusive) {
            int data_movement_cost =
                get_data_movement_cost(if_then_else->condition.get(), inclusive) +
                get_data_movement_cost(if_then_else->then_case.get(), inclusive);
            cost = data_movement_cost;
        } else {
            cost = NORMAL_NODE_DMC;
        }
    }
    return cost;
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

    return costs_children;
}

void FindStmtCost::set_costs(
    bool inclusive, const IRNode *node, vector<const IRNode *> children,
    std::function<int(int)> calculate_cc = [](int x) { return NORMAL_NODE_CC + x; },
    std::function<int(int)> calculate_dmc = [](int x) { return NORMAL_NODE_DMC + x; }) {

    vector<int> costs_children = get_costs_children(node, children, inclusive);

    int computation_cost;
    int data_movement_cost;
    computation_cost = calculate_cc(costs_children[0]);
    data_movement_cost = calculate_dmc(costs_children[1]);

    auto it = stmt_cost.find(node);
    if (it == stmt_cost.end()) {
        if (inclusive) {
            stmt_cost.emplace(
                node, StmtCost{current_loop_depth, computation_cost, data_movement_cost, -1, -1});
        } else {
            stmt_cost.emplace(
                node, StmtCost{current_loop_depth, -1, -1, computation_cost, data_movement_cost});
        }
    } else {
        if (inclusive) {
            it->second.computation_cost_inclusive = computation_cost;
            it->second.data_movement_cost_inclusive = data_movement_cost;
        } else {
            it->second.computation_cost_exclusive = computation_cost;
            it->second.data_movement_cost_exclusive = data_movement_cost;
        }
    }
}

void FindStmtCost::set_max_costs(const Module &m) {

    // inclusive costs (sum up all costs of bodies of functions in module)
    int body_computation_cost = 0;
    int body_data_movement_cost = 0;
    for (const auto &f : m.functions()) {
        body_computation_cost += get_computation_cost(f.body.get(), true);
        body_data_movement_cost += get_data_movement_cost(f.body.get(), true);
    }

    max_computation_cost_inclusive = body_computation_cost;
    max_data_movement_cost_inclusive = body_data_movement_cost;

    // max_computation_cost_exclusive
    int max_cost = 0;
    for (auto const &pair : stmt_cost) {
        int cost = pair.second.computation_cost_exclusive;
        if (cost > max_cost) {
            max_cost = cost;
        }
    }
    max_computation_cost_exclusive = max_cost;

    // max_data_movement_cost_exclusive
    max_cost = 0;
    for (auto const &pair : stmt_cost) {
        int cost = pair.second.data_movement_cost_exclusive;
        if (cost > max_cost) {
            max_cost = cost;
        }
    }
    max_data_movement_cost_exclusive = max_cost;
}

int FindStmtCost::get_scaling_factor(uint8_t bits, uint16_t lanes) const {
    int bits_factor = bits / 8;
    int lanes_factor = lanes / 8;

    if (bits_factor == 0) {
        bits_factor = 1;
    }
    if (lanes_factor == 0) {
        lanes_factor = 1;
    }
    return bits_factor * lanes_factor;
}

void FindStmtCost::visit_binary_op(const IRNode *op, const Expr &a, const Expr &b) {
    a.accept(this);
    b.accept(this);

    // inclusive and exclusive costs are the same
    set_costs(true, op, {a.get(), b.get()});
    set_costs(false, op, {a.get(), b.get()});
}

void FindStmtCost::visit(const IntImm *op) {
    set_costs(true, op, {});
    set_costs(false, op, {});
}

void FindStmtCost::visit(const UIntImm *op) {
    set_costs(true, op, {});
    set_costs(false, op, {});
}

void FindStmtCost::visit(const FloatImm *op) {
    set_costs(true, op, {});
    set_costs(false, op, {});
}

void FindStmtCost::visit(const StringImm *op) {
    set_costs(true, op, {});
    set_costs(false, op, {});
}

void FindStmtCost::visit(const Cast *op) {
    op->value.accept(this);

    // inclusive and exclusive costs are the same
    set_costs(true, op, {op->value.get()});
    set_costs(false, op, {op->value.get()});
}

void FindStmtCost::visit(const Reinterpret *op) {
    op->value.accept(this);

    // inclusive and exclusive costs are the same
    set_costs(true, op, {op->value.get()});
    set_costs(false, op, {op->value.get()});
}
void FindStmtCost::visit(const Variable *op) {
    set_costs(true, op, {});
    set_costs(false, op, {});
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
    set_costs(true, op, {op->a.get()});
    set_costs(false, op, {op->a.get()});
}

void FindStmtCost::visit(const Select *op) {
    op->condition.accept(this);
    op->true_value.accept(this);
    op->false_value.accept(this);

    // inclusive and exclusive costs are the same
    set_costs(true, op, {op->condition.get(), op->true_value.get(), op->false_value.get()});
    set_costs(false, op, {op->condition.get(), op->true_value.get(), op->false_value.get()});
}

void FindStmtCost::visit(const Load *op) {
    op->predicate.accept(this);
    op->index.accept(this);

    uint8_t bits = op->type.bits();
    uint16_t lanes = op->type.lanes();
    int scaling_factor = get_scaling_factor(bits, lanes);

    std::function<int(int)> calculate_cc = [scaling_factor](int children_cost) {
        return scaling_factor * (NORMAL_NODE_CC + children_cost);
    };

    std::function<int(int)> calculate_dmc = [scaling_factor](int children_cost) {
        return scaling_factor * (LOAD_DM_COST + children_cost);
    };

    // inclusive and exclusive costs are the same
    set_costs(true, op, {op->predicate.get(), op->index.get()}, calculate_cc, calculate_dmc);
    set_costs(false, op, {op->predicate.get(), op->index.get()}, calculate_cc, calculate_dmc);
}

void FindStmtCost::visit(const Ramp *op) {
    op->base.accept(this);
    op->stride.accept(this);

    // inclusive and exclusive costs are the same
    set_costs(true, op, {op->base.get(), op->stride.get()});
    set_costs(false, op, {op->base.get(), op->stride.get()});
}

void FindStmtCost::visit(const Broadcast *op) {
    op->value.accept(this);

    // inclusive and exclusive costs are the same
    set_costs(true, op, {op->value.get()});
    set_costs(false, op, {op->value.get()});
}

void FindStmtCost::visit(const Call *op) {
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
    set_costs(true, op, children);
    set_costs(false, op, children);
}

void FindStmtCost::visit(const Let *op) {

    op->value.accept(this);
    op->body.accept(this);

    // inclusive and exclusive costs are the same (keep body in both since it's all inlined)
    set_costs(true, op, {op->value.get(), op->body.get()});
    set_costs(false, op, {op->value.get(), op->body.get()});
}

void FindStmtCost::visit(const Shuffle *op) {
    vector<const IRNode *> children;
    for (const Expr &i : op->vectors) {
        i.accept(this);
        children.push_back(i.get());
    }

    // inclusive and exclusive costs are the same
    set_costs(true, op, children);
    set_costs(false, op, children);
}

void FindStmtCost::visit(const VectorReduce *op) {
    op->value.accept(this);

    // represents the number of times the op->op is applied to the vector
    int count_cost = op->value.type().lanes() - 1;

    std::function<int(int)> calculate_cc = [count_cost](int children_cost) {
        return count_cost * (NORMAL_NODE_CC + children_cost);
    };

    std::function<int(int)> calculate_dmc = [count_cost](int children_cost) {
        return count_cost * (NORMAL_NODE_DMC + children_cost);
    };

    // inclusive and exclusive costs are the same
    set_costs(true, op, {op->value.get()}, calculate_cc, calculate_dmc);
    set_costs(false, op, {op->value.get()}, calculate_cc, calculate_dmc);
}

void FindStmtCost::visit(const LetStmt *op) {
    op->value.accept(this);
    op->body.accept(this);

    set_costs(true, op, {op->value.get(), op->body.get()});
    set_costs(false, op, {op->value.get()});
}

void FindStmtCost::visit(const AssertStmt *op) {
    op->condition.accept(this);
    op->message.accept(this);

    // inclusive and exclusive costs are the same
    set_costs(true, op, {op->condition.get(), op->message.get()});
    set_costs(false, op, {op->condition.get(), op->message.get()});
}

void FindStmtCost::visit(const ProducerConsumer *op) {
    op->body.accept(this);

    set_costs(true, op, {op->body.get()});
    set_costs(false, op, {});
}

void FindStmtCost::visit(const For *op) {
    current_loop_depth += 1;

    op->min.accept(this);
    op->extent.accept(this);
    op->body.accept(this);

    current_loop_depth -= 1;

    set_costs(true, op, {op->min.get(), op->extent.get(), op->body.get()});
    set_costs(false, op, {op->min.get(), op->extent.get()});

    // TODO: complete implementation of different loop types
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
    stringstream name;
    name << op->semaphore;

    op->semaphore.accept(this);
    op->count.accept(this);
    op->body.accept(this);

    set_costs(true, op, {op->semaphore.get(), op->count.get(), op->body.get()});
    set_costs(false, op, {op->semaphore.get(), op->count.get()});
}

void FindStmtCost::visit(const Store *op) {

    op->predicate.accept(this);
    op->index.accept(this);
    op->value.accept(this);

    std::function<int(int)> calculate_cc = [](int children_cost) {
        return NORMAL_NODE_CC + children_cost;
    };

    std::function<int(int)> calculate_dmc = [](int children_cost) {
        return STORE_DM_COST + children_cost;
    };

    // inclusive and exclusive costs are the same
    set_costs(true, op, {op->predicate.get(), op->index.get(), op->value.get()}, calculate_cc,
              calculate_dmc);
    set_costs(false, op, {op->predicate.get(), op->index.get(), op->value.get()}, calculate_cc,
              calculate_dmc);
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
    set_costs(true, op, children);
    set_costs(false, op, children);
}

void FindStmtCost::visit(const Allocate *op) {
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

    set_costs(false, op, children);

    op->body.accept(this);
    children.push_back(op->body.get());

    set_costs(true, op, children);
}

void FindStmtCost::visit(const Free *op) {
    set_costs(true, op, {});
    set_costs(false, op, {});
}

void FindStmtCost::visit(const Realize *op) {
    vector<const IRNode *> children;

    for (const auto &bound : op->bounds) {
        bound.min.accept(this);
        bound.extent.accept(this);
        children.push_back(bound.min.get());
        children.push_back(bound.extent.get());
    }

    op->condition.accept(this);
    children.push_back(op->condition.get());

    set_costs(false, op, children);

    op->body.accept(this);
    children.push_back(op->body.get());

    set_costs(true, op, children);
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

    set_costs(false, op, children);

    op->body.accept(this);
    children.push_back(op->body.get());

    set_costs(true, op, children);
}

void FindStmtCost::visit(const Block *op) {
    vector<const IRNode *> children;

    op->first.accept(this);
    children.push_back(op->first.get());

    if (op->rest.defined()) {
        op->rest.accept(this);
        children.push_back(op->rest.get());
    }

    set_costs(true, op, children);

    // there is no exclusive computation or data movement for Block
    set_costs(false, op, {});
}

void FindStmtCost::visit(const Fork *op) {
    op->first.accept(this);

    vector<const IRNode *> children;
    children.push_back(op->first.get());

    if (op->rest.defined()) {
        op->rest.accept(this);
        children.push_back(op->rest.get());
    }

    set_costs(true, op, children);
    set_costs(false, op, children);
}

void FindStmtCost::visit(const IfThenElse *op) {
    vector<const IRNode *> main_node_children;

    const IfThenElse *original_op = op;

    while (true) {
        op->condition.accept(this);
        op->then_case.accept(this);

        main_node_children.push_back(op->condition.get());
        main_node_children.push_back(op->then_case.get());

        // inclusive and exclusive costs are the same
        set_costs(false, op, {op->condition.get(), op->then_case.get()});
        set_costs(true, op, {op->condition.get(), op->then_case.get()});

        // if there is no else case, we are done
        if (!op->else_case.defined()) {
            break;
        }

        // if else case is another ifthenelse, recurse and reset op to else case
        if (const IfThenElse *nested_if = op->else_case.as<IfThenElse>()) {
            op = nested_if;
        }

        // if else case is not another ifthenelse
        else {
            op->else_case.accept(this);
            main_node_children.push_back(op->else_case.get());
            break;
        }
    }

    // set op costs - for entire if-statement, inclusive and exclusive costs are the same
    set_costs(false, original_op, main_node_children);
    set_costs(true, original_op, main_node_children);
}

void FindStmtCost::visit(const Evaluate *op) {
    op->value.accept(this);

    vector<int> costs_children = get_costs_children(op, {op->value.get()}, true);

    // inclusive and exclusive costs are the same
    set_costs(true, op, {op->value.get()});
    set_costs(false, op, {op->value.get()});
}

void FindStmtCost::visit(const Atomic *op) {
    op->body.accept(this);

    stringstream name;
    name << op->producer_name;

    set_costs(true, op, {op->body.get()});
    set_costs(false, op, {});
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
        s << "LetStmt type";
        auto node1 = dynamic_cast<const LetStmt *>(node);
        s << "name: " << node1->name;
        s << ", value: " << node1->value << endl;
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
        auto node1 = dynamic_cast<const IfThenElse *>(node);
        s << "IfThenElse type - cond: " << node1->condition << endl;
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
