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

#define DEPTH_COST 3
#define NUMBER_COST_COLORS 20
#define LOAD_COST 2
#define STORE_COST 3

#define LOAD_LOCAL_VAR_COST 3
#define LOAD_GLOBAL_VAR_COST 10

/*
 * StmtCost struct
 */
struct StmtCost {
    int depth;               // per nested loop
    int computation_cost;    // per line
    int data_movement_cost;  // per line

    // add other costs later, like integer-ALU cost, float-ALU cost, memory cost, etc.
};

/*
 * CostPreProcessor class
 */
class CostPreProcessor : public IRVisitor {
public:
    CostPreProcessor() = default;
    ~CostPreProcessor() = default;

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
    FindStmtCost() = default;
    ~FindStmtCost() = default;

    // starts the traversal of the given node
    void generate_costs(const Module &m);
    void generate_costs(const Stmt &stmt);

    // returns the range of the node's cost based on the other nodes' costs
    int get_computation_color_range(const IRNode *op) const;
    int get_data_movement_color_range(const IRNode *op) const;

    // calculates the total costs and depth of a node
    int get_depth(const IRNode *node) const;
    int get_calculated_computation_cost(const IRNode *node) const;
    int get_data_movement_cost(const IRNode *node) const;

    bool is_local_variable(const string &name) const;

private:
    unordered_map<const IRNode *, StmtCost> stmt_cost;  // key: node, value: StmtCost
    CostPreProcessor cost_preprocessor;                 // for Atomic and Acquire
    int current_loop_depth = 0;                         // stores current loop depth level
    vector<string> allocate_variables;                  // stores all allocate variables
    int max_computation_cost = 0;                       // stores the maximum computation cost
    int max_data_movement_cost = 0;                     // stores the maximum data movement cost

    // starts the traversal based on Module
    void traverse(const Module &m);

    // gets/sets cost from `stmt_cost` map
    int get_computation_cost(const IRNode *node) const;
    void set_costs(const IRNode *node, int computation_cost, int data_movement_cost);

    // calculates cost of a single StmtCost object
    int calculate_cost(StmtCost cost_node) const;

    // gets max computation cost and max data movement cost
    void set_max_costs();

    // gets scaling factor for Load/Store based on lanes and bits
    int get_scaling_factor(uint8_t bits, uint16_t lanes) const;

    // prints the Node->StmtCost map
    void print_map(unordered_map<const IRNode *, StmtCost> const &m);

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
