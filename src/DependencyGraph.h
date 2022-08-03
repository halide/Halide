#ifndef DEPENDENCYGRAPH_H
#define DEPENDENCYGRAPH_H

#include <set>

// #include "FindStmtCost.h"
#include "IRMutator.h"
// #include "IROperator.h"

using namespace std;
using namespace Halide;
using namespace Internal;

struct DependencyNode {
    int nodeID;
    string nodeName;
    vector<string> nodeDependsOn;
};

class DependencyGraph : public IRMutator {

public:
    DependencyGraph() : current_variable("") {
    }
    ~DependencyGraph() = default;

    string generate_dependency_graph(const Module &m);
    string generate_dependency_graph(const Stmt &stmt);

private:
    using IRMutator::visit;

    vector<DependencyNode> dependency_graph;
    stringstream html;

    map<const string, vector<string>> dependencies;    // key: variable name, value: vector of
                                                       // dependencies
    map<const string, int> duplicate_variable_counts;  // key: variable name, value: number of
                                                       // duplicates
    string current_variable;  // current variable name that is being processed

    string generate_unique_name(const string &name);
    string get_unique_name(const string &name) const;

    // TODO: change this name later
    void generate_html();

    void start_html();
    void end_html();

    void build_graph();
    void generate_nodes();
    DependencyNode get_node(const string &name);

    void add_dependency(const string &variable, const string &dependency);
    void add_empty_dependency(const string &variable);
    vector<string> get_dependencies(const string &variable);

    void traverse(const Module &m);

    void print_dependencies();

    Expr visit(const Let *op) override;
    Expr visit(const Variable *op) override;
    Stmt visit(const LetStmt *op) override;
    Stmt visit(const Store *op) override;
};

#endif
