#include "GetStmtHierarchy.h"

using namespace std;
using namespace Halide;
using namespace Internal;

string GetStmtHierarchy::get_hierarchy_html(const Expr &startNode) {
    start_html();

    depth = 0;
    startCCNodeID = numNodes;
    start_tree();
    startNode.accept(this);
    end_tree();

    end_html();

    return html.c_str();
}
string GetStmtHierarchy::get_hierarchy_html(const Stmt &startNode) {
    start_html();

    depth = 0;
    startCCNodeID = numNodes;
    start_tree();
    startNode.accept(this);
    end_tree();

    end_html();

    return html.c_str();
}

string GetStmtHierarchy::get_else_hierarchy_html() {
    start_html();

    depth = 0;
    startCCNodeID = numNodes;
    start_tree();
    node_without_children(nullptr, "else");
    end_tree();

    end_html();

    return html.c_str();
}

void GetStmtHierarchy::update_num_nodes() {
    numNodes++;
    currNodeID = numNodes;
}

string GetStmtHierarchy::get_node_class_name() {
    if (currNodeID == startCCNodeID) {
        return "startCCNode depth" + std::to_string(depth);
    }

    else if (currNodeID == startDMCNodeID) {
        return "startDMCNode depth" + std::to_string(depth);
    }

    else {
        return "node" + std::to_string(currNodeID) + "child depth" + std::to_string(depth);
    }
}

void GetStmtHierarchy::start_html() {
    html = "";
    numNodes++;
    currNodeID = numNodes;
    startCCNodeID = -1;
    startDMCNodeID = -1;
}
void GetStmtHierarchy::end_html() {
    // empty for now - have it empty so that it matches start_html()
    // TODO: remove this function (change start_html() name)
}
void GetStmtHierarchy::start_tree() {
    html += "<div class='treeDiv'>";
    html += "<div class='tf-tree tf-gap-sm tf-custom-stmtHierarchy'>";
    html += "<ul>";
}
void GetStmtHierarchy::end_tree() {
    html += "</ul>";
    html += "</div>";
    html += "</div>";
}
void GetStmtHierarchy::generate_computation_cost_div(const IRNode *op) {
    stmtHierarchyTooltipCount++;

    string tooltipText = findStmtCost.generate_computation_cost_tooltip(op, false, "");

    // tooltip span
    html += "<span id='stmtHierarchyTooltip" + std::to_string(stmtHierarchyTooltipCount) +
            "' class='tooltip CostTooltip' ";
    html += "role='stmtHierarchyTooltip" + std::to_string(stmtHierarchyTooltipCount) + "'>";
    html += tooltipText;
    html += "</span>";

    // color div
    int computation_range = findStmtCost.get_computation_color_range(op, false);
    string className = "computation-cost-div CostColor" + to_string(computation_range);
    html +=
        "<div id='stmtHierarchyButtonTooltip" + std::to_string(stmtHierarchyTooltipCount) + "' ";
    html +=
        "aria-describedby='stmtHierarchyTooltip" + std::to_string(stmtHierarchyTooltipCount) + "' ";
    html += "class='" + className + "'>";
    html += "</div>";
}
void GetStmtHierarchy::generate_memory_cost_div(const IRNode *op) {
    stmtHierarchyTooltipCount++;

    string tooltipText = findStmtCost.generate_data_movement_cost_tooltip(op, false, "");

    // tooltip span
    html += "<span id='stmtHierarchyTooltip" + std::to_string(stmtHierarchyTooltipCount) +
            "' class='tooltip CostTooltip' ";
    html += "role='stmtHierarchyTooltip" + std::to_string(stmtHierarchyTooltipCount) + "'>";
    html += tooltipText;
    html += "</span>";

    // color div
    int data_movement_range = findStmtCost.get_data_movement_color_range(op, false);
    string className = "memory-cost-div CostColor" + to_string(data_movement_range);
    html +=
        "<div id='stmtHierarchyButtonTooltip" + std::to_string(stmtHierarchyTooltipCount) + "' ";
    html +=
        "aria-describedby='stmtHierarchyTooltip" + std::to_string(stmtHierarchyTooltipCount) + "' ";
    html += "class='" + className + "'>";
    html += "</div>";
}

