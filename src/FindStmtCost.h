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

#define m_assert(expr, msg) assert((void(msg), (expr)))

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

    // starts the traveral based on Module
    void traverse(const Module &m);

    int get_count(const string name) const;

private:
    map<const string, int> lock_access_counts;
    // vector<string> variables_in_context;
    // vector<string> variables_in_loop;

    // bool is_in_context(const string name) const;
    // void add_to_context(const string name);
    // void remove_from_context(const string name);

    // bool is_loop_variable(const string name) const;
    // void add_to_loop(const string name);

    void increase_count(const string name);

    Stmt visit(const Acquire *op) override;
    Stmt visit(const Atomic *op) override;
    // Expr visit(const Variable *op) override;
    // Stmt visit(const For *op) override;
};

/*
 * FindStmtCost class
 */
class FindStmtCost : public IRMutator {

public:
    FindStmtCost() = default;
    ~FindStmtCost() = default;

    // returns the range of node based on its cost
    int get_computation_range(const IRNode *op) const;
    int get_data_movement_range(const IRNode *op) const;

    // calculates the total costs of a stmt
    int get_depth(const IRNode *node) const;
    int get_computation_cost(const IRNode *node) const;
    int get_data_movement_cost(const IRNode *node) const;

    bool requires_context(const IRNode *node, const string name) const;

    void generate_costs(const Module &m);
    void generate_costs(const Stmt &stmt);

    // bool is_loop_variable(const string name) const {
    //     return find(variables_in_loop.begin(), variables_in_loop.end(), name) !=
    //            variables_in_loop.end();
    // }
    // void add_loop_variable(const string name) {
    //     variables_in_loop.push_back(name);
    // }

private:
    // holds mapping of stmt to cost
    unordered_map<const IRNode *, StmtCost> stmt_cost;

    // holds mapping of stmt to whether its in context or not
    // unordered_map<const IRNode *, bool> requires_context_map;
    // map<const string, bool> variable_map;
    // TODO: change these variables so they don't all sound the same

    // vector<string> curr_context;
    // vector<string> loop_vars;

    // TODO: remove once i know it's not needed anymore
    bool in_loop;

    // for Atomic and Acquire
    CostPreProcessor cost_preprocessor;

    // stores current loop depth level
    int current_loop_depth = 0;

    // bool in_curr_context(const string name) const;
    // void add_curr_context(const string name);
    // // void remove_curr_context(const string name);
    // void remove_curr_context(const vector<string> &curr_loop_vars);

    // gets/sets context from `requires_context_map` map
    // TODO: remove this once i know it's not needed
    bool get_context(const IRNode *node, const string name) const;
    void set_context(const IRNode *node, bool context);

    // bool is_loop_var(const string name) const;
    // void add_loop_var(const string name);

    // gets/sets variable context from `variable_map` map
    // bool get_from_variable_map(const string name) const;
    // void add_variable_map(const string name, bool context);

    // starts the traveral based on Module
    void traverse(const Module &m);

    // gets/sets cost from `stmt_cost` map
    int get_cost(const IRNode *node) const;
    void set_costs(const IRNode *node, int computation_cost, int data_movement_cost);

    // calculates cost of a signle StmtCost object
    int calculate_cost(StmtCost cost_node) const;

    // gets scaling factor for Load/Store based on lanes and bits
    int get_scaling_factor(uint8_t bits, uint16_t lanes) const;

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
};

#endif  // FINDSTMTCOST_H
