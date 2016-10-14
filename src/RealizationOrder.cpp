#include <algorithm>
#include <set>

#include "RealizationOrder.h"
#include "FindCalls.h"
#include "Func.h"
#include "IRVisitor.h"
#include "IREquality.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::set;
using std::vector;
using std::pair;

namespace {

class CheckLoopCarriedDependence : public IRVisitor {
private:
    const string &func;
    const vector<Expr> &prev_args;

    using IRVisitor::visit;

    void visit(const Call *op) {
        if ((op->call_type == Call::Halide) && (func == op->name)) {
            internal_assert(!op->func.defined())
                << "Func should not have been defined for a self-reference\n";
            internal_assert(prev_args.size() == op->args.size())
                << "Self-reference should have the same number of args as the original\n";
            for (size_t i = 0; i < op->args.size(); i++) {
                if (!equal(op->args[i], prev_args[i])) {
                    debug(4) << "Self-reference of " << op->name << " with different args "
                             << "from the LHS. Encounter loop-carried dependence.\n";
                    result = false;
                    return;
                }
            }
        }
    }

public:
    bool result;

    CheckLoopCarriedDependence(const string &f, const vector<Expr> &prev_args)
        : func(f), prev_args(prev_args), result(true) {}
};

// Check to see if there is a loop-carried dependence in a 'func' update definition
// with the previous definition it's computed with.
bool has_loop_carried_dependence(const string &func, const vector<Expr> &prev_args,
                                 const Definition &update) {
    internal_assert(prev_args.size() == update.args().size());
    for (size_t i = 0; i < prev_args.size(); ++i) {
        if (!equal(prev_args[i], update.args()[i])) {
            return false;
        }
    }

    for (size_t i = 0; i < update.values().size(); ++i) {
        CheckLoopCarriedDependence c(func, prev_args);
        update.values()[i].accept(&c);
        if (c.result) {
            return true;
        }
    }
    return false;
}

void find_fused_groups_dfs(string current,
                           const map<string, set<string>> &fuse_adjacency_list,
                           set<string> &visited,
                           vector<string> &group) {
    visited.insert(current);
    group.push_back(current);

    map<string, set<string>>::const_iterator iter = fuse_adjacency_list.find(current);
    internal_assert(iter != fuse_adjacency_list.end());

    for (const string &fn : iter->second) {
        if (visited.find(fn) == visited.end()) {
            find_fused_groups_dfs(fn, fuse_adjacency_list, visited, group);
        }
    }
}

vector<vector<string>> find_fused_groups(const vector<string> &order,
                                         const map<string, set<string>> &fuse_adjacency_list) {
    set<string> visited;
    vector<vector<string>> result;

    for (const auto &fn : order) {
        if (visited.find(fn) == visited.end()) {
            vector<string> group;
            find_fused_groups_dfs(fn, fuse_adjacency_list, visited, group);
            result.push_back(std::move(group));
        }
    }
    return result;
}

void collect_fused_pairs(const string &fn, size_t stage,
                         const map<string, Function> &env,
                         const map<string, map<string, Function>> &indirect_calls,
                         const vector<FusedPair> &pairs,
                         vector<FusedPair> &func_fused_pairs,
                         vector<pair<string, vector<string>>> &graph,
                         map<string, set<string>> &fuse_adjacency_list) {
    for (const auto &p : pairs) {
        internal_assert((p.func_1 == fn) && (p.stage_1 == stage));

        if (env.find(p.func_2) == env.end()) {
            // Since func_2 is not being called by anyone, might as well skip this fused pair.
            continue;
        }

        // Assert no duplicates (this technically should not have been possible from the front-end).
        {
            const auto iter = std::find(func_fused_pairs.begin(), func_fused_pairs.end(), p);
            internal_assert(iter == func_fused_pairs.end())
                 << "Found duplicates of fused pair (" << p.func_1 << ".s" << p.stage_1 << ", "
                 << p.func_2 << ".s" << p.stage_2 << ", " << p.var_name << ")\n";
        }

        // Assert no dependencies among the functions that are computed_with.
        // Self-dependecy is allowed in if and only if there is no loop-carried
        // dependence, e.g. f(x, y) = f(x, y) + 2 is okay but
        // f(x, y) = f(x, y - 1) + 2 is not.
        if (p.func_1 != p.func_2) {
            const auto &callees_1 = indirect_calls.find(p.func_1);
            if (callees_1 != indirect_calls.end()) {
                user_assert(callees_1->second.find(p.func_2) == callees_1->second.end())
                    << "Invalid compute_with: there is dependency between "
                    << p.func_1 << " and " << p.func_2 << "\n";
            }
            const auto &callees_2 = indirect_calls.find(p.func_2);
            if (callees_2 != indirect_calls.end()) {
                user_assert(callees_2->second.find(p.func_1) == callees_2->second.end())
                    << "Invalid compute_with: there is dependency between "
                    << p.func_1 << " and " << p.func_2 << "\n";
            }
        } else {
            internal_assert(p.stage_2 > 0) << "Should have been an update definition\n";
            const Function &func = env.find(p.func_2)->second;
            const vector<Expr> prev_args = (p.stage_1 == 0) ? func.definition().args() : func.update(p.stage_1-1).args();
            const Definition &update = func.update(p.stage_2-1);
            bool loop_carried = has_loop_carried_dependence(p.func_1, prev_args, update);
            user_assert(loop_carried) << "Invalid compute_with: there is loop-carried "
                << "dependence between " << p.func_1 << ".s" << p.stage_1 << " and "
                << p.func_2 << ".s" << p.stage_2 << "\n";
        }

        fuse_adjacency_list[p.func_1].insert(p.func_2);
        fuse_adjacency_list[p.func_2].insert(p.func_1);

        func_fused_pairs.push_back(p);

        auto iter = std::find_if(graph.begin(), graph.end(),
            [&p](const pair<string, vector<string>> &s) { return (s.first == p.func_1); });
        internal_assert(iter != graph.end());
        iter->second.push_back(p.func_2);
    }
}

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

void populate_fused_pairs_list(const string &func, const Definition &def,
                               int stage, map<string, Function> &env) {
    const LoopLevel &fuse_level = def.schedule().fuse_level().level;
    if (fuse_level.is_inline() || fuse_level.is_root()) {
        // It isn't fused to anyone
        return;
    }

    auto iter = env.find(fuse_level.func());
    if (iter == env.end()) {
        // The 'parent' this function is fused with is not used anywhere; hence,
        // it is not in 'env'
        return;
    }

    Function &parent = iter->second;
    FusedPair pair(fuse_level.func(), fuse_level.stage(),
                   func, stage, fuse_level.var().name());
    if (fuse_level.stage() == 0) {
        parent.definition().schedule().fused_pairs().push_back(pair);
    } else {
        parent.update(fuse_level.stage()-1).schedule().fused_pairs().push_back(pair);
    }
}

} // anonymous namespace

