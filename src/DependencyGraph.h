#ifndef DEPENDENCYGRAPH_H
#define DEPENDENCYGRAPH_H

#include <set>

// #include "FindStmtCost.h"
#include "IRMutator.h"
// #include "IROperator.h"

using namespace std;
using namespace Halide;
using namespace Internal;

// #define CC_TYPE 0
// #define DMC_TYPE 1

#define m_assert(expr, msg) assert((void(msg), (expr)))

struct DependencyNode {
    int nodeID;
    string nodeName;
    vector<const string> nodeDependsOn;
};

class DependencyGraph : public IRMutator {

public:
    DependencyGraph() : current_variable("") {
    }
    ~DependencyGraph() = default;

    void generate_dependency_graph(const Module &m);
    void generate_dependency_graph(const Stmt &stmt);

private:
    vector<DependencyNode> dependency_graph;

    map<const string, vector<const string>> dependencies;  // key: variable name, value: vector of
                                                           // dependencies
    map<const string, int> duplicate_variable_counts;      // key: variable name, value: number of
                                                           // duplicates
    string current_variable;  // current variable name that is being processed

    string generate_unique_name(const string &name);
    string get_unique_name(const string &name) const;

    // TODO: change this name later
    void print_stuff();

    void build_graph();
    void generate_nodes();
    DependencyNode get_node(const string &name);

    void add_dependency(const string &variable, const string &dependency);
    void add_empty_dependency(const string &variable);
    vector<const string> get_dependencies(const string &variable);

    void traverse(const Module &m);

    void print_dependencies();

    Expr visit(const Let *op) override;
    Expr visit(const Variable *op) override;
    Stmt visit(const LetStmt *op) override;

    // Expr visit(const IntImm *op) override;
    // Expr visit(const UIntImm *op) override;
    // Expr visit(const FloatImm *op) override;
    // Expr visit(const StringImm *op) override;
    // Expr visit(const Cast *op) override;
    // Expr visit(const Add *op) override;
    // Expr visit(const Sub *op) override;
    // Expr visit(const Mul *op) override;
    // Expr visit(const Div *op) override;
    // Expr visit(const Mod *op) override;
    // Expr visit(const Min *op) override;
    // Expr visit(const Max *op) override;
    // Expr visit(const EQ *op) override;
    // Expr visit(const NE *op) override;
    // Expr visit(const LT *op) override;
    // Expr visit(const LE *op) override;
    // Expr visit(const GT *op) override;
    // Expr visit(const GE *op) override;
    // Expr visit(const And *op) override;
    // Expr visit(const Or *op) override;
    // Expr visit(const Not *op) override;
    // Expr visit(const Select *op) override;
    // Expr visit(const Load *op) override;
    // Expr visit(const Ramp *op) override;
    // Expr visit(const Broadcast *op) override;
    // Expr visit(const Call *op) override;
    // Expr visit(const Shuffle *op) override;
    // Expr visit(const VectorReduce *op) override;
    // Stmt visit(const AssertStmt *op) override;
    // Stmt visit(const ProducerConsumer *op) override;
    // Stmt visit(const For *op) override;
    // Stmt visit(const Acquire *op) override;
    // Stmt visit(const Store *op) override;
    // Stmt visit(const Provide *op) override;
    // Stmt visit(const Allocate *op) override;
    // Stmt visit(const Free *op) override;
    // Stmt visit(const Realize *op) override;
    // Stmt visit(const Prefetch *op) override;
    // Stmt visit(const Block *op) override;
    // Stmt visit(const Fork *op) override;
    // Stmt visit(const IfThenElse *op) override;
    // Stmt visit(const Evaluate *op) override;
    // Stmt visit(const Atomic *op) override;
};

#endif