void GetStmtHierarchy::node_without_children(const IRNode *op, string name) {

    string className = get_node_class_name();
    html += "<li class='" + className + "'>";
    html += "<span class='tf-nc end-node'>";
    html += "<div class='nodeContent'>";
    generate_computation_cost_div(op);
    generate_memory_cost_div(op);
    html += "<div class='nodeName'>";
    html += name;
    html += "</div>";
    html += "</div>";
    html += "</span>";
    html += "</li>";
}
void GetStmtHierarchy::open_node(const IRNode *op, string name) {

    string className = get_node_class_name() + " children-node";

    update_num_nodes();

    html += "<li class='" + className + "' id='node" + std::to_string(currNodeID) + "'>";
    html += "<span class='tf-nc'>";

    html += "<div class='nodeContent'>";
    generate_computation_cost_div(op);
    generate_memory_cost_div(op);

    html += "<div class='nodeName'>";
    html += name;
    html += " <button class='stmtHierarchyButton info-button' onclick='handleClick(" +
            std::to_string(currNodeID) + ")'>";
    html += " <i id='stmtHierarchyButton" + std::to_string(currNodeID) + "'></i> ";
    html += "</button>";
    html += "</div>";
    html += "</div>";

    html += "</span>";

    depth++;
    html += "<ul id='list" + std::to_string(currNodeID) + "'>";
}
void GetStmtHierarchy::close_node() {
    depth--;
    html += "</ul>";
    html += "</li>";
}

void GetStmtHierarchy::visit(const IntImm *op) {
    node_without_children(op, to_string(op->value));
}
void GetStmtHierarchy::visit(const UIntImm *op) {
    node_without_children(op, to_string(op->value));
}
void GetStmtHierarchy::visit(const FloatImm *op) {
    node_without_children(op, to_string(op->value));
}
void GetStmtHierarchy::visit(const StringImm *op) {
    node_without_children(op, op->value);
}
void GetStmtHierarchy::visit(const Cast *op) {
    stringstream name;
    name << op->type;
    open_node(op, name.str());
    op->value.accept(this);
    close_node();
}
void GetStmtHierarchy::visit(const Reinterpret *op) {
    stringstream name;
    name << "reinterpret ";
    name << op->type;
    open_node(op, name.str());
    op->value.accept(this);
    close_node();
}
void GetStmtHierarchy::visit(const Variable *op) {
    node_without_children(op, op->name);
}

