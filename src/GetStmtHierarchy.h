#ifndef GETSTMTHIERARCHY_H
#define GETSTMTHIERARCHY_H

#include <set>

#include "IRMutator.h"
#include "IROperator.h"

/** \file
 * Defines the base class for things that recursively walk over the IR
 */
using namespace std;
using namespace Halide;
using namespace Internal;

class GetStmtHierarchy : public IRMutator {

public:
    GetStmtHierarchy() = default;
    ~GetStmtHierarchy() = default;

    string get_hierarchy_html(const Expr &startNode) {
        start_html();
        start_tree();
        mutate(startNode);
        end_tree();
        end_html();

        return html.str();
    }

    string get_hierarchy_html(const Stmt &startNode) {
        start_html();
        start_tree();
        mutate(startNode);
        end_tree();
        end_html();

        return html.str();
    }

private:
    std::stringstream html;

    Expr mutate(const Expr &expr) override {
        return IRMutator::mutate(expr);
    }

    Stmt mutate(const Stmt &stmt) override {
        return IRMutator::mutate(stmt);
    }

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
    Stmt visit(const LetStmt *op) override {
        open_node("=");
        node_without_children(op->name);
        mutate(op->value);
        close_node();
        return op;
    }
    Stmt visit(const AssertStmt *op) override {
        open_node("Assert");
        mutate(op->condition);
        close_node();
        return op;
    }
    Stmt visit(const ProducerConsumer *op) override {
        m_assert(false, "shouldn't be visualizing ProducerConsumer");
        return op;
    }
    Stmt visit(const For *op) override {
        m_assert(false, "shouldn't be visualizing For");
        return op;
    }
    Stmt visit(const Store *op) override {
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
    Stmt visit(const Provide *op) override {
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
    Stmt visit(const Allocate *op) override {
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
    Stmt visit(const Free *op) override {
        open_node("Free");
        node_without_children(op->name);
        close_node();
        return op;
    }
    Stmt visit(const Realize *op) override {
        m_assert(false, "visualizing Realize !! look into it");
        return op;
    }
    Stmt visit(const Block *op) override {
        m_assert(false, "visualizing Block !! look into it");
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
        mutate(op->value);
        return op;
    }
    Expr visit(const Shuffle *op) override {
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
    Expr visit(const VectorReduce *op) override {
        open_node("vector_reduce");
        mutate(op->op);
        mutate(op->value);
        close_node();
        return op;
    }
    Stmt visit(const Prefetch *op) override {
        m_assert(false, "visualizing Prefetch !! look into it");
        return op;
    }
    Stmt visit(const Fork *op) override {
        m_assert(false, "visualizing Fork !! look into it");
        return op;
    }
    Stmt visit(const Acquire *op) override {
        open_node("acquire");
        mutate(op->semaphore);
        mutate(op->count);
        close_node();
        return op;
    }
    Stmt visit(const Atomic *op) override {
        if (op->mutex_name.empty()) {
            node_without_children("atomic");
        } else {
            open_node("atomic");
            node_without_children(op->mutex_name);
            close_node();
        }

        return op;
    }
};

#endif
