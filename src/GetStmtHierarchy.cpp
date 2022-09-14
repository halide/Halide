#include "GetStmtHierarchy.h"

using namespace std;
using namespace Halide;
using namespace Internal;

StmtHierarchyInfo GetStmtHierarchy::get_hierarchy_html(const Expr &node) {
    reset_variables();

    int startNode = currNodeID;
    html << start_tree();
    node.accept(this);
    html << end_tree();
    int endNode = numNodes;

    StmtHierarchyInfo info;
    info.html = html.str();
    info.viz_num = vizCounter;
    info.start_node = startNode;
    info.end_node = endNode;

    return info;
}
StmtHierarchyInfo GetStmtHierarchy::get_hierarchy_html(const Stmt &node) {
    reset_variables();

    int startNode = currNodeID;
    html << start_tree();
    node.accept(this);
    html << end_tree();
    int endNode = numNodes;

    StmtHierarchyInfo info;
    info.html = html.str();
    info.viz_num = vizCounter;
    info.start_node = startNode;
    info.end_node = endNode;

    return info;
}

StmtHierarchyInfo GetStmtHierarchy::get_else_hierarchy_html() {
    reset_variables();

    int startNode = currNodeID;
    html << start_tree();
    html << node_without_children(nullptr, "else");
    html << end_tree();
    int endNode = numNodes;

    StmtHierarchyInfo info;
    info.html = html.str();
    info.viz_num = vizCounter;
    info.start_node = startNode;
    info.end_node = endNode;

    return info;
}

void GetStmtHierarchy::update_num_nodes() {
    numNodes++;
    currNodeID = numNodes;
}

string GetStmtHierarchy::get_node_class_name() {
    stringstream className;
    if (currNodeID == startNodeID) {
        className << "viz" << vizCounter << " startNode depth" << nodeDepth;
    } else {
        className << "viz" << vizCounter << " node" << currNodeID << "child depth" << nodeDepth;
    }
    return className.str();
}

void GetStmtHierarchy::reset_variables() {
    html.str("");
    numNodes++;
    currNodeID = numNodes;
    startNodeID = -1;
    nodeDepth = 0;
    startNodeID = numNodes;
    vizCounter++;
}

string GetStmtHierarchy::start_tree() const {
    stringstream ss;
    ss << "<div class='treeDiv'>";
    ss << "<div class='tf-tree tf-gap-sm tf-custom-stmtHierarchy'>";
    ss << "<ul>";
    return ss.str();
}
string GetStmtHierarchy::end_tree() const {
    stringstream ss;
    ss << "</ul>";
    ss << "</div>";
    ss << "</div>";
    return ss.str();
}

string GetStmtHierarchy::generate_computation_cost_div(const IRNode *op) {
    stmtHierarchyTooltipCount++;

    stringstream ss;
    string tooltipText = findStmtCost.generate_computation_cost_tooltip(op, false, "");

    // tooltip span
    ss << "<span id='stmtHierarchyTooltip" << stmtHierarchyTooltipCount
       << "' class='tooltip CostTooltip' "
       << "role='stmtHierarchyTooltip" << stmtHierarchyTooltipCount << "'>" << tooltipText
       << "</span>";

    // color div
    int computation_range = findStmtCost.get_computation_color_range(op, false);
    string className = "computation-cost-div CostColor" + std::to_string(computation_range);
    ss << "<div id='stmtHierarchyButtonTooltip" << stmtHierarchyTooltipCount << "' ";
    ss << "aria-describedby='stmtHierarchyTooltip" << stmtHierarchyTooltipCount << "' ";
    ss << "class='" << className << "'>";
    ss << "</div>";

    return ss.str();
}
string GetStmtHierarchy::generate_memory_cost_div(const IRNode *op) {
    stmtHierarchyTooltipCount++;

    stringstream ss;
    string tooltipText = findStmtCost.generate_data_movement_cost_tooltip(op, false, "");

    // tooltip span
    ss << "<span id='stmtHierarchyTooltip" << stmtHierarchyTooltipCount
       << "' class='tooltip CostTooltip' "
       << "role='stmtHierarchyTooltip" << stmtHierarchyTooltipCount << "'>" << tooltipText
       << "</span>";

    // color div
    int data_movement_range = findStmtCost.get_data_movement_color_range(op, false);
    string className = "memory-cost-div CostColor" + std::to_string(data_movement_range);
    ss << "<div id='stmtHierarchyButtonTooltip" << stmtHierarchyTooltipCount << "' "
       << "aria-describedby='stmtHierarchyTooltip" << stmtHierarchyTooltipCount << "' "
       << "class='" << className << "'>"
       << "</div>";

    return ss.str();
}

