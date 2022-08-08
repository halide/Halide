#include "GetStmtHierarchy.h"

using namespace std;
using namespace Halide;
using namespace Internal;

string GetStmtHierarchy::get_hierarchy_html(const Expr &startNode) {
    start_html();

    depth = 0;
    colorType = CC_TYPE;
    startCCNodeID = numNodes;
    start_tree();
    mutate(startNode);
    end_tree();

    depth = 0;
    colorType = DMC_TYPE;
    startDMCNodeID = numNodes;
    start_tree();
    mutate(startNode);
    end_tree();

    end_html();

    return html.str();
}

string GetStmtHierarchy::get_hierarchy_html(const Stmt &startNode) {
    start_html();

    depth = 0;
    colorType = CC_TYPE;
    startCCNodeID = numNodes;
    start_tree();
    mutate(startNode);
    end_tree();

    depth = 0;
    update_num_nodes();
    startDMCNodeID = numNodes;
    colorType = DMC_TYPE;
    start_tree();
    mutate(startNode);
    end_tree();

    end_html();

    return html.str();
}

void GetStmtHierarchy::set_stmt_cost(const Module &m) {
    findStmtCost.generate_costs(m);
}
void GetStmtHierarchy::set_stmt_cost(const Stmt &s) {
    findStmtCost.generate_costs(s);
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
        cout << "it's null! confused why i did this lol" << endl;
        return 0;
    }

    if (colorType == CC_TYPE) {
        return findStmtCost.get_computation_range(op);

    } else if (colorType == DMC_TYPE) {
        return findStmtCost.get_data_movement_range(op);

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
            retValue = findStmtCost.get_computation_range(e.get());
            if (retValue > maxValue) {
                maxValue = retValue;
            }
        }
    }

    else if (colorType == DMC_TYPE) {
        for (const Expr &e : exprs) {
            retValue = findStmtCost.get_data_movement_range(e.get());
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
    currNodeID = 0;
    numNodes = 0;
    startCCNodeID = -1;
    startDMCNodeID = -1;

    html << "<html>";
    html << "<head>";
    html << "<link rel=\\'stylesheet\\' "
            "href=\\'https://unpkg.com/treeflex/dist/css/treeflex.css\\'>";
    html << "</head>";
    html << "<style>";
    html << ".tf-custom .tf-nc { border-radius: 5px; border: 1px solid; }";
    html << ".tf-custom .end-node { border-style: dashed; } ";
    html << ".tf-custom .tf-nc:before, .tf-custom .tf-nc:after { border-left-width: 1px; } ";
    html << ".tf-custom li li:before { border-top-width: 1px; }";

    html << ".CostComputation19 { background-color: rgb(130,31,27);}";
    html << ".CostComputation18 { background-color: rgb(145,33,30);}";
    html << ".CostComputation17 { background-color: rgb(160,33,32);}";
    html << ".CostComputation16 { background-color: rgb(176,34,34);}";
    html << ".CostComputation15 { background-color: rgb(185,47,32);}";
    html << ".CostComputation14 { background-color: rgb(193,59,30);}";
    html << ".CostComputation13 { background-color: rgb(202,71,27);}";
    html << ".CostComputation12 { background-color: rgb(210,82,22);}";
    html << ".CostComputation11 { background-color: rgb(218,93,16);}";
    html << ".CostComputation10 { background-color: rgb(226,104,6);}";
    html << ".CostComputation9 { background-color: rgb(229,118,9);}";
    html << ".CostComputation8 { background-color: rgb(230,132,15);}";
    html << ".CostComputation7 { background-color: rgb(231,146,20);}";
    html << ".CostComputation6 { background-color: rgb(232,159,25);}";
    html << ".CostComputation5 { background-color: rgb(233,172,30);}";
    html << ".CostComputation4 { background-color: rgb(233,185,35);}";
    html << ".CostComputation3 { background-color: rgb(233,198,40);}";
    html << ".CostComputation2 { background-color: rgb(232,211,45);}";
    html << ".CostComputation1 { background-color: rgb(231,223,50);}";
    html << ".CostComputation0 { background-color: rgb(236,233,89);}";

    html << ".tf-custom .CostComputationBorder19 { border-color: rgb(130,31,27);}";
    html << ".tf-custom .CostComputationBorder18 { border-color: rgb(145,33,30);}";
    html << ".tf-custom .CostComputationBorder17 { border-color: rgb(160,33,32);}";
    html << ".tf-custom .CostComputationBorder16 { border-color: rgb(176,34,34);}";
    html << ".tf-custom .CostComputationBorder15 { border-color: rgb(185,47,32);}";
    html << ".tf-custom .CostComputationBorder14 { border-color: rgb(193,59,30);}";
    html << ".tf-custom .CostComputationBorder13 { border-color: rgb(202,71,27);}";
    html << ".tf-custom .CostComputationBorder12 { border-color: rgb(210,82,22);}";
    html << ".tf-custom .CostComputationBorder11 { border-color: rgb(218,93,16);}";
    html << ".tf-custom .CostComputationBorder10 { border-color: rgb(226,104,6);}";
    html << ".tf-custom .CostComputationBorder9 { border-color: rgb(229,118,9);}";
    html << ".tf-custom .CostComputationBorder8 { border-color: rgb(230,132,15);}";
    html << ".tf-custom .CostComputationBorder7 { border-color: rgb(231,146,20);}";
    html << ".tf-custom .CostComputationBorder6 { border-color: rgb(232,159,25);}";
    html << ".tf-custom .CostComputationBorder5 { border-color: rgb(233,172,30);}";
    html << ".tf-custom .CostComputationBorder4 { border-color: rgb(233,185,35);}";
    html << ".tf-custom .CostComputationBorder3 { border-color: rgb(233,198,40);}";
    html << ".tf-custom .CostComputationBorder2 { border-color: rgb(232,211,45);}";
    html << ".tf-custom .CostComputationBorder1 { border-color: rgb(231,223,50);}";
    html << ".tf-custom .CostComputationBorder0 { border-color: rgb(236,233,89);} ";

    html << ".arrow { border: solid rgb(125,125,125); border-width: 0 2px 2px 0; display: ";
    html << "inline-block; padding: 3px; }";
    html << ".down { transform: rotate(45deg); -webkit-transform: rotate(45deg); } ";
    html << ".up { transform: rotate(-135deg); -webkit-transform: rotate(-135deg); } ";
    html << ".button {padding: 3px;}";

    html << "body { font-family: Consolas, \\'Liberation Mono\\', Menlo, Courier, monospace;}";
    html << "</style>";
    html << "<body>";
}
void GetStmtHierarchy::end_html() {
    html << "</body></html>";
    html << "<script>";
    html << generate_collapse_expand_js(numNodes);
    html << "</script>";
}

void GetStmtHierarchy::start_tree() {
    html << "<div class=\\'tf-tree tf-gap-sm tf-custom\\' style=\\'font-size: 15px;\\'>";
    html << "<ul>";
}
void GetStmtHierarchy::end_tree() {
    html << "</ul>";
    html << "</div>";
}

void GetStmtHierarchy::node_without_children(string name, int colorCost) {
    string className = get_node_class_name();
    html << "<li class=\\'" << className << "\\'>";
    html << "<span class=\\'tf-nc end-node CostComputationBorder" << colorCost << "\\'>";
    html << name << "</span></li>";
}
void GetStmtHierarchy::open_node(string name, int colorCost) {
    string className = get_node_class_name() + " children-node";

    update_num_nodes();

    html << "<li class=\\'" << className << "\\' id=\\'node" << currNodeID << "\\'>";
    html << "<span class=\\'tf-nc CostComputation" << colorCost << "\\'>";
    html << name;

    html << " <button class=\\'button\\' onclick=\\'handleClick(" << currNodeID << ")\\'>";
    html << " <i id=\\'button" << currNodeID << "\\'></i> ";
    html << "</button>";
    html << "</span>";

    depth++;
    html << "<ul id=\\'list" << currNodeID << "\\'>";
}
void GetStmtHierarchy::close_node() {
    depth--;
    html << "</ul>";
    html << "</li>";
}

Expr GetStmtHierarchy::visit(const IntImm *op) {
    node_without_children(to_string(op->value), get_color_range(op));
    return op;
}
Expr GetStmtHierarchy::visit(const UIntImm *op) {
    node_without_children(to_string(op->value), get_color_range(op));
    return op;
}
Expr GetStmtHierarchy::visit(const FloatImm *op) {
    node_without_children(to_string(op->value), get_color_range(op));
    return op;
}
Expr GetStmtHierarchy::visit(const StringImm *op) {
    node_without_children(op->value, get_color_range(op));
    return op;
}
Expr GetStmtHierarchy::visit(const Cast *op) {
    stringstream name;
    name << op->type;
    int computation_range = get_color_range(op);
    open_node(name.str(), computation_range);
    mutate(op->value);
    close_node();
    return op;
}
Expr GetStmtHierarchy::visit(const Variable *op) {
    node_without_children(op->name, get_color_range(op));
    return op;
}

void GetStmtHierarchy::visit_binary_op(const Expr &a, const Expr &b, const string &name,
                                       int colorCost) {
    open_node(name, colorCost);

    int currNode = currNodeID;
    mutate(a);

    currNodeID = currNode;
    mutate(b);

    close_node();
}

Expr GetStmtHierarchy::visit(const Add *op) {
    int computation_range = get_color_range(op);
    visit_binary_op(op->a, op->b, "+", computation_range);
    return op;
}
Expr GetStmtHierarchy::visit(const Sub *op) {
    int computation_range = get_color_range(op);
    visit_binary_op(op->a, op->b, "-", computation_range);
    return op;
}
Expr GetStmtHierarchy::visit(const Mul *op) {
    int computation_range = get_color_range(op);
    visit_binary_op(op->a, op->b, "*", computation_range);
    return op;
}
Expr GetStmtHierarchy::visit(const Div *op) {
    int computation_range = get_color_range(op);
    visit_binary_op(op->a, op->b, "/", computation_range);
    return op;
}
Expr GetStmtHierarchy::visit(const Mod *op) {
    int computation_range = get_color_range(op);
    visit_binary_op(op->a, op->b, "%", computation_range);
    return op;
}
Expr GetStmtHierarchy::visit(const EQ *op) {
    int computation_range = get_color_range(op);
    visit_binary_op(op->a, op->b, "==", computation_range);
    return op;
}
Expr GetStmtHierarchy::visit(const NE *op) {
    int computation_range = get_color_range(op);
    visit_binary_op(op->a, op->b, "!=", computation_range);
    return op;
}
Expr GetStmtHierarchy::visit(const LT *op) {
    int computation_range = get_color_range(op);
    visit_binary_op(op->a, op->b, "<", computation_range);
    return op;
}
Expr GetStmtHierarchy::visit(const LE *op) {
    int computation_range = get_color_range(op);
    visit_binary_op(op->a, op->b, "<=", computation_range);
    return op;
}
Expr GetStmtHierarchy::visit(const GT *op) {
    int computation_range = get_color_range(op);
    visit_binary_op(op->a, op->b, ">", computation_range);
    return op;
}
Expr GetStmtHierarchy::visit(const GE *op) {
    int computation_range = get_color_range(op);
    visit_binary_op(op->a, op->b, ">=", computation_range);
    return op;
}
Expr GetStmtHierarchy::visit(const And *op) {
    int computation_range = get_color_range(op);
    visit_binary_op(op->a, op->b, "&amp;&amp;", computation_range);
    return op;
}
Expr GetStmtHierarchy::visit(const Or *op) {
    int computation_range = get_color_range(op);
    visit_binary_op(op->a, op->b, "||", computation_range);
    return op;
}
Expr GetStmtHierarchy::visit(const Min *op) {
    int computation_range = get_color_range(op);
    visit_binary_op(op->a, op->b, "min", computation_range);
    return op;
}
Expr GetStmtHierarchy::visit(const Max *op) {
    int computation_range = get_color_range(op);
    visit_binary_op(op->a, op->b, "max", computation_range);
    return op;
}

Expr GetStmtHierarchy::visit(const Not *op) {
    int computation_range = get_color_range(op);
    open_node("!", computation_range);
    mutate(op->a);
    close_node();
    return op;
}
Expr GetStmtHierarchy::visit(const Select *op) {
    int computation_range = get_color_range(op);
    open_node("Select", computation_range);

    int currNode = currNodeID;
    mutate(op->condition);

    currNodeID = currNode;
    mutate(op->true_value);

    currNodeID = currNode;
    mutate(op->false_value);

    close_node();
    return op;
}
Expr GetStmtHierarchy::visit(const Load *op) {
    stringstream index;
    index << op->index;
    node_without_children(op->name + "[" + index.str() + "]", get_color_range(op));
    return op;
}
Expr GetStmtHierarchy::visit(const Ramp *op) {
    int computation_range = get_color_range(op);
    open_node("Ramp", computation_range);

    int currNode = currNodeID;
    mutate(op->base);

    currNodeID = currNode;
    mutate(op->stride);

    currNodeID = currNode;
    mutate(Expr(op->lanes));

    close_node();
    return op;
}
Expr GetStmtHierarchy::visit(const Broadcast *op) {
    int computation_range = get_color_range(op);
    open_node("x" + to_string(op->lanes), computation_range);
    mutate(op->value);
    close_node();
    return op;
}
Expr GetStmtHierarchy::visit(const Call *op) {
    int computation_range = get_color_range(op);
    open_node(op->name, computation_range);

    int currNode = currNodeID;
    for (auto &arg : op->args) {
        currNodeID = currNode;
        mutate(arg);
    }

    close_node();
    return op;
}
Expr GetStmtHierarchy::visit(const Let *op) {
    int computation_range = get_color_range(op);
    open_node("Let", computation_range);
    int currNode = currNodeID;

    // "=" node
    int value_range = get_color_range(op->value.get());
    open_node("=", value_range);
    node_without_children(op->name, 0);
    mutate(op->value);
    close_node();

    // "body" node
    int computation_range_body = get_color_range(op->body.get());
    currNodeID = currNode;
    open_node("body", computation_range_body);
    mutate(op->body);
    close_node();

    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const LetStmt *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: LetStmt is not supported.\n\n";

    int computation_range = get_color_range(op);
    open_node("=", computation_range);

    int currNode = currNodeID;
    node_without_children(op->name, 0);

    currNodeID = currNode;
    mutate(op->value);

    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const AssertStmt *op) {
    int computation_range = get_color_range(op);
    open_node("Assert", computation_range);
    mutate(op->condition);
    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const ProducerConsumer *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: ProducerConsumer is not supported.\n\n";
    return op;
}
Stmt GetStmtHierarchy::visit(const For *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: For is not supported.\n\n";
    return op;
}
Stmt GetStmtHierarchy::visit(const Store *op) {
    int computation_range = get_color_range(op);
    open_node("=", computation_range);

    stringstream index;
    index << op->index;
    node_without_children(op->name + "[" + index.str() + "]", get_color_range(op->index.get()));

    mutate(op->value);
    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const Provide *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: Provide is not supported. Look into it though!!! \n\n";

    int computation_range = get_color_range(op);
    open_node("=", computation_range);
    int currNode0 = currNodeID;

    open_node(op->name, computation_range);
    int currNode1 = currNodeID;
    for (auto &arg : op->args) {
        currNodeID = currNode1;
        mutate(arg);
    }
    close_node();

    for (auto &val : op->values) {
        currNodeID = currNode0;
        mutate(val);
    }
    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const Allocate *op) {
    int computation_range = get_color_range(op);
    open_node("allocate", computation_range);

    stringstream index;
    index << op->type;

    for (const auto &extent : op->extents) {
        index << " * ";
        index << extent;
    }

    node_without_children(op->name + "[" + index.str() + "]", get_color_range_list(op->extents));

    if (!is_const_one(op->condition)) {
        internal_error
            << "\n"
            << "GetStmtHierarchy: Allocate `!is_const_one(op->condition)` is not supported.\n\n";
    }
    if (op->new_expr.defined()) {
        internal_error
            << "\n"
            << "GetStmtHierarchy: Allocate `op->new_expr.defined()` is not supported.\n\n";
    }
    if (!op->free_function.empty()) {
        internal_error
            << "\n"
            << "GetStmtHierarchy: Allocate `!op->free_function.empty()` is not supported.\n\n";
    }

    close_node();

    return op;
}
Stmt GetStmtHierarchy::visit(const Free *op) {
    int computation_range = get_color_range(op);
    open_node("Free", computation_range);
    node_without_children(op->name, get_color_range(op));
    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const Realize *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: Realize is not supported. Look into it though!!! \n\n";
    return op;
}
Stmt GetStmtHierarchy::visit(const Block *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: Block is not supported. Look into it though!!! \n\n";
    return op;
}
Stmt GetStmtHierarchy::visit(const IfThenElse *op) {
    int computation_range = get_color_range(op);
    open_node("IfThenElse", computation_range);

    mutate(op->condition);
    if (op->else_case.defined()) {
        mutate(op->else_case);
    }

    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const Evaluate *op) {
    mutate(op->value);
    return op;
}
Expr GetStmtHierarchy::visit(const Shuffle *op) {
    int computation_range = get_color_range(op);
    if (op->is_concat()) {
        open_node("concat_vectors", computation_range);

        int currNode = currNodeID;
        for (auto &e : op->vectors) {
            currNodeID = currNode;
            mutate(e);
        }
        close_node();
    }

    else if (op->is_interleave()) {
        open_node("interleave_vectors", computation_range);

        int currNode = currNodeID;
        for (auto &e : op->vectors) {
            currNodeID = currNode;
            mutate(e);
        }
        close_node();
    }

    else if (op->is_extract_element()) {
        std::vector<Expr> args = op->vectors;
        args.emplace_back(op->slice_begin());
        open_node("extract_element", computation_range);

        int currNode = currNodeID;
        for (auto &e : args) {
            currNodeID = currNode;
            mutate(e);
        }
        close_node();
    }

    else if (op->is_slice()) {
        std::vector<Expr> args = op->vectors;
        args.emplace_back(op->slice_begin());
        args.emplace_back(op->slice_stride());
        args.emplace_back(static_cast<int>(op->indices.size()));
        open_node("slice_vectors", computation_range);

        int currNode = currNodeID;
        for (auto &e : args) {
            currNodeID = currNode;
            mutate(e);
        }
        close_node();
    }

    else {
        std::vector<Expr> args = op->vectors;
        for (int i : op->indices) {
            args.emplace_back(i);
        }
        open_node("Shuffle", computation_range);

        int currNode = currNodeID;
        for (auto &e : args) {
            currNodeID = currNode;
            mutate(e);
        }
        close_node();
    }
    return op;
}
Expr GetStmtHierarchy::visit(const VectorReduce *op) {
    int computation_range = get_color_range(op);
    open_node("vector_reduce", computation_range);

    int currNode = currNodeID;
    mutate(op->op);

    currNodeID = currNode;
    mutate(op->value);

    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const Prefetch *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: Prefetch is not supported. Look into it though!!! \n\n";
    return op;
}
Stmt GetStmtHierarchy::visit(const Fork *op) {
    internal_error << "\n"
                   << "GetStmtHierarchy: Fork is not supported. Look into it though!!! \n\n";
    return op;
}
Stmt GetStmtHierarchy::visit(const Acquire *op) {
    int computation_range = get_color_range(op);
    open_node("acquire", computation_range);

    int currNode = currNodeID;
    mutate(op->semaphore);

    currNodeID = currNode;
    mutate(op->count);

    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const Atomic *op) {
    if (op->mutex_name.empty()) {
        node_without_children("atomic", get_color_range(op));
    } else {
        int computation_range = get_color_range(op);
        open_node("atomic", computation_range);
        node_without_children(op->mutex_name, 0);
        close_node();
    }

    return op;
}

string GetStmtHierarchy::generate_collapse_expand_js(int totalNodes) {
    stringstream js;
    js << "const nodeExpanded = new Map();";
    js << "function collapseAllNodes(numNodes) {";
    js << "    for (let i = 0; i < numNodes; i++) {";
    js << "        collapseNodeChildren(i);";
    js << "        nodeExpanded.set(i, false);";
    js << "        if (document.getElementById(\\'button\\' + i) != null) {";
    js << "            document.getElementById(\\'button\\' + i).className = \\'arrow down\\';";
    js << "        }";
    js << "    }";
    js << "}";
    js << "function expandNodesUpToDepth(depth) {";
    js << "    for (let i = 0; i < depth; i++) {";
    js << "        const depthChildren = document.getElementsByClassName(\\'depth\\' + i);";
    js << "        for (const child of depthChildren) {";
    js << "            child.style.display = \\'\\';";
    js << "            if (child.className.includes(\\'start\\')) {";
    js << "                continue;";
    js << "            }";
    js << "            let parentNodeID = child.className.split("
          ")[0];";
    js << "            parentNodeID = parentNodeID.split(\\'node\\')[1];";
    js << "            parentNodeID = parentNodeID.split(\\'child\\')[0];";
    js << "            const parentNode = parseInt(parentNodeID);";
    js << "            nodeExpanded.set(parentNode, true);";
    js << "            if (document.getElementById(\\'button\\' + parentNodeID) != null) {";
    js << "                document.getElementById(\\'button\\' + parentNodeID).className = "
          "\\'arrow up\\';";
    js << "            }";
    js << "            const dotdotdot = document.getElementById(\\'node\\' + parentNodeID + "
          "\\'dotdotdot\\');";
    js << "            if (dotdotdot != null) {";
    js << "                dotdotdot.remove();";
    js << "            }";
    js << "        }";
    js << "    }";
    js << "}";
    js << "function handleClick(nodeNum) {";
    js << "    if (nodeExpanded.get(nodeNum)) {";
    js << "        collapseNodeChildren(nodeNum);";
    js << "        nodeExpanded.set(nodeNum, false);";
    js << "    } else {";
    js << "        expandNodeChildren(nodeNum);";
    js << "        nodeExpanded.set(nodeNum, true);";
    js << "    }";
    js << "}";
    js << "function collapseNodeChildren(nodeNum) {";
    js << "    const children = document.getElementsByClassName(\\'node\\' + nodeNum + "
          "\\'child\\');";
    js << "    if (document.getElementById(\\'button\\' + nodeNum) != null) {";
    js << "        document.getElementById(\\'button\\' + nodeNum).className = \\'arrow down\\';";
    js << "    }";
    js << "    for (const child of children) {";
    js << "        child.style.display = \\'none\\';";
    js << "    }";
    js << "    const list = document.getElementById(\\'list\\' + nodeNum);";
    js << "    if (list != null) {";
    js << "        list.appendChild(addDotDotDotChild(nodeNum, 2));";
    js << "    }";
    js << "}";
    js << "function expandNodeChildren(nodeNum) {";
    js << "    const children = document.getElementsByClassName(\\'node\\' + nodeNum + "
          "\\'child\\');";
    js << "    if (document.getElementById(\\'button\\' + nodeNum) != null) {";
    js << "        document.getElementById(\\'button\\' + nodeNum).className = \\'arrow up\\';";
    js << "    }";
    js << "    for (const child of children) {";
    js << "        child.style.display = \\'\\';";
    js << "    }";
    js << "     const dotdotdot = document.getElementById(\\'node\\' + nodeNum + \\'dotdotdot\\');";
    js << "     if (dotdotdot != null) {";
    js << "         dotdotdot.remove();";
    js << "     }";
    js << "}";
    js << "function addDotDotDotChild(nodeNum, colorCost) {";
    js << "    var liDotDotDot = document.createElement(\\'li\\');";
    js << "    liDotDotDot.id = \\'node\\' + nodeNum + \\'dotdotdot\\';";
    js << "    const span =&quot;<span class=\\'tf-nc end-node CostComputationBorder&quot; + "
          "colorCost +  &quot;\\'>...</span> &quot;;";
    js << "    liDotDotDot.innerHTML = span;";
    js << "    return liDotDotDot;";
    js << "}";
    js << "collapseAllNodes(" << totalNodes << ");  ";
    js << "expandNodesUpToDepth(4);";
    return js.str();
}
