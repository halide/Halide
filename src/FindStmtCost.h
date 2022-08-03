#ifndef FINDSTMTCOST_H
#define FINDSTMTCOST_H

#include "ExternFuncArgument.h"
#include "Function.h"
#include "IRMutator.h"
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
class CostPreProcessor : public IRMutator {
public:
    CostPreProcessor() = default;
    ~CostPreProcessor() = default;

    // starts the traversal based on Module
    void traverse(const Module &m);

    // returns the number of lock accesses of the given lock name
    int get_lock_access_count(const string name) const;

private:
    using IRMutator::visit;

    map<const string, int> lock_access_counts;  // key: lock name, value: number of accesses

    // increases the lock access count
    void increase_count(const string name);

    Stmt visit(const Acquire *op) override;
    Stmt visit(const Atomic *op) override;
};

/*
 * FindStmtCost class
 */
class FindStmtCost : public IRMutator {

public:
    FindStmtCost() = default;
    ~FindStmtCost() = default;

    // starts the traversal of the given node
    void generate_costs(const Module &m);
    void generate_costs(const Stmt &stmt);

    // returns the range of the node's cost based on the other nodes' costs
    int get_computation_range(const IRNode *op) const;
    int get_data_movement_range(const IRNode *op) const;

    // calculates the total costs and depth of a node
    int get_depth(const IRNode *node) const;
    int calculate_computation_cost(const IRNode *node) const;
    int get_data_movement_cost(const IRNode *node) const;

private:
    unordered_map<const IRNode *, StmtCost> stmt_cost;  // key: node, value: StmtCost
    CostPreProcessor cost_preprocessor;                 // for Atomic and Acquire
    int current_loop_depth = 0;                         // stores current loop depth level

    // starts the traversal based on Module
    void traverse(const Module &m);

    // gets/sets cost from `stmt_cost` map
    int get_computation_cost(const IRNode *node) const;
    void set_costs(const IRNode *node, int computation_cost, int data_movement_cost);

    // calculates cost of a single StmtCost object
    int calculate_cost(StmtCost cost_node) const;

    // gets scaling factor for Load/Store based on lanes and bits
    int get_scaling_factor(uint8_t bits, uint16_t lanes) const;

    // prints the Node->StmtCost map
    void print_map(unordered_map<const IRNode *, StmtCost> const &m);

    void visit_binary_op(const IRNode *op, const Expr &a, const Expr &b);

    Expr visit(const IntImm *op) override;
    Expr visit(const UIntImm *op) override;
    Expr visit(const FloatImm *op) override;
    Expr visit(const StringImm *op) override;
    Expr visit(const Cast *op) override;
    Expr visit(const Variable *op) override;
    Expr visit(const Add *op) override;
    Expr visit(const Sub *op) override;
    Expr visit(const Mul *op) override;
    Expr visit(const Div *op) override;
    Expr visit(const Mod *op) override;
    Expr visit(const Min *op) override;
    Expr visit(const Max *op) override;
    Expr visit(const EQ *op) override;
    Expr visit(const NE *op) override;
    Expr visit(const LT *op) override;
    Expr visit(const LE *op) override;
    Expr visit(const GT *op) override;
    Expr visit(const GE *op) override;
    Expr visit(const And *op) override;
    Expr visit(const Or *op) override;
    Expr visit(const Not *op) override;
    Expr visit(const Select *op) override;
    Expr visit(const Load *op) override;
    Expr visit(const Ramp *op) override;
    Expr visit(const Broadcast *op) override;
    Expr visit(const Call *op) override;
    Expr visit(const Let *op) override;
    Expr visit(const Shuffle *op) override;
    Expr visit(const VectorReduce *op) override;
    Stmt visit(const LetStmt *op) override;
    Stmt visit(const AssertStmt *op) override;
    Stmt visit(const ProducerConsumer *op) override;
    Stmt visit(const For *op) override;
    Stmt visit(const Acquire *op) override;
    Stmt visit(const Store *op) override;
    Stmt visit(const Provide *op) override;
    Stmt visit(const Allocate *op) override;
    Stmt visit(const Free *op) override;
    Stmt visit(const Realize *op) override;
    Stmt visit(const Prefetch *op) override;
    Stmt visit(const Block *op) override;
    Stmt visit(const Fork *op) override;
    Stmt visit(const IfThenElse *op) override;
    Stmt visit(const Evaluate *op) override;
    Stmt visit(const Atomic *op) override;

    string print_node(const IRNode *node) const;
};

#endif  // FINDSTMTCOST_H
