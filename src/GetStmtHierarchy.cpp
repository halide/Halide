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

    return html.str();
}

string GetStmtHierarchy::get_hierarchy_html(const Stmt &startNode) {
    start_html();

    depth = 0;
    startCCNodeID = numNodes;
    start_tree();
    startNode.accept(this);
    end_tree();

    end_html();

    return html.str();
}

void GetStmtHierarchy::update_num_nodes() {
    numNodes++;
    currNodeID = numNodes;
}

string GetStmtHierarchy::get_node_class_name() {
    if (currNodeID == startCCNodeID) {
        return "startCCNode depth" + to_string(depth);
    }

    else if (currNodeID == startDMCNodeID) {
        return "startDMCNode depth" + to_string(depth);
    }

    else {
        return "node" + to_string(currNodeID) + "child depth" + to_string(depth);
    }
}

int GetStmtHierarchy::get_color_range(const IRNode *op) const {
    if (op == nullptr) {
        return 0;
    }

    if (colorType == CC_TYPE) {
        return findStmtCost.get_computation_color_range(op);

    } else if (colorType == DMC_TYPE) {
        return findStmtCost.get_data_movement_color_range(op);

    } else {
        internal_error << "\n"
                       << "GetStmtHierarchy::get_color_range: invalid color type"
                       << "\n\n";
        return 0;
    }
}

int GetStmtHierarchy::get_color_range_list(vector<Halide::Expr> exprs) const {
    int maxValue = 0;
    int retValue;

    if (colorType == CC_TYPE) {
        for (const Expr &e : exprs) {
            retValue = findStmtCost.get_computation_color_range(e.get());
            if (retValue > maxValue) {
                maxValue = retValue;
            }
        }
    }

    else if (colorType == DMC_TYPE) {
        for (const Expr &e : exprs) {
            retValue = findStmtCost.get_data_movement_color_range(e.get());
            if (retValue > maxValue) {
                maxValue = retValue;
            }
        }
    }

    else {
        internal_error << "\n"
                       << "GetStmtHierarchy::get_color_range_list: invalid color type"
                       << "\n\n";
    }

    return maxValue;
}

void GetStmtHierarchy::start_html() {
    html.str(string());
    numNodes++;
    currNodeID = numNodes;
    startCCNodeID = -1;
    startDMCNodeID = -1;

    // TODO: add navigation maybe
}
void GetStmtHierarchy::end_html() {
    // html << generate_collapse_expand_js(numNodes); // TODO: add this back in
}

void GetStmtHierarchy::start_tree() {
    html << "<div class='treeDiv'>";
    html << "<div class='tf-tree tf-gap-sm tf-custom-stmtHierarchy' style='font-size: 12px;'>";
    html << "<ul>";
}
void GetStmtHierarchy::end_tree() {
    html << "</ul>";
    html << "</div>";
    html << "</div>";
}

void GetStmtHierarchy::node_without_children(const IRNode *op, string name) {

    colorType = CC_TYPE;
    int colorRange = get_color_range(op);
    colorType = DMC_TYPE;
    int dmcColorRange = get_color_range(op);

    string className = get_node_class_name();
    html << "<li class='" << className << "'>";
    html << "<span class='tf-nc end-node'>";
    // html << name << "</span></li>";
    html << "<div style='display: flex; '>";
    html << "<div class='CostColor" << colorRange << "' style='width: 7px;'></div>";
    html << "<div class='CostColor" << dmcColorRange << "' style='width: 7px;'></div>";
    html << "<div style='padding-left: 5px;'>";
    html << name;
    html << "</div>";
    html << "</div>";
    html << "</span>";
    html << "</li>";
}
void GetStmtHierarchy::open_node(const IRNode *op, string name) {

    colorType = CC_TYPE;
    int colorRange = get_color_range(op);
    colorType = DMC_TYPE;
    int dmcColorRange = get_color_range(op);

    string className = get_node_class_name() + " children-node";

    update_num_nodes();

    /*
        <div style="display: flex; ">
            <div class="CostColor0"
                style="width: 7px;"></div>
            <div class="CostColor18"
                style="width: 7px;"></div>
            <div style="padding-left: 5px;">
                _halide_buffer_get_dimensions
                <button
                    class='stmtHierarchyButton info-button'
                    onclick='handleClick(42)'>
                    <i
                        id='stmtHierarchyButton42'></i>
                </button>
            </div>
        </div>
    */
    html << "<li class='" << className << "' id='node" << currNodeID << "'>";
    html << "<span class='tf-nc'>";

    html << "<div style='display: flex; '>";
    html << "<div class='CostColor" << colorRange << "' style='width: 7px;'></div>";
    html << "<div class='CostColor" << dmcColorRange << "' style='width: 7px;'></div>";
    html << "<div style='padding-left: 5px;'>";
    html << name;
    html << " <button class='stmtHierarchyButton info-button' onclick='handleClick(" << currNodeID
         << ")'>";
    html << " <i id='stmtHierarchyButton" << currNodeID << "'></i> ";
    html << "</button>";
    html << "</div>";
    html << "</div>";

    // html << "<span class='tf-nc CostColor" << colorCost << "'>";
    // html << name;

    // html << " <button class='stmtHierarchyButton info-button' onclick='handleClick(" <<
    // currNodeID
    //      << ")'>";
    // html << " <i id='stmtHierarchyButton" << currNodeID << "'></i> ";
    // html << "</button>";
    html << "</span>";

    depth++;
    html << "<ul id='list" << currNodeID << "'>";
}
void GetStmtHierarchy::close_node() {
    depth--;
    html << "</ul>";
    html << "</li>";
}

