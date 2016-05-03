#include "WrapCalls.h"
#include "IRVisitor.h"

#include <set>

namespace Halide{
namespace Internal {

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

void wrap_func_calls_helper(map<string, Function> &env, const string &in_func,
                            const Function &orig, const Function &substitute) {
    env[in_func] = env[in_func].substitute_calls(orig, substitute);
}

// Return true if 'func' exists as copy of one of the Function in 'copied_map'
bool is_copy(const Function& func, const map<Function, Function> copied_map) {
    for (const auto &iter : copied_map) {
        if (iter.second.same_as(func)) {
            return true;
        }
    }
    return false;
}

pair<vector<Function>, map<string, Function>> wrap_func_calls(
            const vector<Function> &outputs, const map<string, Function> &env) {
    vector<Function> wrapped_outputs;
    map<string, Function> wrapped_env;

    // Create empty copy of Func in env.
    map<Function, Function> copied_map;
    for (const auto &iter : env) {
        copied_map[iter.second] = Function(iter.second.name());
    }

    // Deep copy the Func in env into its corresponding empty copy.
    for (const auto &iter : env) {
        iter.second.deep_copy(copied_map[iter.second], copied_map);
    }
    // Need to substitute-in the old reference within the Func with the
    // deep-copy version.
    for (auto &iter : copied_map) {
        iter.second.substitute_calls(copied_map);
    }

    // Populate the env with the deep-copy version.
    for (const auto &iter : copied_map) {
        wrapped_env.emplace(iter.first.name(), iter.second);
    }

    for (const auto &it : copied_map) {
        string wrapped_fname = it.first.name();
        const Function &wrapped_func = it.second;
        const auto &wrappers = wrapped_func.schedule().wrappers();

        // Put names of all wrappers of this Function into the set for
        // faster comparison during the substitution.
        set<string> all_func_wrappers;
        for (const auto &iter : wrappers) {
            all_func_wrappers.insert(Function(iter.second).name());
        }

        for (const auto &iter : wrappers) {
            string in_func = iter.first;
            const Function &wrapper = Function(iter.second); // This is already the deep-copy version
            internal_assert(is_copy(wrapper, copied_map));   // Make sure it's indeed the copy

            if (in_func.empty()) { // Global wrapper
                for (const auto &wrapped_env_iter : wrapped_env) {
                    in_func = wrapped_env_iter.first;
                    if ((wrapper.name() == in_func) || (all_func_wrappers.find(in_func) != all_func_wrappers.end())) {
                        // Should not substitute itself or custom wrapper of the same func
                        debug(4) << "Skip over replacing \"" << in_func << "\" with \"" << wrapper.name() << "\"\n";
                        continue;
                    }
                    if (wrappers.count(in_func)) {
                        // If the 'in_func' already has custom wrapper for 'wrapped_func',
                        // don't substitute in the global wrapper.
                        continue;
                    }
                    debug(4) << "Global wrapper: replacing reference of \""
                             << wrapped_fname <<  "\" in \"" << in_func
                             << "\" with \"" << wrapper.name() << "\"\n";
                    wrap_func_calls_helper(wrapped_env, in_func, wrapped_func, wrapper);
                }
            } else {
                debug(4) << "Custom wrapper: replacing reference of \""
                         << wrapped_fname <<  "\" in \"" << in_func << "\" with \""
                         << wrapper.name() << "\"\n";
                wrap_func_calls_helper(wrapped_env, in_func, wrapped_func, wrapper);
            }
        }
    }

    for (const auto &func : outputs) {
        if (copied_map.count(func)) {
            debug(4) << "Adding deep-copied version to outputs: " << func.name() << "\n";
            wrapped_outputs.push_back(copied_map[func]);
        } else {
            debug(4) << "Adding original version to outputs: " << func.name() << "\n";
            wrapped_outputs.push_back(func);
        }
    }

    return std::make_pair(wrapped_outputs, wrapped_env);
}

}
}
