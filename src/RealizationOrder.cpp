#include <algorithm>
#include <set>
#include <unordered_map>

#include "FindCalls.h"
#include "Func.h"
#include "IREquality.h"
#include "IRVisitor.h"
#include "RealizationOrder.h"

namespace Halide {
namespace Internal {

using std::map;
using std::unordered_map;
using std::pair;
using std::set;
using std::string;
using std::vector;

template<class T>
class UnionFind {
    unordered_map<T, T> parents;
    unordered_map<T, int> sizes;

public:
    UnionFind() = default;

    const T &find(const T &element) {
        auto it = parents.find(element);

        if (it == parents.end()) {
            // add if not in maps
            parents[element] = element;
            sizes[element] = 1;
            return element;
        }

        while (it->first != it->second) {
            // path halving
            const auto grandparent = parents[it->second];
            it->second = grandparent;
            it = parents.find(grandparent);
        }

        return it->second;
    }

    void join(const T &x, const T &y) {
        const T &x_root = find(x);
        const T &y_root = find(y);

        if (x_root == y_root) {
            return;
        }

        // union by size
        int x_size = sizes[x_root];
        int y_size = sizes[y_root];

        // smaller tree gets fused into the larger tree
        const T &smaller = x_size < y_size ? x_root : y_root;
        const T &larger = x_size < y_size ? y_root : x_root;

        parents[smaller] = larger;
        sizes[larger] = x_size + y_size;
        sizes.erase(smaller);
    }
};

template<class T>
class DAG {
    unordered_map<T, vector<const T &>> edges;

public:
    DAG() = default;

    void add_vertex(const T &vertex) {
        edges[vertex]; // ensure vertex exists in map.
    }

    void add_edge(const T &src, const T &dst) {
        edges[src].push_back(dst);
    }
};


template<class T, class U>
class LabeledTree {

public:
    bool operator==(const LabeledTree<T, U> &other) {
        return true;
    }
};

class FusedStage {
    LabeledTree<Stage, Var> tree;

public:
    bool operator==(const FusedStage &other) {
        return tree == other.tree;
    }
};

class FusedGroup {
//    DAG<FusedStage> group;
    vector<Function> members;

public:
    void add_stage(const FusedStage &stage) {

    }

    void add_function(const Function &function) {
        members.push_back(function);
    }

    bool operator==(const FusedGroup &other) {
        if (members.size() != other.members.size()) {
            return false;
        }
        for (size_t i = 0; i < members.size(); ++i) {
            if (!members[i].same_as(other.members[i])) {
                return false;
            }
        }
        return true;
    }
};

class PipelineGraph {
//    DAG<FusedGroup> graph;

public:
    PipelineGraph() = default;

