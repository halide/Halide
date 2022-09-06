#ifndef GETSTMTHIERARCHY_H
#define GETSTMTHIERARCHY_H

#include <set>

#include "FindStmtCost.h"
#include "IROperator.h"
#include "IRVisitor.h"

using namespace std;
using namespace Halide;
using namespace Internal;

class GetStmtHierarchy : public IRVisitor {

public:
    static const string stmtHierarchyCSS;

    GetStmtHierarchy(FindStmtCost findStmtCostPopulated)
        : findStmtCost(findStmtCostPopulated), currNodeID(0), numNodes(0) {
    }

    // returns the generated hierarchy's html
    string get_hierarchy_html(const Expr &startNode);
    string get_hierarchy_html(const Stmt &startNode);

    // special case for else case (node with just "else")
    string get_else_hierarchy_html();

    // generates the JS that is needed to expand/collapse the tree
    string generate_collapse_expand_js();
    string generate_stmtHierarchy_js();

private:
    string html;                // html string
    FindStmtCost findStmtCost;  // used to determine the color of each statement

    // for expanding/collapsing nodes
    uint32_t currNodeID;      // ID of the current node in traversal
    uint32_t numNodes;        // total number of nodes (across both trees in the hierarchy)
    uint32_t startCCNodeID;   // ID of the start node in the CC tree
    uint32_t startDMCNodeID;  // ID of the start node in the DMC tree
    int depth;                // depth of the current node in the tree

    int stmtHierarchyTooltipCount = 0;  // tooltip count

    // updates the currNodeID to be the next available node ID (numNodes)
    // and increases numNodes by 1
    void update_num_nodes();

    // returns the class name in format "node[parentID]child depth[depth]"
    string get_node_class_name();

    // starts and ends the html file
    void start_html();
    void end_html();

    // starts and ends a tree within the html file
    void start_tree();
    void end_tree();

    // creating color divs with tooltips
    void generate_computation_cost_div(const IRNode *op);
    void generate_memory_cost_div(const IRNode *op);

    // opens and closes nodes, depending on number of children
    void node_without_children(const IRNode *op, string name);
    void open_node(const IRNode *op, string name);
    void close_node();

    void visit_binary_op(const IRNode *op, const Expr &a, const Expr &b, const string &name);

    void visit(const IntImm *op) override;
    void visit(const UIntImm *op) override;
    void visit(const FloatImm *op) override;
    void visit(const StringImm *op) override;
    void visit(const Cast *op) override;
    void visit(const Reinterpret *) override;
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
};

#endif