pair<vector<string>, vector<vector<string>>> realization_order(
        const vector<Function> &outputs, map<string, Function> &env) {

    // Populate the fused_pairs of each function definition (i.e. list of all
    // function definitions that are to be computed with that function)
    for (auto &iter : env) {
        populate_fused_pairs_list(iter.first, iter.second.definition(), 0, env);
        for (size_t i = 0; i < iter.second.updates().size(); ++i) {
            populate_fused_pairs_list(iter.first, iter.second.updates()[i], i + 1, env);
        }
    }

    // Collect all indirect calls made by all the functions in "env".
    map<string, map<string, Function>> indirect_calls;
    for (const pair<string, Function> &caller : env) {
        map<string, Function> more_funcs = find_transitive_calls(caller.second);
        indirect_calls.emplace(caller.first, std::move(more_funcs));
    }

    // Make a DAG representing the pipeline. Each function maps to the
    // set describing its inputs.
    vector<pair<string, vector<string>>> graph;

    // Make a directed and non-directed graph representing the compute_with
    // dependencies between functions. Each function maps to the list of Functions
    // computed_with it.
    map<string, vector<FusedPair>> fused_pairs_graph;
    map<string, set<string>> fuse_adjacency_list;

    for (const pair<string, Function> &caller : env) {
        vector<string> s;
        for (const pair<string, Function> &callee : find_direct_calls(caller.second)) {
            if (std::find(s.begin(), s.end(), callee.first) == s.end()) {
                s.push_back(callee.first);
            }
        }
        graph.push_back({caller.first, s});

        // Find all compute_with (fused) pairs. We have to look at the update
        // definitions as well since compute_with is defined per definition (stage).
        vector<FusedPair> &func_fused_pairs = fused_pairs_graph[caller.first];
        fuse_adjacency_list[caller.first]; // Make sure every Func in 'env' is allocated a slot
        collect_fused_pairs(caller.first, 0, env, indirect_calls,
                            caller.second.definition().schedule().fused_pairs(),
                            func_fused_pairs, graph, fuse_adjacency_list);

        for (size_t i = 0; i < caller.second.updates().size(); ++i) {
            const Definition &def = caller.second.updates()[i];
            collect_fused_pairs(caller.first, i + 1, env, indirect_calls,
                                def.schedule().fused_pairs(), func_fused_pairs,
                                graph, fuse_adjacency_list);
        }
    }

    // Make sure we don't have cyclic compute_with: if Func f is computed after
    // Func g, Func g should not be computed after Func f.
    for (const auto &iter : fused_pairs_graph) {
        for (const auto &pair : iter.second) {
            if (pair.func_1 == pair.func_2) {
                // compute_with among stages of a function is okay,
                // e.g. f.update(0).compute_with(f, x)
                continue;
            }
            const auto o_iter = fused_pairs_graph.find(pair.func_2);
            if (o_iter == fused_pairs_graph.end()) {
                continue;
            }
            const auto it = std::find_if(o_iter->second.begin(), o_iter->second.end(),
                [&pair](const FusedPair& other) { return (pair.func_1 == other.func_2) && (pair.func_2 == other.func_1); });
            user_assert(it == o_iter->second.end())
                << "Found cyclic dependencies between compute_with of "
                << pair.func_1 << " and " << pair.func_2 << "\n";
        }
    }

    // Compute the realization order.
    vector<string> order;
    set<string> result_set;
    set<string> visited;

    for (Function f : outputs) {
        if (visited.find(f.name()) == visited.end()) {
            realization_order_dfs(f.name(), graph, visited, result_set, order);
        }
    }

    // Determine group of functions which loops are to be fused together.
    vector<vector<string>> fused_groups = find_fused_groups(order, fuse_adjacency_list);

    // Sort the functions within a fused group based on the realization order
    for (auto &group : fused_groups) {
        std::sort(group.begin(), group.end(),
            [&](const string &lhs, const string &rhs){
                const auto iter_lhs = std::find(order.begin(), order.end(), lhs);
                const auto iter_rhs = std::find(order.begin(), order.end(), rhs);
                return iter_lhs < iter_rhs;
            }
        );
    }

    return std::make_pair(order, fused_groups);
}

}
}
