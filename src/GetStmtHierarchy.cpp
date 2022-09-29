#include "GetStmtHierarchy.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;

StmtHierarchyInfo GetStmtHierarchy::get_hierarchy_html(const Expr &node) {
    reset_variables();

    int start_node = curr_node_ID;
    html << start_tree();
    node.accept(this);
    html << end_tree();
    int end_node = num_nodes;

    StmtHierarchyInfo info;
    info.html = html.str();
    info.viz_num = viz_counter;
    info.start_node = start_node;
    info.end_node = end_node;

    return info;
}
StmtHierarchyInfo GetStmtHierarchy::get_hierarchy_html(const Stmt &node) {
    reset_variables();

    int start_node = curr_node_ID;
    html << start_tree();
    node.accept(this);
    html << end_tree();
    int end_node = num_nodes;

    StmtHierarchyInfo info;
    info.html = html.str();
    info.viz_num = viz_counter;
    info.start_node = start_node;
    info.end_node = end_node;

    return info;
}

StmtHierarchyInfo GetStmtHierarchy::get_else_hierarchy_html() {
    reset_variables();

    int start_node = curr_node_ID;
    html << start_tree();
    html << node_without_children(nullptr, "else");
    html << end_tree();
    int end_node = num_nodes;

    StmtHierarchyInfo info;
    info.html = html.str();
    info.viz_num = viz_counter;
    info.start_node = start_node;
    info.end_node = end_node;

    return info;
}

void GetStmtHierarchy::update_num_nodes() {
    num_nodes++;
    curr_node_ID = num_nodes;
}

string GetStmtHierarchy::get_node_class_name() {
    ostringstream class_name;
    if (curr_node_ID == start_node_id) {
        class_name << "viz" << viz_counter << " startNode depth" << node_depth;
    } else {
        class_name << "viz" << viz_counter << " node" << curr_node_ID << "child depth"
                   << node_depth;
    }
    return class_name.str();
}

void GetStmtHierarchy::reset_variables() {
    html.str("");
    num_nodes++;
    curr_node_ID = num_nodes;
    start_node_id = -1;
    node_depth = 0;
    start_node_id = num_nodes;
    viz_counter++;
}

string GetStmtHierarchy::start_tree() const {
    ostringstream ss;
    ss << "<div class='treeDiv'>";
    ss << "<div class='tf-tree tf-gap-sm tf-custom-stmtHierarchy'>";
    ss << "<ul>";
    return ss.str();
}
string GetStmtHierarchy::end_tree() const {
    ostringstream ss;
    ss << "</ul>";
    ss << "</div>";
    ss << "</div>";
    return ss.str();
}

string GetStmtHierarchy::generate_computation_cost_div(const IRNode *op) {
    stmt_hierarchy_tooltip_count++;

    ostringstream ss;
    string tooltip_text = ir_viz.generate_computation_cost_tooltip(op, "");

    // tooltip span
    ss << "<span id='stmtHierarchyTooltip" << stmt_hierarchy_tooltip_count
       << "' class='tooltip CostTooltip' "
       << "role='stmtHierarchyTooltip" << stmt_hierarchy_tooltip_count << "'>" << tooltip_text
       << "</span>";

    // color div
    int computation_range = ir_viz.get_color_range(op, false, true);
    string class_name = "computation-cost-div CostColor" + std::to_string(computation_range);
    ss << "<div id='stmtHierarchyButtonTooltip" << stmt_hierarchy_tooltip_count << "' ";
    ss << "aria-describedby='stmtHierarchyTooltip" << stmt_hierarchy_tooltip_count << "' ";
    ss << "class='" << class_name << "'>";
    ss << "</div>";

    return ss.str();
}
string GetStmtHierarchy::generate_memory_cost_div(const IRNode *op) {
    stmt_hierarchy_tooltip_count++;

    ostringstream ss;
    string tooltip_text = ir_viz.generate_data_movement_cost_tooltip(op, "");

    // tooltip span
    ss << "<span id='stmtHierarchyTooltip" << stmt_hierarchy_tooltip_count
       << "' class='tooltip CostTooltip' "
       << "role='stmtHierarchyTooltip" << stmt_hierarchy_tooltip_count << "'>" << tooltip_text
       << "</span>";

    // color div
    int data_movement_range = ir_viz.get_color_range(op, false, false);
    string class_name = "memory-cost-div CostColor" + std::to_string(data_movement_range);
    ss << "<div id='stmtHierarchyButtonTooltip" << stmt_hierarchy_tooltip_count << "' "
       << "aria-describedby='stmtHierarchyTooltip" << stmt_hierarchy_tooltip_count << "' "
       << "class='" << class_name << "'>"
       << "</div>";

    return ss.str();
}

