#ifndef GETSTMTHIERARCHY_H
#define GETSTMTHIERARCHY_H

#include <set>

#include "FindStmtCost.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "IRVisualization.h"

using namespace std;
using namespace Halide;
using namespace Internal;

struct StmtHierarchyInfo {
    string html;     // html code for the node
    int viz_num;     // id for that visualization
    int start_node;  // start node for the visualization
    int end_node;    // end node for the visualization
};

class GetStmtHierarchy : public IRVisitor {

public:
    static const string stmt_hierarchy_css, stmt_hierarchy_collapse_expand_JS;

    GetStmtHierarchy(FindStmtCost find_stmt_cost_populated)
        : find_stmt_cost(find_stmt_cost_populated), ir_viz(find_stmt_cost_populated),
          curr_node_ID(0), num_nodes(0), viz_counter(0), stmt_hierarchy_tooltip_count(0) {
    }

    // returns the generated hierarchy's html
    StmtHierarchyInfo get_hierarchy_html(const Expr &node);
    StmtHierarchyInfo get_hierarchy_html(const Stmt &node);

    // special case for else case (node with just "else")
    StmtHierarchyInfo get_else_hierarchy_html();

    // generates the JS that is needed to add the tooltips
    string generate_stmt_hierarchy_js();

private:
    stringstream html;            // html string
    FindStmtCost find_stmt_cost;  // used as input to IRVisualization
    IRVisualization ir_viz;       // used to generate the tooltip information and cost colors

    // for expanding/collapsing nodes
    int curr_node_ID;   // ID of the current node in traversal
    int num_nodes;      // total number of nodes (across all generated trees in the IR)
    int start_node_id;  // ID of the start node of the current tree
    int node_depth;     // depth of the current node in the tree
    int viz_counter;    // counter for the number of visualizations
    int stmt_hierarchy_tooltip_count;  // tooltip count

    // updates the curr_node_ID to be the next available node ID (num_nodes)
    // and increases num_nodes by 1
    void update_num_nodes();

    // returns the class name in format "node[parentID]child depth[depth]"
    string get_node_class_name();

    // resets all the variables to start a new tree
    void reset_variables();

    // starts and ends a tree within the html file
    string start_tree() const;
    string end_tree() const;

    // creating color divs with tooltips
    string generate_computation_cost_div(const IRNode *op);
    string generate_memory_cost_div(const IRNode *op);

    // opens and closes nodes, depending on number of children
    string node_without_children(const IRNode *op, string name);
    string open_node(const IRNode *op, string name);
    string close_node();

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