void GetStmtHierarchy::visit(const IntImm *op) {
    node_without_children(op, to_string(op->value) /*, get_color_range(op)*/);
}
void GetStmtHierarchy::visit(const UIntImm *op) {
    node_without_children(op, to_string(op->value) /*, get_color_range(op)*/);
}
void GetStmtHierarchy::visit(const FloatImm *op) {
    node_without_children(op, to_string(op->value) /*, get_color_range(op)*/);
}
void GetStmtHierarchy::visit(const StringImm *op) {
    node_without_children(op, op->value /*, get_color_range(op)*/);
}
void GetStmtHierarchy::visit(const Cast *op) {
    stringstream name;
    name << op->type;
    // int computation_range = get_color_range(op);
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
    node_without_children(op, op->name /*, get_color_range(op)*/);
}

void GetStmtHierarchy::visit_binary_op(const IRNode *op, const Expr &a, const Expr &b,
                                       const string &name) {
    open_node(op, name);

    int currNode = currNodeID;
    a.accept(this);

    currNodeID = currNode;
    b.accept(this);

    close_node();
}

void GetStmtHierarchy::visit(const Add *op) {
    // int computation_range = get_color_range(op);
    visit_binary_op(op, op->a, op->b, "+");
}
void GetStmtHierarchy::visit(const Sub *op) {
    // int computation_range = get_color_range(op);
    visit_binary_op(op, op->a, op->b, "-");
}
void GetStmtHierarchy::visit(const Mul *op) {
    // int computation_range = get_color_range(op);
    visit_binary_op(op, op->a, op->b, "*");
}
void GetStmtHierarchy::visit(const Div *op) {
    // int computation_range = get_color_range(op);
    visit_binary_op(op, op->a, op->b, "/");
}
void GetStmtHierarchy::visit(const Mod *op) {
    // int computation_range = get_color_range(op);
    visit_binary_op(op, op->a, op->b, "%");
}
void GetStmtHierarchy::visit(const EQ *op) {
    // int computation_range = get_color_range(op);
    visit_binary_op(op, op->a, op->b, "==");
}
void GetStmtHierarchy::visit(const NE *op) {
    // int computation_range = get_color_range(op);
    visit_binary_op(op, op->a, op->b, "!=");
}
void GetStmtHierarchy::visit(const LT *op) {
    // int computation_range = get_color_range(op);
    visit_binary_op(op, op->a, op->b, "<");
}
void GetStmtHierarchy::visit(const LE *op) {
    // int computation_range = get_color_range(op);
    visit_binary_op(op, op->a, op->b, "<=");
}
void GetStmtHierarchy::visit(const GT *op) {
    // int computation_range = get_color_range(op);
    visit_binary_op(op, op->a, op->b, ">");
}
void GetStmtHierarchy::visit(const GE *op) {
    // int computation_range = get_color_range(op);
    visit_binary_op(op, op->a, op->b, ">=");
}
void GetStmtHierarchy::visit(const And *op) {
    // int computation_range = get_color_range(op);
    visit_binary_op(op, op->a, op->b, "&amp;&amp;");
}
void GetStmtHierarchy::visit(const Or *op) {
    // int computation_range = get_color_range(op);
    visit_binary_op(op, op->a, op->b, "||");
}
void GetStmtHierarchy::visit(const Min *op) {
    // int computation_range = get_color_range(op);
    visit_binary_op(op, op->a, op->b, "min");
}
void GetStmtHierarchy::visit(const Max *op) {
    // int computation_range = get_color_range(op);
    visit_binary_op(op, op->a, op->b, "max");
}