string GetStmtHierarchy::node_without_children(const IRNode *op, string name) {
    ostringstream ss;

    string class_name = get_node_class_name();
    ss << "<li class='" << class_name << "'>"
       << "<span class='tf-nc end-node'>";

    ss << "<div class='nodeContent'>";
    ss << generate_computation_cost_div(op);
    ss << generate_memory_cost_div(op);

    ss << "<div class='nodeName'>" << name << "</div>"
       << "</div>"
       << "</span>"
       << "</li>";

    return ss.str();
}
string GetStmtHierarchy::open_node(const IRNode *op, string name) {
    ostringstream ss;
    string class_name = get_node_class_name() + " children-node";

    update_num_nodes();

    ss << "<li class='" << class_name << "' id='node" << curr_node_ID << "'>";
    ss << "<span class='tf-nc'>";

    ss << "<div class='nodeContent'>";
    ss << generate_computation_cost_div(op);
    ss << generate_memory_cost_div(op);

    ss << "<div class='nodeName'>" << name
       << "<button class='stmtHierarchyButton infoButton' onclick='handleClick(" << curr_node_ID
       << ")'>"
       << "<i id='stmtHierarchyButton" << curr_node_ID << "'></i> "
       << "</button>"
       << "</div>"
       << "</div>"
       << "</span>";

    node_depth++;
    ss << "<ul id='list" << curr_node_ID << "'>";

    return ss.str();
}
string GetStmtHierarchy::close_node() {
    node_depth--;
    ostringstream ss;
    ss << "</ul>";
    ss << "</li>";
    return ss.str();
}

void GetStmtHierarchy::visit(const IntImm *op) {
    html << node_without_children(op, std::to_string(op->value));
}
void GetStmtHierarchy::visit(const UIntImm *op) {
    html << node_without_children(op, std::to_string(op->value));
}
void GetStmtHierarchy::visit(const FloatImm *op) {
    html << node_without_children(op, std::to_string(op->value));
}
void GetStmtHierarchy::visit(const StringImm *op) {
    html << node_without_children(op, op->value);
}
void GetStmtHierarchy::visit(const Cast *op) {
    ostringstream name;
    name << op->type;
    html << open_node(op, name.str());
    op->value.accept(this);
    html << close_node();
}
void GetStmtHierarchy::visit(const Reinterpret *op) {
    ostringstream name;
    name << "reinterpret ";
    name << op->type;
    html << open_node(op, name.str());
    op->value.accept(this);
    html << close_node();
}
void GetStmtHierarchy::visit(const Variable *op) {
    html << node_without_children(op, op->name);
}

void GetStmtHierarchy::visit_binary_op(const IRNode *op, const Expr &a, const Expr &b,
                                       const string &name) {
    html << open_node(op, name);

    int curr_node = curr_node_ID;
    a.accept(this);

    curr_node_ID = curr_node;
    b.accept(this);

    html << close_node();
}

