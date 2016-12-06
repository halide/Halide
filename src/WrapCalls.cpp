#include "WrapCalls.h"

#include <set>

namespace Halide{
namespace Internal {

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

typedef map<Function, Function, Function::Compare> SubstitutionMap;

namespace {

void insert_func_wrapper_helper(map<Function, SubstitutionMap, Function::Compare> &func_wrappers_map,
                                const Function &in_func, const Function &wrapped_func,
                                const Function &wrapper) {
    internal_assert(in_func.get_contents().defined() && wrapped_func.get_contents().defined() &&
                    wrapper.get_contents().defined());
    internal_assert(func_wrappers_map[in_func].count(wrapped_func) == 0)
        << "Should only have one wrapper for each function call in a Func\n";

    SubstitutionMap &wrappers_map = func_wrappers_map[in_func];
    for (auto iter = wrappers_map.begin(); iter != wrappers_map.end(); ++iter) {
        if (iter->second.same_as(wrapped_func)) {
            debug(4) << "Merging wrapper of " << in_func.name() << " [" << iter->first.name()
                     << ", " << iter->second.name() << "] with [" << wrapped_func.name() << ", "
                     << wrapper.name() << "]\n";
            iter->second = wrapper;
            return;
        } else if (wrapper.same_as(iter->first)) {
            debug(4) << "Merging wrapper of " << in_func.name() << " [" << wrapped_func.name()
                     << ", " << wrapper.name() << "] with [" << iter->first.name() << ", "
                     << iter->second.name() << "]\n";
            wrappers_map.emplace(wrapped_func, iter->second);
            wrappers_map.erase(iter);
            return;
        }
    }
    wrappers_map[wrapped_func] = wrapper;
}

} // anonymous namespace

map<string, Function> wrap_func_calls(const map<string, Function> &env) {
    map<string, Function> wrapped_env;

    map<Function, SubstitutionMap, Function::Compare> func_wrappers_map; // In Func -> [wrapped Func -> wrapper]

    for (const auto &iter : env) {
        wrapped_env.emplace(iter.first, iter.second);
        func_wrappers_map[iter.second];
    }

    for (const auto &it : env) {
        string wrapped_fname = it.first;
        const Function &wrapped_func = it.second;
        const auto &wrappers = wrapped_func.schedule().wrappers();

        // Put the names of all wrappers of this Function into the set for
        // faster comparison during the substitution.
        set<string> all_func_wrappers;
        for (const auto &iter : wrappers) {
            all_func_wrappers.insert(Function(iter.second).name());
        }

        for (const auto &iter : wrappers) {
            string in_func = iter.first;
            const Function &wrapper = Function(iter.second); // This is already the deep-copy version

            if (in_func.empty()) { // Global wrapper
                for (const auto &wrapped_env_iter : wrapped_env) {
                    in_func = wrapped_env_iter.first;
                    if ((wrapper.name() == in_func) || (all_func_wrappers.find(in_func) != all_func_wrappers.end())) {
                        // The wrapper should still call the original function,
                        // so we don't want to rewrite the calls done by the
                        // wrapper. We also shouldn't rewrite the original
                        // function itself.
                        debug(4) << "Skip over replacing \"" << in_func << "\" with \"" << wrapper.name() << "\"\n";
                        continue;
                    }
                    if (wrappers.count(in_func)) {
                        // If the 'in_func' already has custom wrapper for
                        // 'wrapped_func', don't substitute in the global wrapper.
                        // Custom wrapper always takes precedence over global wrapper
                        continue;
                    }
                    debug(4) << "Global wrapper: replacing reference of \""
                             << wrapped_fname <<  "\" in \"" << in_func
                             << "\" with \"" << wrapper.name() << "\"\n";
                    insert_func_wrapper_helper(func_wrappers_map, wrapped_env_iter.second, wrapped_func, wrapper);
                }
            } else { // Custom wrapper
                debug(4) << "Custom wrapper: replacing reference of \""
                         << wrapped_fname <<  "\" in \"" << in_func << "\" with \""
                         << wrapper.name() << "\"\n";

                const auto &in_func_iter = wrapped_env.find(in_func);
                if (in_func_iter == wrapped_env.end()) {
                    // We find a wrapper definition of 'wrapped_func 'for 'in_func'
                    // which is not in this pipeline. We don't need to perform
                    // the substitution since no function in this pipeline will ever
                    // refer to 'in_func'.
                    //
                    // This situation might arise in the following case below:
                    // f(x) = x;
                    // g(x) = f(x) + 1;
                    // f.in(g);
                    // f.realize(..);
                    debug(4) << "    skip custom wrapper for " << in_func << " [" << wrapped_fname
                             << " -> " << wrapper.name() << "] since it's not in the pipeline\n";
                    continue;
                }
                insert_func_wrapper_helper(func_wrappers_map, wrapped_env[in_func], wrapped_func, wrapper);
            }
        }
    }

    // Perform the substitution
    for (auto &iter : wrapped_env) {
        const auto &substitutions = func_wrappers_map[iter.second];
        if (!substitutions.empty()) {
            iter.second.substitute_calls(substitutions);
        }
    }

    return wrapped_env;
}

}
}
