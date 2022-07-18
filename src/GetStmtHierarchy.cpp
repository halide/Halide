#include "GetStmtHierarchy.h"

using namespace std;
using namespace Halide;
using namespace Internal;

string GetStmtHierarchy::get_hierarchy_html(const Expr &startNode) {
    GetStmtHierarchy::start_html();
    GetStmtHierarchy::start_tree();
    GetStmtHierarchy::mutate(startNode);
    GetStmtHierarchy::end_tree();
    GetStmtHierarchy::end_html();

    return html.str();
}

string GetStmtHierarchy::get_hierarchy_html(const Stmt &startNode) {
    start_html();
    start_tree();
    mutate(startNode);
    end_tree();
    end_html();

    return html.str();
}

void GetStmtHierarchy::start_html() {
    html.str(string());
    html << "<html>";
    html << "<head>";
    html << "<link rel=\\'stylesheet\\' href=\\'https://unpkg.com/treeflex/dist/css/treeflex.css\\'>";
    html << "</head>";
    html << "<style>";
    html << ".tf-custom .tf-nc { border-radius: 5px; border: 1px solid; }";
    html << ".tf-custom .end-node { border-style: dashed; } ";
    html << ".tf-custom .tf-nc:before, .tf-custom .tf-nc:after { border-left-width: 1px; } ";
    html << ".tf-custom li li:before { border-top-width: 1px; } .tf-custom .children-node { background-color: lightgrey; }";
    html << "body { font-family: Consolas, \\'Liberation Mono\\', Menlo, Courier, monospace;}";
    html << "</style>";
    html << "<body>";
}

void GetStmtHierarchy::end_html() {
    html << "</body></html>";
}
void GetStmtHierarchy::start_tree() {
    html << "<div class=\\'tf-tree tf-gap-sm tf-custom\\' style=\\'font-size: 15px;\\'>";
    html << "<ul>";
}

void GetStmtHierarchy::end_tree() {
    html << "</ul>";
    html << "</div>";
}

void GetStmtHierarchy::node_without_children(string name) {
    html << "<li><span class=\\'tf-nc end-node\\'>" << name << "</span></li>";
}

void GetStmtHierarchy::open_node(string name) {
    html << "<li><span class=\\'tf-nc children-node\\'>" << name << "</span>";
    html << "<ul>";
}

void GetStmtHierarchy::close_node() {
    html << "</ul>";
    html << "</li>";
}

Expr GetStmtHierarchy::visit(const IntImm *op) {
    node_without_children(to_string(op->value));
    return op;
}
Expr GetStmtHierarchy::visit(const UIntImm *op) {
    node_without_children(to_string(op->value));
    return op;
}
Expr GetStmtHierarchy::visit(const FloatImm *op) {
    node_without_children(to_string(op->value));
    return op;
}
Expr GetStmtHierarchy::visit(const StringImm *op) {
    node_without_children(op->value);
    return op;
}
Expr GetStmtHierarchy::visit(const Cast *op) {
    stringstream name;
    name << op->type;
    open_node(name.str());
    mutate(op->value);
    close_node();
    return op;
}
Expr GetStmtHierarchy::visit(const Variable *op) {
    node_without_children(op->name);
    return op;
}

void GetStmtHierarchy::visit_binary_op(const Expr &a, const Expr &b, const string &name) {
    open_node(name);
    mutate(a);
    mutate(b);
    close_node();
}

Expr GetStmtHierarchy::visit(const Add *op) {
    visit_binary_op(op->a, op->b, "+");
    return op;
}
Expr GetStmtHierarchy::visit(const Sub *op) {
    visit_binary_op(op->a, op->b, "-");
    return op;
}
Expr GetStmtHierarchy::visit(const Mul *op) {
    visit_binary_op(op->a, op->b, "*");
    return op;
}
Expr GetStmtHierarchy::visit(const Div *op) {
    visit_binary_op(op->a, op->b, "/");
    return op;
}
Expr GetStmtHierarchy::visit(const Mod *op) {
    visit_binary_op(op->a, op->b, "%");
    return op;
}
Expr GetStmtHierarchy::visit(const EQ *op) {
    visit_binary_op(op->a, op->b, "==");
    return op;
}
Expr GetStmtHierarchy::visit(const NE *op) {
    visit_binary_op(op->a, op->b, "!=");
    return op;
}
Expr GetStmtHierarchy::visit(const LT *op) {
    visit_binary_op(op->a, op->b, "<");
    return op;
}
Expr GetStmtHierarchy::visit(const LE *op) {
    visit_binary_op(op->a, op->b, "<=");
    return op;
}
Expr GetStmtHierarchy::visit(const GT *op) {
    visit_binary_op(op->a, op->b, ">");
    return op;
}
Expr GetStmtHierarchy::visit(const GE *op) {
    visit_binary_op(op->a, op->b, ">=");
    return op;
}
Expr GetStmtHierarchy::visit(const And *op) {
    visit_binary_op(op->a, op->b, "&amp;&amp;");
    return op;
}
Expr GetStmtHierarchy::visit(const Or *op) {
    visit_binary_op(op->a, op->b, "||");
    return op;
}
Expr GetStmtHierarchy::visit(const Min *op) {
    visit_binary_op(op->a, op->b, "min");
    return op;
}
Expr GetStmtHierarchy::visit(const Max *op) {
    visit_binary_op(op->a, op->b, "max");
    return op;
}

