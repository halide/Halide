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

#define m_assert(expr, msg) assert((void(msg), (expr)))

struct StmtCost {
    int cost;   // per line
    int depth;  // per nested loop

    // add other costs later, like integer-ALU cost, float-ALU cost, memory cost, etc.
};

class CostPreProcessor : public IRMutator {
public:
    CostPreProcessor() = default;
    ~CostPreProcessor() = default;

    // starts the traveral based on Module
    void traverse(const Module &m);

    int get_count(const string name) const;

private:
    map<const string, int> lock_access_counts;

    void increase_count(const string name);

    Stmt visit(const Acquire *op) override;
    Stmt visit(const Atomic *op) override;
};

class FindStmtCost : public IRMutator {

public:
    FindStmtCost() = default;
    ~FindStmtCost() = default;

    // returns the range of node based on its
    int get_range(const IRNode *op) const;

    // calculates the total cost of a stmt
    int get_total_cost(const IRNode *node) const;

    int get_depth(const IRNode *node) const;

    void generate_costs(const Module &m);
    void generate_costs(const Stmt &stmt);

private:
    // create variable that will hold mapping of stmt to cost
    unordered_map<const IRNode *, StmtCost> stmt_cost;

    // for Atomic and Acquire
    CostPreProcessor cost_preprocessor;

    // stores current loop depth level
    int current_loop_depth = 0;

    // starts the traveral based on Module
    void traverse(const Module &m);

    // gets cost from `stmt_cost` map
    int get_cost(const IRNode *node) const;

    // sets cost & depth in `stmt_cost` map
    void set_cost(const IRNode *node, int cost);

    // calculates cost of a signle StmtCost object
    int calculate_cost(StmtCost cost_node) const;

    void print_map(unordered_map<const IRNode *, StmtCost> const &m);

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
