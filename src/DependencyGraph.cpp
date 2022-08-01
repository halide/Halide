#include "DependencyGraph.h"
#include "Module.h"
#include "Substitute.h"

using namespace std;
using namespace Halide;
using namespace Internal;

void DependencyGraph::generate_dependency_graph(const Module &m) {
    traverse(m);
    print_stuff();
}
void DependencyGraph::generate_dependency_graph(const Stmt &stmt) {
    mutate(stmt);
    print_stuff();
}

void DependencyGraph::print_stuff() {
    build_graph();
    generate_nodes();
}

void DependencyGraph::build_graph() {
    for (const auto &kv : dependencies) {
        DependencyNode node;
        node.nodeID = dependency_graph.size();
        node.nodeName = kv.first;
        node.nodeDependsOn = vector<string>(kv.second);
        dependency_graph.push_back(node);
    }
}

DependencyNode DependencyGraph::get_node(const string &name) {
    for (const auto &node : dependency_graph) {
        if (node.nodeName == name) {
            return node;
        }
    }

    DependencyNode node;
    node.nodeID = dependency_graph.size();
    node.nodeName = name;
    node.nodeDependsOn = vector<string>();
    dependency_graph.push_back(node);
    return node;
}

void DependencyGraph::generate_nodes() {

    stringstream setEdges;
    // g.setEdge(3, 4);
    for (const auto &node : dependency_graph) {
        for (const auto &dependency : node.nodeDependsOn) {
            setEdges << "g.setEdge(" << get_node(dependency).nodeID << ", " << node.nodeID << ");"
                     << endl;
        }
    }

    stringstream setNodes;
    // g.setNode(19, { label: "blur_y.s0.x.x" });
    for (const auto &node : dependency_graph) {
        setNodes << "g.setNode(" << node.nodeID << ", { label: \"" << node.nodeName << "\" });"
                 << endl;
    }

    cout << setNodes.str() << endl;
    cout << setEdges.str() << endl;
}

string DependencyGraph::generate_unique_name(const string &name) {
    auto it = dependencies.find(name);

    // no duplicate variable found
    if (it == dependencies.end()) {
        return name;
    }

    // need to create a new unique name
    else {
        auto it2 = duplicate_variable_counts.find(name);

        // doesn't yet have duplicated name
        if (it2 == duplicate_variable_counts.end()) {
            duplicate_variable_counts[name] = 2;
            return name + "_" + to_string(2);
        }

        // already has duplicated name
        else {
            it2->second++;
            return name + "_" + to_string(it2->second);
        }
    }
}

string DependencyGraph::get_unique_name(const string &name) const {

    auto it = duplicate_variable_counts.find(name);

    if (it == duplicate_variable_counts.end()) {
        return name;
    } else {
        return name + "_" + to_string(it->second);
    }
}

void DependencyGraph::add_dependency(const string &variable, const string &dependency) {
    if (variable == "") {
        return;
    }
    auto it = dependencies.find(variable);
    if (it == dependencies.end()) {
        m_assert(false, "current_variable should be already in the map");
    } else {
        it->second.push_back(dependency);
    }
}
void DependencyGraph::add_empty_dependency(const string &variable) {
    auto it = dependencies.find(variable);
    if (it == dependencies.end()) {
        dependencies[variable] = vector<string>{};
    } else {
        m_assert(false, "variable already exists");
    }
}

vector<string> DependencyGraph::get_dependencies(const string &variable) {
    auto it = dependencies.find(variable);
    if (it == dependencies.end()) {
        m_assert(false, "variable not found in `dependencies`");
    } else {
        return it->second;
    }
}

void DependencyGraph::traverse(const Module &m) {
    // recursively traverse all submodules
    for (const auto &s : m.submodules()) {
        traverse(s);
    }
    // traverse all functions
    for (const auto &f : m.functions()) {
        Stmt inlined_s = substitute_in_all_lets(f.body);

        mutate(inlined_s);
    }
}

void DependencyGraph::print_dependencies() {
    cout << endl << endl << "Dependencies: " << endl;
    for (const auto &it : dependencies) {
        cout << it.first << ": " << endl;
        for (const auto &dependency : it.second) {
            cout << "     " << dependency << endl;
        }
        cout << endl;
    }
}

Expr DependencyGraph::visit(const Let *op) {
    string previous_variable = current_variable;

    string unique_var_name = generate_unique_name(op->name);
    current_variable = unique_var_name;
    add_empty_dependency(unique_var_name);
    mutate(op->value);

    current_variable = previous_variable;
    mutate(op->body);

    return op;
}

Expr DependencyGraph::visit(const Variable *op) {
    // auto type = op->type;

    string unique_var_name = get_unique_name(op->name);
    add_dependency(current_variable, unique_var_name);
    return op;
}

Stmt DependencyGraph::visit(const LetStmt *op) {
    string previous_variable = current_variable;

    string unique_var_name = generate_unique_name(op->name);
    current_variable = unique_var_name;
    add_empty_dependency(unique_var_name);
    mutate(op->value);

    current_variable = previous_variable;
    mutate(op->body);

    return op;
}

/*Stmt DependencyGraph::visit(const Store *op) {
    string previous_variable = current_variable;

    current_variable = op->name;
    auto it = dependencies.find(current_variable);
    if (it == dependencies.end()) {
        add_empty_dependency(current_variable);
    }
    mutate(op->value);

    current_variable = previous_variable;

    return op;
}*/
