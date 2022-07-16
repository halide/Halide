#ifndef ProducerConsumerHierarchy_H
#define ProducerConsumerHierarchy_H

#include <set>

#include "IRMutator.h"

using namespace std;
using namespace Halide;
using namespace Internal;

class ProducerConsumerHierarchy : public IRMutator {

public:
    ProducerConsumerHierarchy() = default;
    ~ProducerConsumerHierarchy() = default;

    void generate_producer_consumer_html(const Module &m) {
        start_html();
        traverse(m);
        end_html();
    }

    void generate_producer_consumer_html(const Stmt &stmt) {
        start_html();
        mutate(stmt);
        end_html();
    }

    void print_hiararchy() {
        cout << endl;
        cout << "Hiararchy HTML: ";
        cout << endl;
        cout << html.str();
        cout << endl;
    }

private:
    std::stringstream html;

    Expr mutate(const Expr &expr) override {
        return IRMutator::mutate(expr);
    }
    Stmt mutate(const Stmt &stmt) override {
        return IRMutator::mutate(stmt);
    }

    string get_hierarchy_html(const Expr &startNode) {
        start_html();
        mutate(startNode);
        end_html();

        return html.str();
    }
    string get_producer_consumer_html(const Stmt &startNode) {
        start_html();
        mutate(startNode);
        end_html();

        return html.str();
    }

    void traverse(const Module &m) {
        // recursively traverse all submodules
        for (const auto &s : m.submodules()) {
            traverse(s);
        }
        // traverse all functions
        for (const auto &f : m.functions()) {
            mutate(f.body);
        }
    }

    void start_html() {
        html.str(string());
        html << "<html>";
        html << "<head>";
        html << "<link rel=\\'stylesheet\\' href=\\'https://unpkg.com/treeflex/dist/css/treeflex.css\\'>";
        html << "</head>";
        html << "<style>";
        html << "body { font-family: Consolas, \\'Liberation Mono\\', Menlo, Courier, monospace;}";
        html << "table, th, td { border: 1px solid black; border-collapse: collapse; padding: 15px; border-spacing: 15px; } ";
        html << "</style>";
        html << "<body>";
    }

    void end_html() {
        html << "</body></html>";
    }

    void open_table() {
        html << "<table>";
    }

    void close_table() {
        html << "</table>";
    }

    void table_header(const string &header) {
        html << "<tr><th>" << header << "</th></tr>";
    }

    void open_table_row() {
        html << "<tr>";
        html << "<td>";
    }

    void close_table_row() {
        html << "</td>";
        html << "</tr>";
    }

    Stmt visit(const ProducerConsumer *op) override {
        open_table();
        stringstream header;
        header << (op->is_producer ? "Produce" : "Consumer");
        header << " " << op->name;
        table_header(header.str());
        open_table_row();
        mutate(op->body);
        close_table_row();
        close_table();

        return op;
    }
};

#endif
