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

class GetStmtHierarchy : public IRMutator {

public:
    GetStmtHierarchy(FindStmtCost findStmtCostPopulated) : findStmtCost(findStmtCostPopulated) {
    }
    ~GetStmtHierarchy() = default;

    // returns the generated hierarchy's html
    string get_hierarchy_html(const Expr &startNode);
    string get_hierarchy_html(const Stmt &startNode);

private:
    int colorType;              // 0: CC (computation cost), 1: DMC (data movement cost)
    std::stringstream html;     // html string
    FindStmtCost findStmtCost;  // used to determine the color of each statement

    // for expanding/collapsing
    int currNodeID;      // ID of the current node in traversal
    int numNodes;        // total number of nodes (across both trees in the hierarchy)
    int startCCNodeID;   // ID of the start node in the CC tree
    int startDMCNodeID;  // ID of the start node in the DMC tree
    int depth;           // depth of the current node in the tree

    // updates the currNodeID to be the next available node ID (numNodes)
    // and increases numNodes by 1
    void update_num_nodes();

    // returns the class name in format "node[parentID]child depth[depth]"
    string get_node_class_name();

    // gets the color range of the current node so that it can be colored
    // appropriately in the html file
    int get_color_range(const IRNode *op) const;
    int get_color_range_list(vector<Halide::Expr> exprs) const;

    // starts and ends the html file
    void start_html();
    void end_html();

    // starts and ends a tree within the html file
    void start_tree();
    void end_tree();

    // generates the JS that is needed to expand/collapse the tree
    string generate_collapse_expand_js(int totalNodes);

    // opens and closes nodes, depending on number of children
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