void GetStmtHierarchy::visit_binary_op(const IRNode *op, const Expr &a, const Expr &b,
                                       const string &name) {
    open_node(op, name);

    uint32_t currNode = currNodeID;
    a.accept(this);

    currNodeID = currNode;
    b.accept(this);

    close_node();
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
    open_node(op, "!");
    op->a.accept(this);
    close_node();
}
void GetStmtHierarchy::visit(const Select *op) {
    open_node(op, "Select");

    uint32_t currNode = currNodeID;
    op->condition.accept(this);

    currNodeID = currNode;
    op->true_value.accept(this);

    currNodeID = currNode;
    op->false_value.accept(this);

    close_node();
}
void GetStmtHierarchy::visit(const Load *op) {
    stringstream index;
    index << op->index;
    node_without_children(op, op->name + "[" + index.str() + "]");
}
void GetStmtHierarchy::visit(const Ramp *op) {
    open_node(op, "Ramp");

    uint32_t currNode = currNodeID;
    op->base.accept(this);

    currNodeID = currNode;
    op->stride.accept(this);

    currNodeID = currNode;
    Expr(op->lanes).accept(this);

    close_node();
}
void GetStmtHierarchy::visit(const Broadcast *op) {
    open_node(op, "x" + to_string(op->lanes));
    op->value.accept(this);
    close_node();
}
void GetStmtHierarchy::visit(const Call *op) {
    open_node(op, op->name);

    uint32_t currNode = currNodeID;
    for (auto &arg : op->args) {
        currNodeID = currNode;
        arg.accept(this);
    }

    close_node();
}
void GetStmtHierarchy::visit(const Let *op) {
    open_node(op, "Let");
    uint32_t currNode = currNodeID;

    open_node(op->value.get(), "Let");
    node_without_children(nullptr, op->name);
    op->value.accept(this);
    close_node();

    // "body" node
    currNodeID = currNode;
    open_node(op->body.get(), "body");
    op->body.accept(this);
    close_node();

    close_node();
}
void GetStmtHierarchy::visit(const LetStmt *op) {
    open_node(op, "Let");

    uint32_t currNode = currNodeID;
    node_without_children(nullptr, op->name);

    currNodeID = currNode;
    op->value.accept(this);

    close_node();
}
void GetStmtHierarchy::visit(const AssertStmt *op) {
    open_node(op, "Assert");

    uint32_t currNode = currNodeID;
    op->condition.accept(this);

    currNodeID = currNode;
    op->message.accept(this);
    close_node();
}
void GetStmtHierarchy::visit(const ProducerConsumer *op) {
    string nodeName = op->is_producer ? "Produce" : "Consume";
    nodeName += " " + op->name;
    node_without_children(op, nodeName);
}
void GetStmtHierarchy::visit(const For *op) {
    open_node(op, "For");

    uint32_t currNode = currNodeID;
    open_node(nullptr, "loop var");
    node_without_children(nullptr, op->name);
    close_node();

    currNodeID = currNode;
    open_node(op->min.get(), "min");
    op->min.accept(this);
    close_node();

    currNodeID = currNode;
    open_node(op->extent.get(), "extent");
    op->extent.accept(this);
    close_node();

    close_node();
}
void GetStmtHierarchy::visit(const Store *op) {
    open_node(op, "Store");

    stringstream index;
    index << op->index;
    node_without_children(op->index.get(), op->name + "[" + index.str() + "]");

    op->value.accept(this);
    close_node();
}
void GetStmtHierarchy::visit(const Provide *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: Provide is not supported. Look into it though!!! \n\n";

    open_node(op, "Provide");
    uint32_t currNode0 = currNodeID;

    open_node(op, op->name);
    uint32_t currNode1 = currNodeID;
    for (auto &arg : op->args) {
        currNodeID = currNode1;
        arg.accept(this);
    }
    close_node();

    for (auto &val : op->values) {
        currNodeID = currNode0;
        val.accept(this);
    }
    close_node();
}
void GetStmtHierarchy::visit(const Allocate *op) {
    open_node(op, "allocate");

    stringstream index;
    index << op->type;

    for (const auto &extent : op->extents) {
        index << " * ";
        index << extent;
    }

    node_without_children(op, op->name + "[" + index.str() + "]");

    string name = "";
    if (!is_const_one(op->condition)) {
        stringstream cond;
        cond << op->condition;
        name += " if " + cond.str();
    }

    if (op->new_expr.defined()) {
        internal_error << "\n"
                       << "GetStmtHierarchy: Allocate " << op->name
                       << " `op->new_expr.defined()` is not supported.\n\n";
    }
    if (!op->free_function.empty()) {
        name += "custom_delete {" + op->free_function + "}";
    }

    if (name != "") node_without_children(op, name);
    close_node();
}
void GetStmtHierarchy::visit(const Free *op) {
    open_node(op, "Free");
    node_without_children(op, op->name);
    close_node();
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
    open_node(op, "If");

    open_node(op->condition.get(), "condition");
    op->condition.accept(this);
    close_node();

    // don't visualize else case because that will be visualized later as another IfThenElse block

    close_node();
}
void GetStmtHierarchy::visit(const Evaluate *op) {
    op->value.accept(this);
}
void GetStmtHierarchy::visit(const Shuffle *op) {
    if (op->is_concat()) {
        open_node(op, "concat_vectors");

        uint32_t currNode = currNodeID;
        for (auto &e : op->vectors) {
            currNodeID = currNode;
            e.accept(this);
        }
        close_node();
    }

    else if (op->is_interleave()) {
        open_node(op, "interleave_vectors");

        uint32_t currNode = currNodeID;
        for (auto &e : op->vectors) {
            currNodeID = currNode;
            e.accept(this);
        }
        close_node();
    }

    else if (op->is_extract_element()) {
        std::vector<Expr> args = op->vectors;
        args.emplace_back(op->slice_begin());
        open_node(op, "extract_element");

        uint32_t currNode = currNodeID;
        for (auto &e : args) {
            currNodeID = currNode;
            e.accept(this);
        }
        close_node();
    }

    else if (op->is_slice()) {
        std::vector<Expr> args = op->vectors;
        args.emplace_back(op->slice_begin());
        args.emplace_back(op->slice_stride());
        args.emplace_back(static_cast<int>(op->indices.size()));
        open_node(op, "slice_vectors");

        uint32_t currNode = currNodeID;
        for (auto &e : args) {
            currNodeID = currNode;
            e.accept(this);
        }
        close_node();
    }

    else {
        std::vector<Expr> args = op->vectors;
        for (int i : op->indices) {
            args.emplace_back(i);
        }
        open_node(op, "Shuffle");

        uint32_t currNode = currNodeID;
        for (auto &e : args) {
            currNodeID = currNode;
            e.accept(this);
        }
        close_node();
    }
}
void GetStmtHierarchy::visit(const VectorReduce *op) {
    open_node(op, "vector_reduce");

    uint32_t currNode = currNodeID;
    stringstream opOp;
    opOp << op->op;
    node_without_children(nullptr, opOp.str());

    currNodeID = currNode;
    op->value.accept(this);

    close_node();
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
    open_node(op, "acquire");

    uint32_t currNode = currNodeID;
    op->semaphore.accept(this);

    currNodeID = currNode;
    op->count.accept(this);

    close_node();
}
void GetStmtHierarchy::visit(const Atomic *op) {
    if (op->mutex_name.empty()) {
        node_without_children(op, "atomic");
    } else {
        open_node(op, "atomic");
        node_without_children(nullptr, op->mutex_name);
        close_node();
    }
}