string GetStmtHierarchy::node_without_children(const IRNode *op, string name) {
    stringstream ss;

    string className = get_node_class_name();
    ss << "<li class='" << className << "'>"
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
    stringstream ss;
    string className = get_node_class_name() + " children-node";

    update_num_nodes();

    ss << "<li class='" << className << "' id='node" << currNodeID << "'>";
    ss << "<span class='tf-nc'>";

    ss << "<div class='nodeContent'>";
    ss << generate_computation_cost_div(op);
    ss << generate_memory_cost_div(op);

    ss << "<div class='nodeName'>" << name
       << "<button class='stmtHierarchyButton info-button' onclick='handleClick(" << currNodeID
       << ")'>"
       << "<i id='stmtHierarchyButton" << currNodeID << "'></i> "
       << "</button>"
       << "</div>"
       << "</div>"
       << "</span>";

    nodeDepth++;
    ss << "<ul id='list" << currNodeID << "'>";

    return ss.str();
}
string GetStmtHierarchy::close_node() {
    nodeDepth--;
    stringstream ss;
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
    stringstream name;
    name << op->type;
    html << open_node(op, name.str());
    op->value.accept(this);
    html << close_node();
}
void GetStmtHierarchy::visit(const Reinterpret *op) {
    stringstream name;
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

    int currNode = currNodeID;
    a.accept(this);

    currNodeID = currNode;
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

    int currNode = currNodeID;
    op->condition.accept(this);

    currNodeID = currNode;
    op->true_value.accept(this);

    currNodeID = currNode;
    op->false_value.accept(this);

    html << close_node();
}
void GetStmtHierarchy::visit(const Load *op) {
    stringstream index;
    index << op->index;
    html << node_without_children(op, op->name + "[" + index.str() + "]");
}
void GetStmtHierarchy::visit(const Ramp *op) {
    html << open_node(op, "Ramp");

    int currNode = currNodeID;
    op->base.accept(this);

    currNodeID = currNode;
    op->stride.accept(this);

    currNodeID = currNode;
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

    int currNode = currNodeID;
    for (auto &arg : op->args) {
        currNodeID = currNode;
        arg.accept(this);
    }

    html << close_node();
}
void GetStmtHierarchy::visit(const Let *op) {
    html << open_node(op, "Let");
    int currNode = currNodeID;

    html << open_node(op->value.get(), "Let");
    html << node_without_children(nullptr, op->name);
    op->value.accept(this);
    html << close_node();

    // "body" node
    currNodeID = currNode;
    html << open_node(op->body.get(), "body");
    op->body.accept(this);
    html << close_node();

    html << close_node();
}
void GetStmtHierarchy::visit(const LetStmt *op) {
    html << open_node(op, "Let");

    int currNode = currNodeID;
    html << node_without_children(nullptr, op->name);

    currNodeID = currNode;
    op->value.accept(this);

    html << close_node();
}
void GetStmtHierarchy::visit(const AssertStmt *op) {
    html << open_node(op, "Assert");

    int currNode = currNodeID;
    op->condition.accept(this);

    currNodeID = currNode;
    op->message.accept(this);
    html << close_node();
}
void GetStmtHierarchy::visit(const ProducerConsumer *op) {
    string nodeName = op->is_producer ? "Produce" : "Consume";
    nodeName += " " + op->name;
    html << node_without_children(op, nodeName);
}
void GetStmtHierarchy::visit(const For *op) {
    html << open_node(op, "For");

    int currNode = currNodeID;
    html << open_node(nullptr, "loop var");
    html << node_without_children(nullptr, op->name);
    html << close_node();

    currNodeID = currNode;
    html << open_node(op->min.get(), "min");
    op->min.accept(this);
    html << close_node();

    currNodeID = currNode;
    html << open_node(op->extent.get(), "extent");
    op->extent.accept(this);
    html << close_node();

    html << close_node();
}
void GetStmtHierarchy::visit(const Store *op) {
    html << open_node(op, "Store");

    stringstream index;
    index << op->index;
    html << node_without_children(op->index.get(), op->name + "[" + index.str() + "]");

    op->value.accept(this);
    html << close_node();
}
void GetStmtHierarchy::visit(const Provide *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: Provide is not supported. Look into it though!!! \n\n";

    html << open_node(op, "Provide");
    int currNode0 = currNodeID;

    html << open_node(op, op->name);
    int currNode1 = currNodeID;
    for (auto &arg : op->args) {
        currNodeID = currNode1;
        arg.accept(this);
    }
    html << close_node();

    for (auto &val : op->values) {
        currNodeID = currNode0;
        val.accept(this);
    }
    html << close_node();
}
void GetStmtHierarchy::visit(const Allocate *op) {
    html << open_node(op, "allocate");

    stringstream index;
    index << op->type;

    for (const auto &extent : op->extents) {
        index << " * ";
        index << extent;
    }

    html << node_without_children(op, op->name + "[" + index.str() + "]");

    stringstream name;
    if (!is_const_one(op->condition)) {
        name << " if " << op->condition;
    }

    if (op->new_expr.defined()) {
        internal_error << "\n"
                       << "GetStmtHierarchy: Allocate " << op->name
                       << " `op->new_expr.defined()` is not supported.\n\n";
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
    internal_error << "\n"
                   << "GetStmtHierarchy: Realize is not supported. Look into it though!!! \n\n";
}
void GetStmtHierarchy::visit(const Block *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: Block is not supported. Look into it though!!! \n\n";
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

        int currNode = currNodeID;
        for (auto &e : op->vectors) {
            currNodeID = currNode;
            e.accept(this);
        }
        html << close_node();
    }

    else if (op->is_interleave()) {
        html << open_node(op, "interleave_vectors");

        int currNode = currNodeID;
        for (auto &e : op->vectors) {
            currNodeID = currNode;
            e.accept(this);
        }
        html << close_node();
    }

    else if (op->is_extract_element()) {
        std::vector<Expr> args = op->vectors;
        args.emplace_back(op->slice_begin());
        html << open_node(op, "extract_element");

        int currNode = currNodeID;
        for (auto &e : args) {
            currNodeID = currNode;
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

        int currNode = currNodeID;
        for (auto &e : args) {
            currNodeID = currNode;
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

        int currNode = currNodeID;
        for (auto &e : args) {
            currNodeID = currNode;
            e.accept(this);
        }
        html << close_node();
    }
}
void GetStmtHierarchy::visit(const VectorReduce *op) {
    html << open_node(op, "vector_reduce");

    int currNode = currNodeID;
    stringstream opOp;
    opOp << op->op;
    html << node_without_children(nullptr, opOp.str());

    currNodeID = currNode;
    op->value.accept(this);

    html << close_node();
}
void GetStmtHierarchy::visit(const Prefetch *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: Prefetch is not supported. Look into it though!!! \n\n";
}
void GetStmtHierarchy::visit(const Fork *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: Fork is not supported. Look into it though!!! \n\n";
}
void GetStmtHierarchy::visit(const Acquire *op) {
    html << open_node(op, "acquire");

    int currNode = currNodeID;
    op->semaphore.accept(this);

    currNodeID = currNode;
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

string GetStmtHierarchy::generate_stmtHierarchy_js() {
    stringstream stmtHierarchyJS;

    stmtHierarchyJS
        << "\n// stmtHierarchy JS\n"
        << "for (let i = 1; i <= " << stmtHierarchyTooltipCount << "; i++) { \n"
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

    return stmtHierarchyJS.str();
}

const string GetStmtHierarchy::stmtHierarchyCSS = "\n \
/* StmtHierarchy CSS */\n \
.arrow { border: solid rgb(125,125,125); border-width: 0 2px 2px 0; display:  \n \
inline-block; padding: 3px; } \n \
.down { transform: rotate(45deg); -webkit-transform: rotate(45deg); }  \n \
.up { transform: rotate(-135deg); -webkit-transform: rotate(-135deg); }  \n \
.stmtHierarchyButton {padding: 3px;} \n \
.tf-custom-stmtHierarchy .tf-nc { border-radius: 5px; border: 1px solid; font-size: 12px; border-color: rgb(200, 200, 200);} \n \
.tf-custom-stmtHierarchy .end-node { border-style: dashed; font-size: 12px; } \n \
.tf-custom-stmtHierarchy .tf-nc:before, .tf-custom-stmtHierarchy .tf-nc:after { border-left-width: 1px; border-color: rgb(200, 200, 200);} \n \
.tf-custom-stmtHierarchy li li:before { border-top-width: 1px; border-color: rgb(200, 200, 200);}\n \
.tf-custom-stmtHierarchy { font-size: 12px; } \n \
div.nodeContent { display: flex; } \n \
div.nodeName { padding-left: 5px; } \n \
";

const string GetStmtHierarchy::stmtHierarchyCollapseExpandJS = "\n \
// collapse/expand js (stmt hierarchy) \n \
var nodeExpanded = new Map(); \n \
var alreadyAddedDotDotDot = []; \n \
function collapseAllNodes(startNode, endNode) { \n \
    for (let i = startNode; i <= endNode; i++) { \n \
        collapseNodeChildren(i); \n \
        nodeExpanded.set(i, false); \n \
        if (document.getElementById('stmtHierarchyButton' + i) != null) { \n \
            document.getElementById('stmtHierarchyButton' + i).className = 'arrow down'; \n \
        } \n \
    } \n \
} \n \
function expandNodesUpToDepth(depth, vizNum) { \n \
    for (let i = 0; i < depth; i++) { \n \
        const depthChildren = document.getElementsByClassName('viz' + vizNum + ' depth' + i); \n \
        for (const child of depthChildren) { \n \
            child.style.display = ''; \n \
            if (child.className.includes('start')) { \n \
                continue; \n \
            } \n \
            let parentNodeID = child.className.split()[0]; \n \
            parentNodeID = parentNodeID.split('node')[1]; \n \
            parentNodeID = parentNodeID.split('child')[0]; \n \
            const parentNode = parseInt(parentNodeID); \n \
            nodeExpanded.set(parentNode, true); \n \
            if (document.getElementById('stmtHierarchyButton' + parentNodeID) != null) { \n \
                document.getElementById('stmtHierarchyButton' + parentNodeID).className = 'arrow up'; \n \
            } \n \
            const dotdotdot = document.getElementById('node' + parentNodeID + 'dotdotdot'); \n \
            if (dotdotdot != null) { \n \
                dotdotdot.remove(); \n \
            } \n \
        } \n \
    } \n \
} \n \
function handleClick(nodeNum) { \n \
    if (nodeExpanded.get(nodeNum)) { \n \
        collapseNodeChildren(nodeNum); \n \
        nodeExpanded.set(nodeNum, false); \n \
    } else { \n \
        expandNodeChildren(nodeNum); \n \
        nodeExpanded.set(nodeNum, true); \n \
    } \n \
} \n \
function collapseNodeChildren(nodeNum) { \n \
    const children = document.getElementsByClassName('node' + nodeNum + 'child'); \n \
    if (document.getElementById('stmtHierarchyButton' + nodeNum) != null) { \n \
        document.getElementById('stmtHierarchyButton' + nodeNum).className = 'arrow down'; \n \
    } \n \
    for (const child of children) { \n \
        child.style.display = 'none'; \n \
    } \n \
    if (alreadyAddedDotDotDot.includes(nodeNum)) { \n \
        return; \n \
    } \n \
    const list = document.getElementById('list' + nodeNum); \n \
    const parentNode = document.getElementById('node' + nodeNum); \n \
    if (list != null && parentNode != null) { \n \
        const span = parentNode.children[0]; \n \
        list.appendChild(addDotDotDotChild(nodeNum)); \n \
        alreadyAddedDotDotDot.push(nodeNum); \n \
    } \n \
} \n \
function expandNodeChildren(nodeNum) { \n \
    const children = document.getElementsByClassName('node' + nodeNum + 'child'); \n \
    if (document.getElementById('stmtHierarchyButton' + nodeNum) != null) { \n \
        document.getElementById('stmtHierarchyButton' + nodeNum).className = 'arrow up'; \n \
    } \n \
    for (const child of children) { \n \
        child.style.display = ''; \n \
    } \n \
     const dotdotdot = document.getElementById('node' + nodeNum + 'dotdotdot'); \n \
     if (dotdotdot != null) { \n \
         dotdotdot.remove(); \n \
     } \n \
} \n \
function addDotDotDotChild(nodeNum, colorCost) { \n \
    var liDotDotDot = document.createElement('li'); \n \
    liDotDotDot.id = 'node' + nodeNum + 'dotdotdot'; \n \
    const span =\"<span class='tf-nc end-node'>...</span> \"; \n \
    liDotDotDot.innerHTML = span; \n \
    return liDotDotDot; \n \
} \n \
";