void GetStmtHierarchy::visit(const Add *op) {
    visit_binary_op(op, op->a, op->b, "+");
}
void GetStmtHierarchy::visit(const Sub *op) {
    visit_binary_op(op, op->a, op->b, "-");
}
void GetStmtHierarchy::visit(const Mul *op) {
    visit_binary_op(op, op->a, op->b, "*");
}
void GetStmtHierarchy::visit(const Div *op) {
    visit_binary_op(op, op->a, op->b, "/");
}
void GetStmtHierarchy::visit(const Mod *op) {
    visit_binary_op(op, op->a, op->b, "%");
}
void GetStmtHierarchy::visit(const EQ *op) {
    visit_binary_op(op, op->a, op->b, "==");
}
void GetStmtHierarchy::visit(const NE *op) {
    visit_binary_op(op, op->a, op->b, "!=");
}
void GetStmtHierarchy::visit(const LT *op) {
    visit_binary_op(op, op->a, op->b, "<");
}
void GetStmtHierarchy::visit(const LE *op) {
    visit_binary_op(op, op->a, op->b, "<=");
}
void GetStmtHierarchy::visit(const GT *op) {
    visit_binary_op(op, op->a, op->b, ">");
}
void GetStmtHierarchy::visit(const GE *op) {
    visit_binary_op(op, op->a, op->b, ">=");
}
void GetStmtHierarchy::visit(const And *op) {
    visit_binary_op(op, op->a, op->b, "&amp;&amp;");
}
void GetStmtHierarchy::visit(const Or *op) {
    visit_binary_op(op, op->a, op->b, "||");
}
void GetStmtHierarchy::visit(const Min *op) {
    visit_binary_op(op, op->a, op->b, "min");
}
void GetStmtHierarchy::visit(const Max *op) {
    visit_binary_op(op, op->a, op->b, "max");
}

