#ifndef GETSTMTHIERARCHY_H
#define GETSTMTHIERARCHY_H

#include <set>

#include "IRMutator.h"

/** \file
 * Defines the base class for things that recursively walk over the IR
 */
using namespace std;
using namespace Halide;
using namespace Internal;

/** A base class for algorithms that need to recursively walk over the
 * IR. The default implementations just recursively walk over the
 * children. Override the ones you care about.
 */
class GetStmtHierarchy : public IRMutator {
public:
    GetStmtHierarchy() = default;
    ~GetStmtHierarchy() = default;

    string get_hierarchy_html(const Expr &startNode) {
        start_html();
        start_tree();
        // cout << "Start node: " << startNode << endl;
        mutate(startNode);
        end_tree();
        end_html();

        return html.str();
    }

    string get_hierarchy_html(const Stmt &startNode) {
        start_html();
        start_tree();
        // cout << "Start node: " << startNode << endl;
        mutate(startNode);
        end_tree();
        end_html();

        return html.str();
    }

    Expr mutate(const Expr &expr) override {
        return IRMutator::mutate(expr);
    }

    Stmt mutate(const Stmt &stmt) override {
        return IRMutator::mutate(stmt);
    }

private:
    std::stringstream html;

    void start_html() {
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

    void end_html() {
        html << "</body></html>";
    }
    void start_tree() {
        html << "<div class=\\'tf-tree tf-gap-sm tf-custom\\' style=\\'font-size: 15px;\\'>";
        html << "<ul>";
    }

    void end_tree() {
        html << "</ul>";
        html << "</div>";
    }

    void node_without_children(string name) {
        html << "<li><span class=\\'tf-nc end-node\\'>" << name << "</span></li>";
    }

    void open_node(string name) {
        html << "<li><span class=\\'tf-nc children-node\\'>" << name << "</span>";
        html << "<ul>";
    }

    void close_node() {
        html << "</ul>";
        html << "</li>";
    }

    Expr visit(const IntImm *op) override {
        node_without_children(to_string(op->value));
        return op;
    }
    Expr visit(const UIntImm *op) override {
        node_without_children(to_string(op->value));
        return op;
    }
    Expr visit(const FloatImm *op) override {
        node_without_children(to_string(op->value));
        return op;
    }
    Expr visit(const StringImm *op) override {
        node_without_children(op->value);
        return op;
    }
    Expr visit(const Cast *op) override {
        stringstream name;
        name << op->type;
        open_node(name.str());
        mutate(op->value);
        close_node();
        return op;
    }
    Expr visit(const Variable *op) override {
        node_without_children(op->name);
        return op;
    }

    void visit_binary_op(const Expr &a, const Expr &b, const string &name) {
        open_node(name);
        // cout << "in binary, name: " << name << ", a: " << a << ", b: " << b << endl;
        mutate(a);
        mutate(b);
        close_node();
    }

    Expr visit(const Add *op) override {
        visit_binary_op(op->a, op->b, "+");
        return op;
    }
    Expr visit(const Sub *op) override {
        visit_binary_op(op->a, op->b, "-");
        return op;
    }
    Expr visit(const Mul *op) override {
        visit_binary_op(op->a, op->b, "*");
        return op;
    }
    Expr visit(const Div *op) override {
        visit_binary_op(op->a, op->b, "/");
        return op;
    }
    Expr visit(const Mod *op) override {
        visit_binary_op(op->a, op->b, "%");
        return op;
    }
    Expr visit(const EQ *op) override {
        visit_binary_op(op->a, op->b, "==");
        return op;
    }
    Expr visit(const NE *op) override {
        visit_binary_op(op->a, op->b, "!=");
        return op;
    }
    Expr visit(const LT *op) override {
        visit_binary_op(op->a, op->b, "<");
        return op;
    }
    Expr visit(const LE *op) override {
        visit_binary_op(op->a, op->b, "<=");
        return op;
    }
    Expr visit(const GT *op) override {
        visit_binary_op(op->a, op->b, ">");
        return op;
    }
    Expr visit(const GE *op) override {
        visit_binary_op(op->a, op->b, ">=");
        return op;
    }
    Expr visit(const And *op) override {
        visit_binary_op(op->a, op->b, "&amp;&amp;");
        return op;
    }
    Expr visit(const Or *op) override {
        visit_binary_op(op->a, op->b, "||");
        return op;
    }
    Expr visit(const Min *op) override {
        visit_binary_op(op->a, op->b, "min");
        return op;
    }
    Expr visit(const Max *op) override {
        visit_binary_op(op->a, op->b, "max");
        return op;
    }

    Expr visit(const Not *op) override {
        open_node("!");
        mutate(op->a);
        close_node();
        return op;
    }
    Expr visit(const Select *op) override {
        open_node("Select");
        mutate(op->condition);
        mutate(op->true_value);
        mutate(op->false_value);
        close_node();
        return op;
    }
    Expr visit(const Load *op) override {
        stringstream index;
        index << op->index;
        node_without_children(op->name + "[" + index.str() + "]");
        return op;
    }
    Expr visit(const Ramp *op) override {
        open_node("Ramp");
        mutate(op->base);
        mutate(op->stride);
        mutate(Expr(op->lanes));
        close_node();
        return op;
    }
    Expr visit(const Broadcast *op) override {
        open_node("x" + to_string(op->lanes));
        mutate(op->value);
        close_node();
        return op;
    }
    Expr visit(const Call *op) override {
        open_node(op->name);
        for (auto &arg : op->args) {
            mutate(arg);
        }
        close_node();
        return op;
    }
    Expr visit(const Let *op) override {
        node_without_children("TODO: change Let");
        return op;
    }
    Stmt visit(const LetStmt *op) override {
        node_without_children("TODO: change LetStmt");
        return op;
    }
    Stmt visit(const AssertStmt *op) override {
        open_node("Assert");
        mutate(op->condition);
        close_node();
        return op;
    }
    Stmt visit(const ProducerConsumer *op) override {
        node_without_children("TODO: change ProducerConsumer");
        return op;
    }
    Stmt visit(const For *op) override {
        node_without_children("TODO: change For");
        return op;
    }
    Stmt visit(const Store *op) override {
        stringstream index;
        index << op->index;
        stringstream value;
        value << op->value;
        node_without_children(op->name + "[" + index.str() + "] = " + value.str());
        return op;
    }
    Stmt visit(const Provide *op) override {
        node_without_children("TODO: change Provide");
        return op;
    }
    Stmt visit(const Allocate *op) override {
        node_without_children("TODO: change Allocate");
        return op;
    }
    Stmt visit(const Free *op) override {
        open_node("Free");
        node_without_children(op->name);
        close_node();
        return op;
    }
    Stmt visit(const Realize *op) override {
        node_without_children("TODO: change Realize");
        return op;
    }
    Stmt visit(const Block *op) override {
        node_without_children("TODO: change Block");
        return op;
    }
    Stmt visit(const IfThenElse *op) override {
        open_node("IfThenElse");
        mutate(op->condition);
        if (op->else_case.defined()) {
            mutate(op->else_case);
        }
        close_node();
        return op;
    }
    Stmt visit(const Evaluate *op) override {
        open_node("Evaluate");
        mutate(op->value);
        close_node();
        return op;
    }
    Expr visit(const Shuffle *op) override {
        node_without_children("TODO: change Shuffle");
        return op;
    }
    Expr visit(const VectorReduce *op) override {
        node_without_children("TODO: change VectorReduce");
        return op;
    }
    Stmt visit(const Prefetch *op) override {
        node_without_children("TODO: change Prefetch");
        return op;
    }
    Stmt visit(const Fork *op) override {
        node_without_children("TODO: change Fork");
        return op;
    }
    Stmt visit(const Acquire *op) override {
        node_without_children("TODO: change Acquire");
        return op;
    }
    Stmt visit(const Atomic *op) override {
        node_without_children("TODO: change Atomic");
        return op;
    }
};

#endif
