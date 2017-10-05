#include <algorithm>
#include <set>

#include "RealizationOrder.h"
#include "FindCalls.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::set;
using std::vector;
using std::pair;

void realization_order_dfs(string current,
                           const vector<pair<string, vector<string>>> &graph,
                           set<string> &visited,
                           set<string> &result_set,
                           vector<string> &order) {
    visited.insert(current);

    const auto iter = std::find_if(graph.begin(), graph.end(),
        [&current](const pair<string, vector<string>> &p) { return (p.first == current); });
    internal_assert(iter != graph.end());

    for (const string &fn : iter->second) {
        if (visited.find(fn) == visited.end()) {
            realization_order_dfs(fn, graph, visited, result_set, order);
        } else if (fn != current) { // Self-loops are allowed in update stages
            internal_assert(result_set.find(fn) != result_set.end())
                << "Stuck in a loop computing a realization order. "
                << "Perhaps this pipeline has a loop?\n";
        }
    }

    result_set.insert(current);
    order.push_back(current);
}

vector<string> realization_order(const vector<Function> &outputs,
                                 const map<string, Function> &env) {

    // Make a DAG representing the pipeline. Each function maps to the
    // set describing its inputs.
    vector<pair<string, vector<string>>> graph;

    for (const pair<string, Function> &caller : env) {
        vector<string> s;
        for (const pair<string, Function> &callee : find_direct_calls(caller.second)) {
            if (std::find(s.begin(), s.end(), callee.first) == s.end()) {
                s.push_back(callee.first);
            }
        }
        graph.push_back({caller.first, s});
    }

    vector<string> order;
    set<string> result_set;
    set<string> visited;

    for (Function f : outputs) {
        if (visited.find(f.name()) == visited.end()) {
            realization_order_dfs(f.name(), graph, visited, result_set, order);
        }
    }

    return order;
}

}
}