void GetStmtHierarchy::visit(const Not *op) {
    html << open_node(op, "!");
    op->a.accept(this);
    html << close_node();
}
void GetStmtHierarchy::visit(const Select *op) {
    html << open_node(op, "Select");

    int curr_node = curr_node_ID;
    op->condition.accept(this);

    curr_node_ID = curr_node;
    op->true_value.accept(this);

    curr_node_ID = curr_node;
    op->false_value.accept(this);

    html << close_node();
}
void GetStmtHierarchy::visit(const Load *op) {
    ostringstream index;
    index << op->index;
    html << node_without_children(op, op->name + "[" + index.str() + "]");
}
void GetStmtHierarchy::visit(const Ramp *op) {
    html << open_node(op, "Ramp");

    int curr_node = curr_node_ID;
    op->base.accept(this);

    curr_node_ID = curr_node;
    op->stride.accept(this);

    curr_node_ID = curr_node;
    Expr(op->lanes).accept(this);

    html << close_node();
}
void GetStmtHierarchy::visit(const Broadcast *op) {
    html << open_node(op, "x" + std::to_string(op->lanes));
    op->value.accept(this);
    html << close_node();
}
void GetStmtHierarchy::visit(const Call *op) {
    html << open_node(op, op->name);

    int curr_node = curr_node_ID;
    for (auto &arg : op->args) {
        curr_node_ID = curr_node;
        arg.accept(this);
    }

    html << close_node();
}
void GetStmtHierarchy::visit(const Let *op) {
    html << open_node(op, "Let");
    int curr_node = curr_node_ID;

    html << open_node(op->value.get(), "Let");
    html << node_without_children(nullptr, op->name);
    op->value.accept(this);
    html << close_node();

    // "body" node
    curr_node_ID = curr_node;
    html << open_node(op->body.get(), "body");
    op->body.accept(this);
    html << close_node();

    html << close_node();
}
void GetStmtHierarchy::visit(const LetStmt *op) {
    html << open_node(op, "Let");

    int curr_node = curr_node_ID;
    html << node_without_children(nullptr, op->name);

    curr_node_ID = curr_node;
    op->value.accept(this);

    html << close_node();
}
void GetStmtHierarchy::visit(const AssertStmt *op) {
    html << open_node(op, "Assert");

    int curr_node = curr_node_ID;
    op->condition.accept(this);

    curr_node_ID = curr_node;
    op->message.accept(this);
    html << close_node();
}
void GetStmtHierarchy::visit(const ProducerConsumer *op) {
    string node_name = op->is_producer ? "Produce" : "Consume";
    node_name += " " + op->name;
    html << node_without_children(op, node_name);
}
void GetStmtHierarchy::visit(const For *op) {
    html << open_node(op, "For");

    int curr_node = curr_node_ID;
    html << open_node(nullptr, "loop var");
    html << node_without_children(nullptr, op->name);
    html << close_node();

    curr_node_ID = curr_node;
    html << open_node(op->min.get(), "min");
    op->min.accept(this);
    html << close_node();

    curr_node_ID = curr_node;
    html << open_node(op->extent.get(), "extent");
    op->extent.accept(this);
    html << close_node();

    html << close_node();
}
void GetStmtHierarchy::visit(const Store *op) {
    html << open_node(op, "Store");

    ostringstream index;
    index << op->index;
    html << node_without_children(op->index.get(), op->name + "[" + index.str() + "]");

    op->value.accept(this);
    html << close_node();
}
void GetStmtHierarchy::visit(const Provide *op) {
    html << open_node(op, "Provide");
    int curr_node0 = curr_node_ID;

    html << open_node(op, op->name);
    int curr_node1 = curr_node_ID;
    for (auto &arg : op->args) {
        curr_node_ID = curr_node1;
        arg.accept(this);
    }
    html << close_node();

    for (auto &val : op->values) {
        curr_node_ID = curr_node0;
        val.accept(this);
    }
    html << close_node();
}
void GetStmtHierarchy::visit(const Allocate *op) {
    html << open_node(op, "allocate");

    ostringstream index;
    index << op->type;

    for (const auto &extent : op->extents) {
        index << " * ";
        index << extent;
    }

    html << node_without_children(op, op->name + "[" + index.str() + "]");

    ostringstream name;
    if (!is_const_one(op->condition)) {
        name << " if " << op->condition;
    }

    if (op->new_expr.defined()) {
        internal_assert(false) << "\n"
                               << "GetStmtHierarchy: Allocate " << op->name
                               << " `op->new_expr.defined()` is not supported yet.\n\n";
    }
    if (!op->free_function.empty()) {
        name << "custom_delete {" << op->free_function << "}";
    }

    if (name.str() != "") html << node_without_children(op, name.str());
    html << close_node();
}
void GetStmtHierarchy::visit(const Free *op) {
    html << open_node(op, "Free");
    html << node_without_children(op, op->name);
    html << close_node();
}
void GetStmtHierarchy::visit(const Realize *op) {
    internal_assert(false) << "\n"
                           << "GetStmtHierarchy: Realize is not supported yet \n\n";
}
void GetStmtHierarchy::visit(const Block *op) {
    internal_assert(false) << "\n"
                           << "GetStmtHierarchy: Block is not supported and should never be visualized. \n\n";
}
void GetStmtHierarchy::visit(const IfThenElse *op) {
    html << open_node(op, "If");

    html << open_node(op->condition.get(), "condition");
    op->condition.accept(this);
    html << close_node();

    // don't visualize else case because that will be visualized later as another IfThenElse block
    // in StmtToViz.cpp

    html << close_node();
}
void GetStmtHierarchy::visit(const Evaluate *op) {
    op->value.accept(this);
}
void GetStmtHierarchy::visit(const Shuffle *op) {
    if (op->is_concat()) {
        html << open_node(op, "concat_vectors");

        int curr_node = curr_node_ID;
        for (auto &e : op->vectors) {
            curr_node_ID = curr_node;
            e.accept(this);
        }
        html << close_node();
    }

    else if (op->is_interleave()) {
        html << open_node(op, "interleave_vectors");

        int curr_node = curr_node_ID;
        for (auto &e : op->vectors) {
            curr_node_ID = curr_node;
            e.accept(this);
        }
        html << close_node();
    }

    else if (op->is_extract_element()) {
        std::vector<Expr> args = op->vectors;
        args.emplace_back(op->slice_begin());
        html << open_node(op, "extract_element");

        int curr_node = curr_node_ID;
        for (auto &e : args) {
            curr_node_ID = curr_node;
            e.accept(this);
        }
        html << close_node();
    }

    else if (op->is_slice()) {
        std::vector<Expr> args = op->vectors;
        args.emplace_back(op->slice_begin());
        args.emplace_back(op->slice_stride());
        args.emplace_back(static_cast<int>(op->indices.size()));
        html << open_node(op, "slice_vectors");

        int curr_node = curr_node_ID;
        for (auto &e : args) {
            curr_node_ID = curr_node;
            e.accept(this);
        }
        html << close_node();
    }

    else {
        std::vector<Expr> args = op->vectors;
        for (int i : op->indices) {
            args.emplace_back(i);
        }
        html << open_node(op, "Shuffle");

        int curr_node = curr_node_ID;
        for (auto &e : args) {
            curr_node_ID = curr_node;
            e.accept(this);
        }
        html << close_node();
    }
}
void GetStmtHierarchy::visit(const VectorReduce *op) {
    html << open_node(op, "vector_reduce");

    int curr_node = curr_node_ID;
    ostringstream op_op;
    op_op << op->op;
    html << node_without_children(nullptr, op_op.str());

    curr_node_ID = curr_node;
    op->value.accept(this);

    html << close_node();
}
void GetStmtHierarchy::visit(const Prefetch *op) {
    internal_assert(false) << "\n"
                           << "GetStmtHierarchy: Prefetch is not supported yet. \n\n";
}
void GetStmtHierarchy::visit(const Fork *op) {
    internal_assert(false) << "\n"
                           << "GetStmtHierarchy: Fork is not supported yet. \n\n";
}
void GetStmtHierarchy::visit(const Acquire *op) {
    html << open_node(op, "acquire");

    int curr_node = curr_node_ID;
    op->semaphore.accept(this);

    curr_node_ID = curr_node;
    op->count.accept(this);

    html << close_node();
}
void GetStmtHierarchy::visit(const Atomic *op) {
    if (op->mutex_name.empty()) {
        html << node_without_children(op, "atomic");
    } else {
        html << open_node(op, "atomic");
        html << node_without_children(nullptr, op->mutex_name);
        html << close_node();
    }
}