    void add_fused_group(const FusedGroup &group) {
//        graph.add_vertex(group);
    }
};

void find_fused_groups_dfs(const string &current,
                           const map<string, set<string>> &fuse_adjacency_list,
                           set<string> &visited,
                           vector<string> &group) {
    visited.insert(current);
    group.push_back(current);

    auto iter = fuse_adjacency_list.find(current);
    internal_assert(iter != fuse_adjacency_list.end());

    for (const string &fn : iter->second) {
        if (visited.find(fn) == visited.end()) {
            find_fused_groups_dfs(fn, fuse_adjacency_list, visited, group);
        }
    }
}

pair<map<string, vector<string>>, map<string, string>>
find_fused_groups(const map<string, Function> &env,
                  const map<string, set<string>> &fuse_adjacency_list) {
    set<string> visited;
    map<string, vector<string>> fused_groups;
    map<string, string> group_name;

    for (const auto &iter : env) {
        const string &fn = iter.first;
        if (visited.find(fn) == visited.end()) {
            vector<string> group;
            find_fused_groups_dfs(fn, fuse_adjacency_list, visited, group);

            // Create a unique name for the fused group.
            string rename = unique_name("_fg");
            fused_groups.emplace(rename, group);
            for (const auto &m : group) {
                group_name.emplace(m, rename);
            }
        }
    }
    return {fused_groups, group_name};
}

void realization_order_dfs(const string &current,
                           const map<string, vector<string>> &graph,
                           set<string> &visited,
                           set<string> &result_set,
                           vector<string> &order) {
    visited.insert(current);

    const auto &iter = graph.find(current);
    internal_assert(iter != graph.end());

    for (const string &fn : iter->second) {
        internal_assert(fn != current);
        if (visited.find(fn) == visited.end()) {
            realization_order_dfs(fn, graph, visited, result_set, order);
        } else {
            internal_assert(result_set.find(fn) != result_set.end())
            << "Stuck in a loop computing a realization order. "
            << "Perhaps this pipeline has a loop involving " << current << "?\n";
        }
    }

    result_set.insert(current);
    order.push_back(current);
}

// Check the validity of a pair of fused stages.
void validate_fused_pair(const string &fn, size_t stage_index,
                         const map<string, Function> &env,
                         const map<string, map<string, Function>> &indirect_calls,
                         const FusedPair &p,
                         const vector<FusedPair> &func_fused_pairs) {
    internal_assert((p.parent_func == fn) && (p.parent_stage == stage_index));

    user_assert(env.count(p.child_func))
    << "Illegal compute_with: \"" << p.child_func << "\" is scheduled to be computed with \""
    << p.parent_func << "\" but \"" << p.child_func << "\" is not used anywhere.\n";

    // Assert no compute_with of updates of the same Func and no duplicates
    // (These technically should not have been possible from the front-end).
    {
        internal_assert(p.parent_func != p.child_func);
        const auto &iter = std::find(func_fused_pairs.begin(), func_fused_pairs.end(), p);
        internal_assert(iter == func_fused_pairs.end())
        << "Found duplicates of fused pair (" << p.parent_func << ".s" << p.parent_stage << ", "
        << p.child_func << ".s" << p.child_stage << ", " << p.var_name << ")\n";
    }

    // Assert no dependencies among the functions that are computed_with.
    const auto &callees_1 = indirect_calls.find(p.parent_func);
    if (callees_1 != indirect_calls.end()) {
        user_assert(callees_1->second.find(p.child_func) == callees_1->second.end())
        << "Invalid compute_with: there is dependency between "
        << p.parent_func << " and " << p.child_func << "\n";
    }
    const auto &callees_2 = indirect_calls.find(p.child_func);
    if (callees_2 != indirect_calls.end()) {
        user_assert(callees_2->second.find(p.parent_func) == callees_2->second.end())
        << "Invalid compute_with: there is dependency between "
        << p.parent_func << " and " << p.child_func << "\n";
    }
}

// Populate 'func_fused_pairs' and 'fuse_adjacency_list': a directed and
// non-directed graph representing the compute_with dependencies between
// functions.
void collect_fused_pairs(const FusedPair &p,
                         vector<FusedPair> &func_fused_pairs,
                         map<string, vector<string>> &graph,
                         map<string, set<string>> &fuse_adjacency_list) {
    fuse_adjacency_list[p.parent_func].insert(p.child_func);
    fuse_adjacency_list[p.child_func].insert(p.parent_func);

    func_fused_pairs.push_back(p);

    // If there is a compute_with dependency between two functions, we need
    // to update the pipeline DAG so that the computed realization order
    // respects this dependency.
    graph[p.parent_func].push_back(p.child_func);
}

// Populate the 'fused_pairs' list in Schedule of each function stage.
void populate_fused_pairs_list(const string &func, const Definition &def,
                               size_t stage_index, map<string, Function> &env) {
    internal_assert(def.defined());
    const LoopLevel &fuse_level = def.schedule().fuse_level().level;
    if (fuse_level.is_inlined() || fuse_level.is_root()) {
        // 'func' is not fused with anyone.
        return;
    }

    auto iter = env.find(fuse_level.func());
    user_assert(iter != env.end())
    << "Illegal compute_with: \"" << func << "\" is scheduled to be computed with \""
    << fuse_level.func() << "\" which is not used anywhere.\n";

    Function &parent = iter->second;
    user_assert(!parent.has_extern_definition())
    << "Illegal compute_with: Func \"" << func << "\" is scheduled to be "
    << "computed with extern Func \"" << parent.name() << "\"\n";

    FusedPair pair(fuse_level.func(), fuse_level.stage_index(),
                   func, stage_index, fuse_level.var().name());
    if (fuse_level.stage_index() == 0) {
        parent.definition().schedule().fused_pairs().push_back(pair);
    } else {
        internal_assert(fuse_level.stage_index() > 0);
        parent.update(fuse_level.stage_index() - 1).schedule().fused_pairs().push_back(pair);
    }
}

// Make sure we don't have cyclic compute_with: if Func 'f' is computed after
// Func 'g', Func 'g' should not be computed after Func 'f'.
void check_no_cyclic_compute_with(const map<string, vector<FusedPair>> &fused_pairs_graph) {
    for (const auto &iter : fused_pairs_graph) {
        for (const auto &pair : iter.second) {
            internal_assert(pair.parent_func != pair.child_func);
            const auto &o_iter = fused_pairs_graph.find(pair.child_func);
            if (o_iter == fused_pairs_graph.end()) {
                continue;
            }
            const auto &it = std::find_if(o_iter->second.begin(), o_iter->second.end(),
                                          [&pair](const FusedPair &other) {
                                              return (pair.parent_func == other.child_func) &&
                                                     (pair.child_func == other.parent_func);
                                          });
            user_assert(it == o_iter->second.end())
            << "Found cyclic dependencies between compute_with of "
            << pair.parent_func << " and " << pair.child_func << "\n";
        }
    }
}

void fuse_funcs(UnionFind<string> &fused_groups_set,
                const Function &function,
                const LoopLevel &fuse_level,
                map<string, Function> &env) {
    if (fuse_level.is_inlined() || fuse_level.is_root()) {
        return;
    }

    const auto iter = env.find(fuse_level.func());
    user_assert(iter != env.end())
    << "Illegal compute_with: \"" << function.name() << "\" is scheduled to be computed with \""
    << fuse_level.func() << "\" which is not used anywhere.\n";

    const Function &parent = iter->second;
    user_assert(!parent.has_extern_definition())
    << "Illegal compute_with: Func \"" << function.name() << "\" is scheduled to be "
    << "computed with extern Func \"" << parent.name() << "\"\n";

    fused_groups_set.join(function.name(), parent.name());
}

void dump_pipeline_graph(const vector<Function> &outputs, map<string, Function> &env) {
    UnionFind<string> fused_groups_set;

    // Identify fused groups
    for (auto &it : env) {
        const auto &func = it.second;

        const auto &init_fuse = func.definition().schedule().fuse_level().level;
        fuse_funcs(fused_groups_set, func, init_fuse, env);

        for (const auto &update : func.updates()) {
            const auto &update_fuse = update.schedule().fuse_level().level;
            fuse_funcs(fused_groups_set, func, update_fuse, env);
        }
    }

    PipelineGraph graph{};
    map<string, FusedGroup> representatives;

    // Collect fused groups into objects
    for (auto &it : env) {
        const auto &func = it.second;
        const string &group = fused_groups_set.find(func.name());
        representatives[group].add_function(func);
    }

    //
    for (auto &it : representatives) {
        graph.add_fused_group(it.second);
    }


}

pair<vector<string>, vector<vector<string>>>
realization_order(const vector<Function> &outputs, map<string, Function> &env) {

    // Populate the fused_pairs list of each function definition (i.e. list of
    // all function definitions that are to be computed with that function).
    for (auto &iter : env) {
        if (iter.second.has_extern_definition()) {
            // Extern function should not be fused.
            continue;
        }
        populate_fused_pairs_list(iter.first, iter.second.definition(), 0, env);
        for (size_t i = 0; i < iter.second.updates().size(); ++i) {
            populate_fused_pairs_list(iter.first, iter.second.updates()[i], i + 1, env);
        }
    }

    // Collect all indirect calls made by all the functions in "env".
    map<string, map<string, Function>> indirect_calls;
    for (const pair<const string, Function> &caller : env) {
        map<string, Function> more_funcs = find_transitive_calls(caller.second);
        indirect_calls.emplace(caller.first, more_funcs);
    }

    // 'graph' is a DAG representing the pipeline. Each function maps to the
    // set describing its inputs.
    map<string, vector<string>> graph;

    // Make a directed and non-directed graph representing the compute_with
    // dependencies between functions. Each function maps to the list of
    // functions computed_with it.
    map<string, vector<FusedPair>> fused_pairs_graph;
    map<string, set<string>> fuse_adjacency_list;

    for (const pair<const string, Function> &func : env) {
        // Find all compute_with (fused) pairs. We have to look at the update
        // definitions as well since compute_with is defined per definition (stage).
        vector<FusedPair> &func_fused_pairs = fused_pairs_graph[func.first];
        fuse_adjacency_list[func.first]; // Make sure every Func in 'env' is allocated a slot
        if (!func.second.has_extern_definition()) {
            for (auto &p : func.second.definition().schedule().fused_pairs()) {
                validate_fused_pair(func.first, 0, env, indirect_calls,
                                    p, func_fused_pairs);
                collect_fused_pairs(p, func_fused_pairs, graph, fuse_adjacency_list);
            }
            for (size_t i = 0; i < func.second.updates().size(); ++i) {
                for (auto &p : func.second.updates()[i].schedule().fused_pairs()) {
                    validate_fused_pair(func.first, i + 1, env, indirect_calls,
                                        p, func_fused_pairs);
                    collect_fused_pairs(p, func_fused_pairs, graph, fuse_adjacency_list);
                }
            }
        }
    }

    check_no_cyclic_compute_with(fused_pairs_graph);

    // Determine groups of functions which loops are to be fused together.
    // 'fused_groups' maps a fused group to its members.
    // 'group_name' maps a function to the name of the fused group it belongs to.
    map<string, vector<string>> fused_groups;
    map<string, string> group_name;
    std::tie(fused_groups, group_name) = find_fused_groups(env, fuse_adjacency_list);

    // Compute the DAG representing the pipeline
    for (const pair<const string, Function> &caller : env) {
        const string &caller_rename = group_name.at(caller.first);
        // Create a dummy node representing the fused group and add input edge
        // dependencies from the nodes representing member of the fused group
        // to this dummy node.
        graph[caller.first].push_back(caller_rename);
        // Direct the calls to calls from the dummy node. This forces all the
        // functions called by members of the fused group to be realized first.
        vector<string> &s = graph[caller_rename];
        for (const pair<const string, Function> &callee : find_direct_calls(caller.second)) {
            if ((callee.first != caller.first) && // Skip calls to itself (i.e. update stages)
                (std::find(s.begin(), s.end(), callee.first) == s.end())) {
                s.push_back(callee.first);
            }
        }
    }

    // Compute the realization order of the fused groups (i.e. the dummy nodes)
    // and also the realization order of the functions within a fused group.
    vector<string> temp;
    set<string> result_set;
    set<string> visited;
    for (const Function &f : outputs) {
        if (visited.find(f.name()) == visited.end()) {
            realization_order_dfs(f.name(), graph, visited, result_set, temp);
        }
    }

    // Collect the realization order of the fused groups.
    vector<vector<string>> group_order;
    for (const auto &fn : temp) {
        const auto &iter = fused_groups.find(fn);
        if (iter != fused_groups.end()) {
            group_order.push_back(iter->second);
        }
    }

    // Sort the functions within a fused group based on the compute_with
    // dependencies (i.e. parent of the fused loop should be realized after its
    // children).
    for (auto &group : group_order) {
        std::sort(group.begin(), group.end(),
                  [&](const string &lhs, const string &rhs) {
                      const auto &iter_lhs = std::find(temp.begin(), temp.end(), lhs);
                      const auto &iter_rhs = std::find(temp.begin(), temp.end(), rhs);
                      return iter_lhs < iter_rhs;
                  }
        );
    }

    // Collect the realization order of all functions within the pipeline.
    vector<string> order;
    for (const auto &group : group_order) {
        for (const auto &f : group) {
            order.push_back(f);
        }
    }

    return {order, group_order};
}

vector<string> topological_order(const vector<Function> &outputs,
                                 const map<string, Function> &env) {

    // Make a DAG representing the pipeline. Each function maps to the
    // set describing its inputs.
    map<string, vector<string>> graph;

    for (const pair<const string, Function> &caller : env) {
        vector<string> s;
        for (const pair<const string, Function> &callee : find_direct_calls(caller.second)) {
            if ((callee.first != caller.first) && // Skip calls to itself (i.e. update stages)
                (std::find(s.begin(), s.end(), callee.first) == s.end())) {
                s.push_back(callee.first);
            }
        }
        graph.emplace(caller.first, s);
    }

    vector<string> order;
    set<string> result_set;
    set<string> visited;
    for (const Function &f : outputs) {
        if (visited.find(f.name()) == visited.end()) {
            realization_order_dfs(f.name(), graph, visited, result_set, order);
        }
    }

    return order;
}

}  // namespace Internal
}  // namespace Halide
