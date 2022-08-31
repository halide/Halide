#ifndef DEPENDENCYGRAPH_H
#define DEPENDENCYGRAPH_H

#include <set>

// #include "FindStmtCost.h"
#include "IRVisitor.h"
// #include "IROperator.h"

#include <map>

using namespace std;
using namespace Halide;
using namespace Internal;

struct DependencyNode {
    int nodeID;
    string nodeName;
    vector<string> nodeDependsOn;
};

class DependencyGraph : public IRVisitor {

public:
    // returns the generated hierarchy's html
    string generate_dependency_graph(const Module &m);
    string generate_dependency_graph(const Stmt &stmt);

private:
    using IRVisitor::visit;

    map<const string, vector<string>> dependencies;    // key: variable name, value: vector of
                                                       // dependencies
    map<const string, int> duplicate_variable_counts;  // key: variable name, value: number of
                                                       // duplicates
    string current_variable;  // current variable name that is being processed

    vector<DependencyNode> dependency_graph;  // stores list of nodes in the graph
    stringstream html;                        // html string

    // to avoid duplicates in the graph
    // TODO: might remove this later (depending on convo with Maaz and Marcos)
    string generate_unique_name(const string &name);
    string get_unique_name(const string &name) const;

    // generates the html string
    void generate_html();

    // starts/ends the html string
    void start_html();
    void end_html();

    // builds up the `dependency_graph` vector
    void build_graph();

    // generates the strings for the nodes in the html string
    void generate_nodes_in_html();

    // gets `DependencyNode` for the given variable name
    DependencyNode get_node(const string &name);

    // adds/gets dependency to/from `dependencies` map
    void add_dependency(const string &variable, const string &dependency);
    void add_empty_dependency(const string &variable);
    vector<string> get_dependencies(const string &variable);

    // traverses a Module
    void traverse(const Module &m);

    // prints the dependency from `dependencies` list to the console
    void print_dependencies();

    void visit(const Let *op) override;
    void visit(const Variable *op) override;
    void visit(const LetStmt *op) override;
    void visit(const Store *op) override;
};

#endif