string GetStmtHierarchy::generate_stmt_hierarchy_js() {
    ostringstream stmt_hierarchy_js;

    stmt_hierarchy_js
        << "\n// stmtHierarchy JS\n"
        << "for (let i = 1; i <= " << stmt_hierarchy_tooltip_count << "; i++) { \n"
        << "    const button = document.getElementById('stmtHierarchyButtonTooltip' + i); \n"
        << "    const tooltip = document.getElementById('stmtHierarchyTooltip' + i); \n"
        << "    button.addEventListener('mouseenter', () => { \n"
        << "        showTooltip(button, tooltip); \n"
        << "    }); \n"
        << "    button.addEventListener('mouseleave', () => { \n"
        << "        hideTooltip(tooltip); \n"
        << "    } \n"
        << "    ); \n"
        << "    tooltip.addEventListener('focus', () => { \n"
        << "        showTooltip(button, tooltip); \n"
        << "    } \n"
        << "    ); \n"
        << "    tooltip.addEventListener('blur', () => { \n"
        << "        hideTooltip(tooltip); \n"
        << "    } \n"
        << "    ); \n"
        << "} \n";

    return stmt_hierarchy_js.str();
}

const char *GetStmtHierarchy::stmt_hierarchy_css =
    R"(
/* StmtHierarchy CSS */
.arrow { border: solid rgb(125,125,125); border-width: 0 2px 2px 0; display:
inline-block; padding: 3px; }
.down { transform: rotate(45deg); -webkit-transform: rotate(45deg); }
.up { transform: rotate(-135deg); -webkit-transform: rotate(-135deg); }
.stmtHierarchyButton {padding: 3px;}
.tf-custom-stmtHierarchy .tf-nc { border-radius: 5px; border: 1px solid; font-size: 12px; border-color: rgb(200, 200, 200);}
.tf-custom-stmtHierarchy .end-node { border-style: dashed; font-size: 12px; }
.tf-custom-stmtHierarchy .tf-nc:before, .tf-custom-stmtHierarchy .tf-nc:after { border-left-width: 1px; border-color: rgb(200, 200, 200);}
.tf-custom-stmtHierarchy li li:before { border-top-width: 1px; border-color: rgb(200, 200, 200);}
.tf-custom-stmtHierarchy { font-size: 12px; }
div.nodeContent { display: flex; }
div.nodeName { padding-left: 5px; }
)";