Expr GetStmtHierarchy::visit(const Not *op) {
    open_node("!");
    mutate(op->a);
    close_node();
    return op;
}
Expr GetStmtHierarchy::visit(const Select *op) {
    open_node("Select");
    mutate(op->condition);
    mutate(op->true_value);
    mutate(op->false_value);
    close_node();
    return op;
}
Expr GetStmtHierarchy::visit(const Load *op) {
    stringstream index;
    index << op->index;
    node_without_children(op->name + "[" + index.str() + "]");
    return op;
}
Expr GetStmtHierarchy::visit(const Ramp *op) {
    open_node("Ramp");
    mutate(op->base);
    mutate(op->stride);
    mutate(Expr(op->lanes));
    close_node();
    return op;
}
Expr GetStmtHierarchy::visit(const Broadcast *op) {
    open_node("x" + to_string(op->lanes));
    mutate(op->value);
    close_node();
    return op;
}
Expr GetStmtHierarchy::visit(const Call *op) {
    open_node(op->name);
    for (auto &arg : op->args) {
        mutate(arg);
    }
    close_node();
    return op;
}
Expr GetStmtHierarchy::visit(const Let *op) {
    open_node("Let");
    open_node("=");
    node_without_children(op->name);
    mutate(op->value);
    close_node();
    open_node("body");
    mutate(op->body);
    close_node();
    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const LetStmt *op) {
    open_node("=");
    node_without_children(op->name);
    mutate(op->value);
    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const AssertStmt *op) {
    open_node("Assert");
    mutate(op->condition);
    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const ProducerConsumer *op) {
    m_assert(false, "shouldn't be visualizing ProducerConsumer");
    return op;
}
Stmt GetStmtHierarchy::visit(const For *op) {
    m_assert(false, "shouldn't be visualizing For");
    return op;
}
Stmt GetStmtHierarchy::visit(const Store *op) {
    open_node("=");
    stringstream index;
    index << op->index;
    stringstream value;
    value << op->value;
    node_without_children(op->name + "[" + index.str() + "]");
    mutate(op->value);
    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const Provide *op) {
    open_node("=");
    open_node(op->name);
    for (auto &arg : op->args) {
        mutate(arg);
    }
    close_node();
    for (auto &val : op->values) {
        mutate(val);
    }
    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const Allocate *op) {
    open_node("allocate");
    stringstream index;
    index << op->type;

    for (const auto &extent : op->extents) {
        index << " * ";
        index << extent;
    }

    node_without_children(op->name + "[" + index.str() + "]");

    if (!is_const_one(op->condition)) {
        m_assert(false, "visualizing Allocate: !is_const_one(op->condition) !! look into it");
    }
    if (op->new_expr.defined()) {
        m_assert(false, "visualizing Allocate: op->new_expr.defined() !! look into it");
    }
    if (!op->free_function.empty()) {
        m_assert(false, "visualizing Allocate: !op->free_function.empty() !! look into it");
    }

    close_node();

    return op;
}
Stmt GetStmtHierarchy::visit(const Free *op) {
    open_node("Free");
    node_without_children(op->name);
    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const Realize *op) {
    m_assert(false, "visualizing Realize !! look into it");
    return op;
}
Stmt GetStmtHierarchy::visit(const Block *op) {
    m_assert(false, "visualizing Block !! look into it");
    return op;
}
Stmt GetStmtHierarchy::visit(const IfThenElse *op) {
    open_node("IfThenElse");
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
    if (op->is_concat()) {
        open_node("concat_vectors");
        for (auto &e : op->vectors) {
            mutate(e);
        }
        close_node();
    }

    else if (op->is_interleave()) {
        open_node("interleave_vectors");
        for (auto &e : op->vectors) {
            mutate(e);
        }
        close_node();
    }

    else if (op->is_extract_element()) {
        std::vector<Expr> args = op->vectors;
        args.emplace_back(op->slice_begin());
        open_node("extract_element");
        for (auto &e : args) {
            mutate(e);
        }
        close_node();
    }

    else if (op->is_slice()) {
        std::vector<Expr> args = op->vectors;
        args.emplace_back(op->slice_begin());
        args.emplace_back(op->slice_stride());
        args.emplace_back(static_cast<int>(op->indices.size()));
        open_node("slice_vectors");
        for (auto &e : args) {
            mutate(e);
        }
        close_node();
    }

    else {
        std::vector<Expr> args = op->vectors;
        for (int i : op->indices) {
            args.emplace_back(i);
        }
        open_node("Shuffle");
        for (auto &e : args) {
            mutate(e);
        }
        close_node();
    }
    return op;
}
Expr GetStmtHierarchy::visit(const VectorReduce *op) {
    open_node("vector_reduce");
    mutate(op->op);
    mutate(op->value);
    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const Prefetch *op) {
    m_assert(false, "visualizing Prefetch !! look into it");
    return op;
}
Stmt GetStmtHierarchy::visit(const Fork *op) {
    m_assert(false, "visualizing Fork !! look into it");
    return op;
}
Stmt GetStmtHierarchy::visit(const Acquire *op) {
    open_node("acquire");
    mutate(op->semaphore);
    mutate(op->count);
    close_node();
    return op;
}
Stmt GetStmtHierarchy::visit(const Atomic *op) {
    if (op->mutex_name.empty()) {
        node_without_children("atomic");
    } else {
        open_node("atomic");
        node_without_children(op->mutex_name);
        close_node();
    }

    return op;
}