void GetStmtHierarchy::visit(const Not *op) {
    // int computation_range = get_color_range(op);
    // open_node("!", computation_range);
    open_node(op, "!");
    op->a.accept(this);
    close_node();
}
void GetStmtHierarchy::visit(const Select *op) {
    // int computation_range = get_color_range(op);
    // open_node("Select", computation_range);
    open_node(op, "Select");

    int currNode = currNodeID;
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
    // node_without_children(op->name + "[" + index.str() + "]"/*, get_color_range(op)*/));
    node_without_children(op, op->name + "[" + index.str() + "]");
}
void GetStmtHierarchy::visit(const Ramp *op) {
    // int computation_range = get_color_range(op);
    // open_node("Ramp", computation_range);
    open_node(op, "Ramp");

    int currNode = currNodeID;
    op->base.accept(this);

    currNodeID = currNode;
    op->stride.accept(this);

    currNodeID = currNode;
    Expr(op->lanes).accept(this);

    close_node();
}
void GetStmtHierarchy::visit(const Broadcast *op) {
    // int computation_range = get_color_range(op);
    // open_node("x" + to_string(op->lanes), computation_range);
    open_node(op, "x" + to_string(op->lanes));
    op->value.accept(this);
    close_node();
}
void GetStmtHierarchy::visit(const Call *op) {
    // int computation_range = get_color_range(op);
    // open_node(op->name, computation_range);
    open_node(op, op->name);

    int currNode = currNodeID;
    for (auto &arg : op->args) {
        currNodeID = currNode;
        arg.accept(this);
    }

    close_node();
}
void GetStmtHierarchy::visit(const Let *op) {
    // int computation_range = get_color_range(op);
    // open_node("Let", computation_range);
    open_node(op, "Let");
    int currNode = currNodeID;

    // "=" node
    // int value_range = get_color_range(op->value.get());
    // open_node("=", value_range);
    open_node(op->value.get(), "=");
    // node_without_children(op->name, 0);
    node_without_children(nullptr, op->name);
    op->value.accept(this);
    close_node();

    // "body" node
    // int computation_range_body = get_color_range(op->body.get());
    currNodeID = currNode;
    // open_node("body", computation_range_body);
    open_node(op->body.get(), "body");
    op->body.accept(this);
    close_node();

    close_node();
}
void GetStmtHierarchy::visit(const LetStmt *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: LetStmt is not supported.\n\n";

    // int computation_range = get_color_range(op);
    // open_node("=", computation_range);
    open_node(op, "=");

    int currNode = currNodeID;
    // node_without_children(op->name, 0);
    node_without_children(nullptr, op->name);

    currNodeID = currNode;
    op->value.accept(this);

    close_node();
}
void GetStmtHierarchy::visit(const AssertStmt *op) {
    // int computation_range = get_color_range(op);
    // open_node("Assert", computation_range);
    open_node(op, "Assert");
    op->condition.accept(this);
    close_node();
}
void GetStmtHierarchy::visit(const ProducerConsumer *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: ProducerConsumer is not supported.\n\n";
}
void GetStmtHierarchy::visit(const For *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: For is not supported.\n\n";
}
void GetStmtHierarchy::visit(const Store *op) {
    // int computation_range = get_color_range(op);
    // open_node("=", computation_range);
    open_node(op, "=");

    stringstream index;
    index << op->index;
    // node_without_children(op->name + "[" + index.str() + "]", get_color_range(op->index.get()));
    node_without_children(op->index.get(), op->name + "[" + index.str() + "]");

    op->value.accept(this);
    close_node();
}
void GetStmtHierarchy::visit(const Provide *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: Provide is not supported. Look into it though!!! \n\n";

    // int computation_range = get_color_range(op);
    // open_node("=", computation_range);
    open_node(op, "=");
    int currNode0 = currNodeID;

    // open_node(op->name, computation_range);
    open_node(op, op->name);
    int currNode1 = currNodeID;
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
    // int computation_range = get_color_range(op);
    // open_node("allocate", computation_range);
    open_node(op, "allocate");

    stringstream index;
    index << op->type;

    for (const auto &extent : op->extents) {
        index << " * ";
        index << extent;
    }

    // node_without_children(op->name + "[" + index.str() + "]", get_color_range_list(op->extents));
    node_without_children(op, op->name + "[" + index.str() + "]");

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

    // node_without_children(name.str(), computation_range);
    node_without_children(op, name.str());
    close_node();
}
void GetStmtHierarchy::visit(const Free *op) {
    // int computation_range = get_color_range(op);
    // open_node("Free", computation_range);
    open_node(op, "Free");
    // node_without_children(op->name, get_color_range(op));
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
    // int computation_range = get_color_range(op);
    // open_node("IfThenElse", computation_range);
    open_node(op, "IfThenElse");

    op->condition.accept(this);
    if (op->else_case.defined()) {
        op->else_case.accept(this);
    }

    close_node();
}
void GetStmtHierarchy::visit(const Evaluate *op) {
    op->value.accept(this);
}
void GetStmtHierarchy::visit(const Shuffle *op) {
    // int computation_range = get_color_range(op);
    if (op->is_concat()) {
        // open_node("concat_vectors", computation_range);
        open_node(op, "concat_vectors");

        int currNode = currNodeID;
        for (auto &e : op->vectors) {
            currNodeID = currNode;
            e.accept(this);
        }
        close_node();
    }

    else if (op->is_interleave()) {
        // open_node("interleave_vectors", computation_range);
        open_node(op, "interleave_vectors");

        int currNode = currNodeID;
        for (auto &e : op->vectors) {
            currNodeID = currNode;
            e.accept(this);
        }
        close_node();
    }

    else if (op->is_extract_element()) {
        std::vector<Expr> args = op->vectors;
        args.emplace_back(op->slice_begin());
        // open_node("extract_element", computation_range);
        open_node(op, "extract_element");

        int currNode = currNodeID;
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
        // open_node("slice_vectors", computation_range);
        open_node(op, "slice_vectors");

        int currNode = currNodeID;
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
        // open_node("Shuffle", computation_range);
        open_node(op, "Shuffle");

        int currNode = currNodeID;
        for (auto &e : args) {
            currNodeID = currNode;
            e.accept(this);
        }
        close_node();
    }
}
void GetStmtHierarchy::visit(const VectorReduce *op) {
    internal_error
        << "\n"
        << "GetStmtHierarchy: VectorReduce is not supported. Look into it though!!! \n\n";

    // int computation_range = get_color_range(op);
    // open_node("vector_reduce", computation_range);
    open_node(op, "vector_reduce");

    int currNode = currNodeID;
    // op->op.accept(this);
    // TODO: how to deal with op->op?

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
    // int computation_range = get_color_range(op);
    // open_node("acquire", computation_range);
    open_node(op, "acquire");

    int currNode = currNodeID;
    op->semaphore.accept(this);

    currNodeID = currNode;
    op->count.accept(this);

    close_node();
}
void GetStmtHierarchy::visit(const Atomic *op) {
    if (op->mutex_name.empty()) {
        // node_without_children("atomic", get_color_range(op));
        node_without_children(op, "atomic");
    } else {
        // int computation_range = get_color_range(op);
        // open_node("atomic", computation_range);
        // node_without_children(op->mutex_name, 0);
        open_node(op, "atomic");
        node_without_children(nullptr, op->mutex_name);
        close_node();
    }
}

