#ifndef GETSTMTHIERARCHY_H
#define GETSTMTHIERARCHY_H

#include <set>

#include "FindStmtCost.h"
#include "IRMutator.h"
#include "IROperator.h"

using namespace std;
using namespace Halide;
using namespace Internal;

#define CC_TYPE 0
#define DMC_TYPE 1

#define m_assert(expr, msg) assert((void(msg), (expr)))

class GetStmtHierarchy : public IRMutator {

public:
    GetStmtHierarchy() = default;
    ~GetStmtHierarchy() = default;

    string get_hierarchy_html(const Expr &startNode);

    string get_hierarchy_html(const Stmt &startNode);

    void set_stmt_cost(const Module &m);
    void set_stmt_cost(const Stmt &s);

private:
    int colorType;
    std::stringstream html;
    FindStmtCost findStmtCost;
    int currNodeID;
    int numNodes;
    int startCCNodeID;
    int startDMCNodeID;

    void update_num_nodes();

    string get_node_class_name();

    string get_cost(const IRNode *node) const;
    string get_cost_list(vector<Halide::Expr> exprs) const;

    int get_range(const IRNode *op) const;
    int get_range_list(vector<Halide::Expr> exprs) const;

    void start_html();
    void end_html();

    void start_tree();
    void end_tree();

    string generate_collapse_expand_js();

    void node_without_children(string name, int colorCost);
    void open_node(string name, int colorCost);
    void close_node();

    void visit_binary_op(const Expr &a, const Expr &b, const string &name, int colorCost);

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

#endif