const char *GetStmtHierarchy::stmt_hierarchy_collapse_expand_JS = R"(
// collapse/expand js (stmt hierarchy)
var nodeExpanded = new Map();
function collapseAllNodes(startNode, endNode) {
    for (let i = startNode; i <= endNode; i++) {
        collapseNodeChildren(i);
        nodeExpanded.set(i, false);
        if (document.getElementById('stmtHierarchyButton' + i) != null) {
            document.getElementById('stmtHierarchyButton' + i).className = 'arrow down';
        }
    }
}
function expandNodesUpToDepth(depth, vizNum) {
    for (let i = 0; i < depth; i++) {
        const depthChildren = document.getElementsByClassName('viz' + vizNum + ' depth' + i);
        for (const child of depthChildren) {
            child.style.display = '';
            if (child.className.includes('start')) {
                continue;
            }
            let parentNodeID = child.className.split()[0];
            parentNodeID = parentNodeID.split('node')[1];
            parentNodeID = parentNodeID.split('child')[0];
            const parentNode = parseInt(parentNodeID);
            nodeExpanded.set(parentNode, true);
            if (document.getElementById('stmtHierarchyButton' + parentNodeID) != null) {
                document.getElementById('stmtHierarchyButton' + parentNodeID).className = 'arrow up';
            }
            const dotdotdot = document.getElementById('node' + parentNodeID + 'dotdotdot');
            if (dotdotdot != null) {
                dotdotdot.remove();
            }
        }
    }
}
function handleClick(nodeNum) {
    if (nodeExpanded.get(nodeNum)) {
        collapseNodeChildren(nodeNum);
        nodeExpanded.set(nodeNum, false);
    } else {
        expandNodeChildren(nodeNum);
        nodeExpanded.set(nodeNum, true);
    }
}
function collapseNodeChildren(nodeNum) {
    const children = document.getElementsByClassName('node' + nodeNum + 'child');
    if (document.getElementById('stmtHierarchyButton' + nodeNum) != null) {
        document.getElementById('stmtHierarchyButton' + nodeNum).className = 'arrow down';
    }
    for (const child of children) {
        child.style.display = 'none';
    }
    const list = document.getElementById('list' + nodeNum);
    const parentNode = document.getElementById('node' + nodeNum);
    if (list != null && parentNode != null) {
        const span = parentNode.children[0];
        list.appendChild(addDotDotDotChild(nodeNum));
    }
}
function expandNodeChildren(nodeNum) {
    const children = document.getElementsByClassName('node' + nodeNum + 'child');
    if (document.getElementById('stmtHierarchyButton' + nodeNum) != null) {
        document.getElementById('stmtHierarchyButton' + nodeNum).className = 'arrow up';
    }
    for (const child of children) {
        child.style.display = '';
    }
     const dotdotdot = document.getElementById('node' + nodeNum + 'dotdotdot');
     if (dotdotdot != null) {
         dotdotdot.remove();
     }
}
function addDotDotDotChild(nodeNum, colorCost) {
    var liDotDotDot = document.createElement('li');
    liDotDotDot.id = 'node' + nodeNum + 'dotdotdot';
    const span =\"<span class='tf-nc end-node'>...</span> \";
    liDotDotDot.innerHTML = span;
    return liDotDotDot;
}
)";

}  // namespace Internal
}  // namespace Halide