string GetStmtHierarchy::generate_collapse_expand_js() {

    stringstream js;

    js << "// collapse/expand js (stmt hierarchy)\n";
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
    js << "        var computationNumber = span.className.split('CostComputation')[1];\n";
    js << "        if (computationNumber == null) {\n";
    js << "            computationNumber = 0;\n";
    js << "        } else {\n";
    js << "            computationNumber = parseInt(computationNumber);\n";
    js << "        }\n";
    js << "        list.appendChild(addDotDotDotChild(nodeNum, computationNumber));\n";
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
    js << "    const span =\"<span class='tf-nc end-node CostComputationBorder\" + "
          "colorCost +  \"'>...</span> \";\n";
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
.tf-custom-stmtHierarchy .tf-nc { border-radius: 5px; border: 1px solid; font-size: 12px;} \n \
.tf-custom-stmtHierarchy .end-node { border-style: dashed; font-size: 12px; } \n \
.tf-custom-stmtHierarchy .tf-nc:before, .tf-custom-stmtHierarchy .tf-nc:after { border-left-width: 1px; } \n \
.tf-custom-stmtHierarchy li li:before { border-top-width: 1px; }\n \
.tf-custom-stmtHierarchy .CostComputationBorder19 { border-color: rgb(130,31,27);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder18 { border-color: rgb(145,33,30);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder17 { border-color: rgb(160,33,32);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder16 { border-color: rgb(176,34,34);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder15 { border-color: rgb(185,47,32);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder14 { border-color: rgb(193,59,30);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder13 { border-color: rgb(202,71,27);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder12 { border-color: rgb(210,82,22);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder11 { border-color: rgb(218,93,16);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder10 { border-color: rgb(226,104,6);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder9 { border-color: rgb(229,118,9);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder8 { border-color: rgb(230,132,15);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder7 { border-color: rgb(231,146,20);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder6 { border-color: rgb(232,159,25);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder5 { border-color: rgb(233,172,30);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder4 { border-color: rgb(233,185,35);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder3 { border-color: rgb(233,198,40);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder2 { border-color: rgb(232,211,45);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder1 { border-color: rgb(231,223,50);} \n \
.tf-custom-stmtHierarchy .CostComputationBorder0 { border-color: rgb(236,233,89);}  \n \
";
