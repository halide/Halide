#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "FindCalls.h"
#include "Func.h"
#include "IREquality.h"
#include "IRVisitor.h"
#include "RealizationOrder.h"

using std::map;
using std::unordered_map;
using std::unordered_set;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace std {

template<>
struct hash<Halide::Internal::FusedStage> {
    std::size_t operator()(const Halide::Internal::FusedStage &k) const {
        return hash<Halide::Internal::FusedStageContents *>()(k.contents.get());
    }
};

template<>
struct hash<Halide::Internal::FusedGroup> {
    std::size_t operator()(const Halide::Internal::FusedGroup &k) const {
        return hash<Halide::Internal::FusedGroupContents *>()(k.contents.get());
    }
};

}

namespace {

template<class T>
class UnionFind {
    static_assert(std::is_nothrow_move_constructible<T>::value, "T should be noexcept MoveConstructible");

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
    unordered_map<T, unordered_set<T>> edges{};
    unordered_map<T, int> in_degree{};
    unordered_map<T, int> out_degree{};

    void dfs_sort(vector<T> &order, unordered_set<T> &settled, unordered_set<T> &on_path, T node) {
        if (settled.count(node)) {
            return;
        }
        user_assert(!on_path.count(node)) << "Detected cycle in DAG.\n";
        on_path.insert(node);
        for (const auto &neighbor : edges[node]) {
            dfs_sort(order, settled, on_path, neighbor);
        }
        on_path.erase(node);
        settled.insert(node);
        order.push_back(node);
    }

public:
    DAG() = default;

    void add_vertex(const T &vertex) {
        if (!edges.count(vertex)) {
            edges[vertex];
            in_degree[vertex] = 0;
            out_degree[vertex] = 0;
        }
    }

    void add_edge(const T &src, const T &dst) {
        add_vertex(src);
        add_vertex(dst);

        const auto did_insert = edges[src].insert(dst).second;
        if (did_insert) {
            out_degree[src]++;
            in_degree[dst]++;
        }
    }

    const unordered_set<T> &get_neighbors(const T &v) {
        return edges.at(v);
    }

    vector<T> topological_sort() {
        vector<T> start_points;
        for (const auto &it : in_degree) {
            if (it.second == 0) {
                start_points.push_back(it.first);
            }
        }
        return topological_sort(start_points);
    }

    vector<T> topological_sort(const vector<T> &start_points) {
        vector<T> order;
        unordered_set<T> settled;
        unordered_set<T> on_path;
        for (const auto &node : start_points) {
            if (!settled.count(node)) {
                dfs_sort(order, settled, on_path, node);
            }
        }
        return order;
    }

    vector<T> vertex_set() const {
        vector<T> vs;
        for (const auto &it : edges) {
            vs.push_back(it.first);
        }
        return vs;
    }
};

template<class T, class U>
class LabeledTree {
};

}

namespace Halide {
namespace Internal {


struct FusedStageContents {
    mutable RefCount ref_count;

    LabeledTree<Stage, Var> tree;
};

template<>
RefCount &ref_count<FusedStageContents>(const FusedStageContents *p) {
    return p->ref_count;
}

template<>
void destroy<FusedStageContents>(const FusedStageContents *p) {
    delete p;
}

FusedStage::FusedStage() : contents(new FusedStageContents) {}

FusedStage::FusedStage(const FusedStage &other) = default;

FusedStage::FusedStage(FusedStage &&other) noexcept = default;

bool FusedStage::operator==(const FusedStage &other) const {
    return contents.same_as(other.contents);
}

struct FusedGroupContents {
    mutable RefCount ref_count;

    DAG<FusedStage> group;
    vector<Function> funcs;
};

template<>
RefCount &ref_count<FusedGroupContents>(const FusedGroupContents *p) {
    return p->ref_count;
}

template<>
void destroy<FusedGroupContents>(const FusedGroupContents *p) {
    delete p;
}

FusedGroup::FusedGroup() : contents(new FusedGroupContents) {}

bool FusedGroup::operator==(const FusedGroup &other) const {
    return contents.same_as(other.contents);
}

void FusedGroup::add_stage(const FusedStage &stage) {

}

void FusedGroup::add_function(const Function &function) {
    contents->funcs.push_back(function);
}

const std::vector<Function> &FusedGroup::functions() const {
    return contents->funcs;
}

std::string FusedGroup::repr() const {
    string rep = "[";
    string sep;
    for (const auto &fn : contents->funcs) {
        rep += sep;
        rep += fn.name();
        sep = "|";
    }
    rep += "]";
    return rep;
}

struct PipelineGraphContents {
    mutable RefCount ref_count;

    DAG<FusedGroup> graph;
};


template<>
RefCount &ref_count<PipelineGraphContents>(const PipelineGraphContents *p) {
    return p->ref_count;
}

template<>
void destroy<PipelineGraphContents>(const PipelineGraphContents *p) {
    delete p;
}

PipelineGraph::PipelineGraph() : contents(new PipelineGraphContents) {}

bool PipelineGraph::operator==(const PipelineGraph &other) {
    return contents.same_as(other.contents);
}

vector<FusedGroup> PipelineGraph::fused_groups() const {
    return contents->graph.vertex_set();
}

void PipelineGraph::add_edge(const FusedGroup &src, const FusedGroup &dst) {
    contents->graph.add_edge(src, dst);
}

void PipelineGraph::set_outputs(const std::vector<Function> &vector) {

}

void PipelineGraph::debug_dump() const {
    debug(0) << "-----------\n";
    for (const auto &fg : contents->graph.vertex_set()) {
        debug(0) << fg.repr() << " --> {";
        for (const auto &dep : contents->graph.get_neighbors(fg)) {
            debug(0) << " " << dep.repr();
        }
        debug(0) << " }\n";
    }
    debug(0) << "---\n";
    for (const auto &fg : contents->graph.topological_sort()) {
        debug(0) << fg.repr() << " ";
    }
    debug(0) << "\n";
}

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

PipelineGraph create_pipeline_graph(const vector<Function> &outputs, map<string, Function> &env) {
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

    // Create the pipeline graph
    PipelineGraph graph;

    // Collect fused groups into objects
    unordered_map<string, FusedGroup> rep_to_fg;
    for (auto &it : env) {
        const auto &func = it.second;
        const string &group = fused_groups_set.find(func.name());
        rep_to_fg[group].add_function(func);
    }

    // For each fused group
    for (const auto &it : rep_to_fg) {
        const auto &src = it.second;

        // TODO: Fill in the guts of the fused group

        // Draw edges to its dependencies
        for (const auto &fn : src.functions()) {
            const map<string, Function> &calls = find_direct_calls(fn);
            for (const auto &call_it : calls) {
                const auto &dst_fn = call_it.second;
                if (fn.name() == dst_fn.name()) {
                    continue;
                }
                const auto &dst = rep_to_fg[fused_groups_set.find(dst_fn.name())];
                graph.add_edge(src, dst);
            }
        }
    }

    // Drop vertices and edges that are unreachable from any output
    graph.set_outputs(outputs);

    return graph;
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