string GetStmtHierarchy::generate_stmtHierarchy_js() {
    string stmtHierarchyJS;

    stmtHierarchyJS += "\n// stmtHierarchy JS\n";
    stmtHierarchyJS +=
        "for (let i = 1; i <= " + std::to_string(stmtHierarchyTooltipCount) + "; i++) { \n";
    stmtHierarchyJS +=
        "    const button = document.querySelector('#stmtHierarchyButtonTooltip' + i); \n";
    stmtHierarchyJS +=
        "    const tooltip = document.querySelector('#stmtHierarchyTooltip' + i); \n";
    stmtHierarchyJS += "    button.addEventListener('mouseenter', () => { \n";
    stmtHierarchyJS += "        showTooltip(button, tooltip); \n";
    stmtHierarchyJS += "    }); \n";
    stmtHierarchyJS += "    button.addEventListener('mouseleave', () => { \n";
    stmtHierarchyJS += "        hideTooltip(tooltip); \n";
    stmtHierarchyJS += "    } \n";
    stmtHierarchyJS += "    ); \n";
    stmtHierarchyJS += "    tooltip.addEventListener('focus', () => { \n";
    stmtHierarchyJS += "        showTooltip(button, tooltip); \n";
    stmtHierarchyJS += "    } \n";
    stmtHierarchyJS += "    ); \n";
    stmtHierarchyJS += "    tooltip.addEventListener('blur', () => { \n";
    stmtHierarchyJS += "        hideTooltip(tooltip); \n";
    stmtHierarchyJS += "    } \n";
    stmtHierarchyJS += "    ); \n";
    stmtHierarchyJS += "} \n";

    return stmtHierarchyJS;
}

