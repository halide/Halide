#ifndef FINDSTMTCOST_H
#define FINDSTMTCOST_H

#include "ExternFuncArgument.h"
#include "Function.h"
#include "IRVisitor.h"
#include "Module.h"

#include <stdexcept>
#include <unordered_map>

using namespace Halide;
using namespace Internal;
using namespace std;

#define NORMAL_NODE_CC 1
#define NORMAL_NODE_DMC 0
#define NORMAL_SCALE_FACTOR_DMC 1
#define DEPTH_COST 3
#define LOAD_DM_COST 2
#define STORE_DM_COST 3
#define LOAD_LOCAL_VAR_COST 3
#define LOAD_GLOBAL_VAR_COST 10

#define NUMBER_COST_COLORS 20

/*
 * StmtCost struct
 */
struct StmtCost {
    int depth;                         // per nested loop
    int computation_cost_inclusive;    // per line
    int data_movement_cost_inclusive;  // per line
    int computation_cost_exclusive;    // per line
    int data_movement_cost_exclusive;  // per line
};

/*
 * CostPreProcessor class
 */
class CostPreProcessor : public IRVisitor {
public:
    // starts the traversal based on Module
    void traverse(const Module &m);
    void traverse(const Stmt &s);

    // returns the number of lock accesses of the given lock name
    int get_lock_access_count(const string name) const;

private:
    using IRVisitor::visit;

    map<const string, int> lock_access_counts;  // key: lock name, value: number of accesses

    // increases the lock access count
    void increase_count(const string name);

    void visit(const Acquire *op) override;
    void visit(const Atomic *op) override;
};

/*
 * FindStmtCost class
 */
class FindStmtCost : public IRVisitor {

public:
    // starts the traversal of the given node
    void generate_costs(const Module &m);
    void generate_costs(const Stmt &stmt);

    // generates tooltip information based on given node
    string generate_computation_cost_tooltip(const IRNode *op, bool inclusive, string extraNote);
    string generate_data_movement_cost_tooltip(const IRNode *op, bool inclusive, string extraNote);

    // returns the range of the node's cost based on the other nodes' costs
    int get_computation_color_range(const IRNode *op, bool inclusive) const;
    int get_data_movement_color_range(const IRNode *op, bool inclusive) const;

    // for when blocks are collapsed in code viz
    int get_combined_computation_color_range(const IRNode *op) const;
    int get_combined_data_movement_color_range(const IRNode *op) const;

    // is local (defined in Allocate block) or not
    bool is_local_variable(const string &name) const;

private:
    unordered_map<const IRNode *, StmtCost> stmt_cost;  // key: node, value: StmtCost
    CostPreProcessor cost_preprocessor;                 // for Atomic and Acquire
    int current_loop_depth = 0;                         // stores current loop depth level
    vector<string> allocate_variables;                  // stores all allocate variables

    int max_computation_cost_inclusive = 0;    // stores the maximum computation cost (inclusive)
    int max_data_movement_cost_inclusive = 0;  // stores the maximum data movement cost (inclusive)
    int max_computation_cost_exclusive = 0;    // stores the maximum computation cost (exclusive)
    int max_data_movement_cost_exclusive = 0;  // stores the maximum data movement cost (exclusive)

    // starts the traversal based on Module
    void traverse(const Module &m);

    // calculates the total costs and depth of a node
    int get_depth(const IRNode *node) const;
    int get_computation_cost(const IRNode *node, bool inclusive) const;
    int get_data_movement_cost(const IRNode *node, bool inclusive) const;

    // gets costs from `stmt_cost` map
    vector<int> get_costs_children(const IRNode *parent, vector<const IRNode *> children,
                                   bool inclusive) const;

    // sets costs
    void set_inclusive_costs(const IRNode *node, vector<const IRNode *> children, int node_cc,
                             int node_dmc, int scalingFactor_dmc);
    void set_exclusive_costs(const IRNode *node, vector<const IRNode *> children, int node_cc,
                             int node_dmc, int scalingFactor_dmc);

    // gets max computation cost and max data movement cost
    void set_max_costs();

    // TODO: comment
    string tooltip_table(map<string, string> &table, string extraNote);

    // gets scaling factor for Load/Store based on lanes and bits
    int get_scaling_factor(uint8_t bits, uint16_t lanes) const;

    void visit_binary_op(const IRNode *op, const Expr &a, const Expr &b);

    void visit(const IntImm *op) override;
    void visit(const UIntImm *op) override;
    void visit(const FloatImm *op) override;
    void visit(const StringImm *op) override;
    void visit(const Cast *op) override;
    void visit(const Reinterpret *op) override;
    void visit(const Variable *op) override;
    void visit(const Add *op) override;
    void visit(const Sub *op) override;
    void visit(const Mul *op) override;
    void visit(const Div *op) override;
    void visit(const Mod *op) override;
    void visit(const Min *op) override;
    void visit(const Max *op) override;
    void visit(const EQ *op) override;
    void visit(const NE *op) override;
    void visit(const LT *op) override;
    void visit(const LE *op) override;
    void visit(const GT *op) override;
    void visit(const GE *op) override;
    void visit(const And *op) override;
    void visit(const Or *op) override;
    void visit(const Not *op) override;
    void visit(const Select *op) override;
    void visit(const Load *op) override;
    void visit(const Ramp *op) override;
    void visit(const Broadcast *op) override;
    void visit(const Call *op) override;
    void visit(const Let *op) override;
    void visit(const Shuffle *op) override;
    void visit(const VectorReduce *op) override;
    void visit(const LetStmt *op) override;
    void visit(const AssertStmt *op) override;
    void visit(const ProducerConsumer *op) override;
    void visit(const For *op) override;
    void visit(const Acquire *op) override;
    void visit(const Store *op) override;
    void visit(const Provide *op) override;
    void visit(const Allocate *op) override;
    void visit(const Free *op) override;
    void visit(const Realize *op) override;
    void visit(const Prefetch *op) override;
    void visit(const Block *op) override;
    void visit(const Fork *op) override;
    void visit(const IfThenElse *op) override;
    void visit(const Evaluate *op) override;
    void visit(const Atomic *op) override;

    string print_node(const IRNode *node) const;
};

#endif  // FINDSTMTCOST_H
