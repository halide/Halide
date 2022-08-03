#include "DependencyGraph.h"
#include "Error.h"
#include "Module.h"
#include "Substitute.h"

using namespace std;
using namespace Halide;
using namespace Internal;

string DependencyGraph::generate_dependency_graph(const Module &m) {
    traverse(m);
    generate_html();

    return html.str();
}
string DependencyGraph::generate_dependency_graph(const Stmt &stmt) {
    mutate(stmt);
    generate_html();

    return html.str();
}

void DependencyGraph::generate_html() {
    html.str(string());

    build_graph();
    start_html();
    generate_nodes();
    end_html();
}

void DependencyGraph::start_html() {
    html << "<!DOCTYPE html>";
    html << "<meta charset=\\'utf-8\\'>";
    html << "<head>";
    html << "<script src=\\'https://d3js.org/d3.v4.js\\'></script>";
    html << "<script "
            "src=\\'https://dagrejs.github.io/project/dagre-d3/latest/dagre-d3.min.js\\'></script>";
    html << "</head>";
    html << "<body>";
    html << "<svg id=\\'myGraph\\' width=\\'10000\\'></svg>";
    html << "<script>";
    html << "var g = new dagreD3.graphlib.Graph()";
    html << ".setGraph({})";
    html << ".setDefaultEdgeLabel(function () { return {}; });";
}

void DependencyGraph::end_html() {

    html << "g.nodes().forEach(function (v) {";
    html << "var node = g.node(v);";
    html << "node.rx = node.ry = 5;";
    html << "});";
    html << "var render = new dagreD3.render();";
    html << "var svg = d3.select(\\'#myGraph\\'),";
    html << "svgGroup = svg.append(\\'g\\');";
    html << "render(svgGroup, g);";
    html << "svg.attr(\\'width\\', g.graph().width + 40);";
    html << "var xCenterOffset = (svg.attr(\\'width\\') - g.graph().width) / 2;";
    html << "svgGroup.attr(\\'transform\\', \\'translate(\\' + xCenterOffset + \\', 20)\\');";
    html << "svg.attr(\\'height\\', g.graph().height + 40);";
    html << "</script>";
    html << "<style>";
    html << "g.type-TK>rect {";
    html << "fill: #00ffd0;";
    html << "}";
    html << "text {";
    html << "font-weight: 300;";
    html << "font-family: \\'Helvetica Neue\\', Helvetica, Arial, sans-serif;";
    html << "font-size: 14px;";
    html << "}";
    html << ".node rect {";
    html << "stroke: #999;";
    html << "fill: #fff;";
    html << "stroke-width: 1.5px;";
    html << "}";
    html << ".edgePath path {";
    html << "stroke: #333;";
    html << "stroke-width: 3px;";
    html << "}";
    html << ".edgePath path:hover {";
    html << "stroke: red;";
    html << "stroke-width: 4px;";
    html << "z-index: 9999;";
    html << "}";
    html << "</style>";
    html << "</body>";
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
            setEdges << "g.setEdge(" << get_node(dependency).nodeID << ", " << node.nodeID << ");";
        }
    }

    stringstream setNodes;
    // g.setNode(19, { label: "blur_y.s0.x.x" });
    for (const auto &node : dependency_graph) {
        setNodes << "g.setNode(" << node.nodeID << ", { label: \\'" << node.nodeName << "\\' });";
    }

    html << setNodes.str();
    html << setEdges.str();
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
        internal_error << "\n"
                       << "DependencyGraph::add_dependency: `" << variable
                       << "` not found - should already be in the map"
                       << "\n\n";
    } else {
        it->second.push_back(dependency);
    }
}
void DependencyGraph::add_empty_dependency(const string &variable) {
    auto it = dependencies.find(variable);
    if (it == dependencies.end()) {
        dependencies[variable] = vector<string>{};
    } else {
        internal_error << "\n"
                       << "DependencyGraph::add_empty_dependency: `" << variable
                       << "` already found - should not already be in the map"
                       << "\n\n";
    }
}

vector<string> DependencyGraph::get_dependencies(const string &variable) {
    auto it = dependencies.find(variable);
    if (it == dependencies.end()) {
        internal_error << "\n"
                       << "DependencyGraph::get_dependencies: `" << variable
                       << "` not found - should already be in the map"
                       << "\n\n";
        return vector<string>{};
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

Stmt DependencyGraph::visit(const Store *op) {
    string previous_variable = current_variable;

    current_variable = op->name;
    // TODO: the next couple lines are for duplicate
    //       versions of the variable. is there
    //       anything in particular we should do here?
    auto it = dependencies.find(current_variable);
    if (it == dependencies.end()) {
        add_empty_dependency(current_variable);
    }
    mutate(op->value);

    current_variable = previous_variable;

    return op;
}