string GetStmtHierarchy::generate_collapse_expand_js() {

    stringstream js;

    js << "\n// collapse/expand js (stmt hierarchy)\n";
    js << "var nodeExpanded = new Map();\n";
    js << "function collapseAllNodes(numNodes) {\n";
    js << "    for (let i = 0; i < numNodes; i++) {\n";
    js << "        collapseNodeChildren(i);\n";
    js << "        nodeExpanded.set(i, false);\n";
    js << "        if (document.getElementById('stmtHierarchyButton' + i) != null) {\n";
    js << "            document.getElementById('stmtHierarchyButton' + i).className = 'arrow "
          "down';\n";
    js << "        }\n";
    js << "    }\n";
    js << "}\n";
    js << "function expandNodesUpToDepth(depth) {\n";
    js << "    for (let i = 0; i < depth; i++) {\n";
    js << "        const depthChildren = document.getElementsByClassName('depth' + i);\n";
    js << "        for (const child of depthChildren) {\n";
    js << "            child.style.display = '';\n";
    js << "            if (child.className.includes('start')) {\n";
    js << "                continue;\n";
    js << "            }\n";
    js << "            let parentNodeID = child.className.split("
          ")[0];\n";
    js << "            parentNodeID = parentNodeID.split('node')[1];\n";
    js << "            parentNodeID = parentNodeID.split('child')[0];\n";
    js << "            const parentNode = parseInt(parentNodeID);\n";
    js << "            nodeExpanded.set(parentNode, true);\n";
    js << "            if (document.getElementById('stmtHierarchyButton' + parentNodeID) != null) "
          "{\n";
    js << "                document.getElementById('stmtHierarchyButton' + parentNodeID).className "
          "= "
          "'arrow up';\n";
    js << "            }\n";
    js << "            const dotdotdot = document.getElementById('node' + parentNodeID + "
          "'dotdotdot');\n";
    js << "            if (dotdotdot != null) {\n";
    js << "                dotdotdot.remove();\n";
    js << "            }\n";
    js << "        }\n";
    js << "    }\n";
    js << "}\n";
    js << "function handleClick(nodeNum) {\n";
    js << "    if (nodeExpanded.get(nodeNum)) {\n";
    js << "        collapseNodeChildren(nodeNum);\n";
    js << "        nodeExpanded.set(nodeNum, false);\n";
    js << "    } else {\n";
    js << "        expandNodeChildren(nodeNum);\n";
    js << "        nodeExpanded.set(nodeNum, true);\n";
    js << "    }\n";
    js << "}\n";
    js << "function collapseNodeChildren(nodeNum) {\n";
    js << "    const children = document.getElementsByClassName('node' + nodeNum + "
          "'child');\n";
    js << "    if (document.getElementById('stmtHierarchyButton' + nodeNum) != null) {\n";
    js << "        document.getElementById('stmtHierarchyButton' + nodeNum).className = 'arrow "
          "down';\n";
    js << "    }\n";
    js << "    for (const child of children) {\n";
    js << "        child.style.display = 'none';\n";
    js << "    }\n";
    js << "    const list = document.getElementById('list' + nodeNum);\n";
    js << "    const parentNode = document.getElementById('node' + nodeNum);\n";
    js << "    if (list != null && parentNode != null) {\n";
    js << "        const span = parentNode.children[0];\n";
    js << "        list.appendChild(addDotDotDotChild(nodeNum));\n";
    js << "    }\n";
    js << "}\n";
    js << "function expandNodeChildren(nodeNum) {\n";
    js << "    const children = document.getElementsByClassName('node' + nodeNum + "
          "'child');\n";
    js << "    if (document.getElementById('stmtHierarchyButton' + nodeNum) != null) {\n";
    js << "        document.getElementById('stmtHierarchyButton' + nodeNum).className = 'arrow "
          "up';\n";
    js << "    }\n";
    js << "    for (const child of children) {\n";
    js << "        child.style.display = '';\n";
    js << "    }\n";
    js << "     const dotdotdot = document.getElementById('node' + nodeNum + 'dotdotdot');\n";
    js << "     if (dotdotdot != null) {\n";
    js << "         dotdotdot.remove();\n";
    js << "     }\n";
    js << "}\n";
    js << "function addDotDotDotChild(nodeNum, colorCost) {\n";
    js << "    var liDotDotDot = document.createElement('li');\n";
    js << "    liDotDotDot.id = 'node' + nodeNum + 'dotdotdot';\n";
    js << "    const span =\"<span class='tf-nc end-node'>...</span> \";\n";
    js << "    liDotDotDot.innerHTML = span;\n";
    js << "    return liDotDotDot;\n";
    js << "}\n";
    js << "collapseAllNodes(" << numNodes << ");  \n";
    js << "expandNodesUpToDepth(4);\n";

    return js.str();
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
