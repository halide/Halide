#include "WrapCalls.h"
#include "FindCalls.h"

#include <set>

namespace Halide {
namespace Internal {

using std::map;
using std::set;
using std::string;

typedef map<FunctionPtr, FunctionPtr> SubstitutionMap;

namespace {

void insert_func_wrapper_helper(map<FunctionPtr, SubstitutionMap> &func_wrappers_map,
                                FunctionPtr in_func,
                                FunctionPtr wrapped_func,
                                FunctionPtr wrapper) {
    internal_assert(in_func.defined() &&
                    wrapped_func.defined() &&
                    wrapper.defined());
    internal_assert(func_wrappers_map[in_func].count(wrapped_func) == 0)
        << "Should only have one wrapper for each function call in a Func\n";

    SubstitutionMap &wrappers_map = func_wrappers_map[in_func];
    for (auto iter = wrappers_map.begin(); iter != wrappers_map.end(); ++iter) {
        if (iter->second.same_as(wrapped_func)) {
            debug(4) << "Merging wrapper of " << Function(in_func).name()
                     << " [" << Function(iter->first).name()
                     << ", " << Function(iter->second).name()
                     << "] with [" << Function(wrapped_func).name() << ", "
                     << Function(wrapper).name() << "]\n";
            iter->second = wrapper;
            return;
        } else if (wrapper.same_as(iter->first)) {
            debug(4) << "Merging wrapper of " << Function(in_func).name()
                     << " [" << Function(wrapped_func).name()
                     << ", " << Function(wrapper).name()
                     << "] with [" << Function(iter->first).name()
                     << ", " << Function(iter->second).name() << "]\n";
            wrappers_map.emplace(wrapped_func, iter->second);
            wrappers_map.erase(iter);
            return;
        }
    }
    wrappers_map[wrapped_func] = wrapper;
}

void validate_custom_wrapper(Function in_func, Function wrapped, Function wrapper) {
    map<string, Function> callees = find_direct_calls(in_func);
    if (!callees.count(wrapper.name())) {
        std::ostringstream callees_text;
        for (const auto &it : callees) {
            callees_text << "  " << it.second.name() << "\n";
        }

        user_error
            << "Cannot wrap \"" << wrapped.name() << "\" in \"" << in_func.name()
            << "\" because \"" << in_func.name() << "\" does not call \""
            << wrapped.name() << "\"\n"
            << "Direct callees of \"" << in_func.name() << "\" are:\n" << callees_text.str();
    }
}

}  // anonymous namespace

map<string, Function> wrap_func_calls(const map<string, Function> &env) {
    map<string, Function> wrapped_env;

    map<FunctionPtr, SubstitutionMap> func_wrappers_map; // In Func -> [wrapped Func -> wrapper]
    set<string> global_wrappers;

    for (const auto &iter : env) {
        wrapped_env.emplace(iter.first, iter.second);
        func_wrappers_map[iter.second.get_contents()];
    }

    for (const auto &it : env) {
        string wrapped_fname = it.first;
        FunctionPtr wrapped_func = it.second.get_contents();
        const auto &wrappers = it.second.schedule().wrappers();

        // Put the names of all wrappers of this Function into the set for
        // faster comparison during the substitution.
        set<string> all_func_wrappers;
        for (const auto &iter : wrappers) {
            all_func_wrappers.insert(Function(iter.second).name());
        }

        for (const auto &iter : wrappers) {
            string in_func = iter.first;
            FunctionPtr wrapper = iter.second;

            if (in_func.empty()) { // Global wrapper
                global_wrappers.insert(Function(wrapper).name());
                for (const auto &wrapped_env_iter : wrapped_env) {
                    in_func = wrapped_env_iter.first;
                    if ((wrapped_fname == in_func) ||
                        (all_func_wrappers.find(in_func) != all_func_wrappers.end())) {
                        // The wrapper should still call the original function,
                        // so we don't want to rewrite the calls done by the
                        // wrapper. We also shouldn't rewrite the original
                        // function itself.
                        debug(4) << "Skip over replacing \"" << in_func
                                 << "\" with \"" << Function(wrapper).name() << "\"\n";
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
                             << "\" with \"" << Function(wrapper).name() << "\"\n";
                    insert_func_wrapper_helper(func_wrappers_map,
                                               wrapped_env_iter.second.get_contents(),
                                               wrapped_func, wrapper);
                }
            } else { // Custom wrapper
                debug(4) << "Custom wrapper: replacing reference of \""
                         << wrapped_fname <<  "\" in \"" << in_func << "\" with \""
                         << Function(wrapper).name() << "\"\n";

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
                             << " -> " << Function(wrapper).name() << "] since it's not in the pipeline\n";
                    continue;
                }
                insert_func_wrapper_helper(func_wrappers_map,
                                           wrapped_env[in_func].get_contents(),
                                           wrapped_func,
                                           wrapper);
            }
        }
    }

    // Perform the substitution
    for (auto &iter : wrapped_env) {
        const auto &substitutions = func_wrappers_map[iter.second.get_contents()];
        if (!substitutions.empty()) {
            iter.second.substitute_calls(substitutions);
        }
    }

    // Assert that the custom wrappers are actually used, i.e. if f.in(g) is
    // called, but 'f' is never called inside 'g', this will throw a user error.
    // Perform the check after the wrapper substitution to handle multi-fold
    // wrappers, e.g. f.in(g).in(g).
    for (const auto &iter : wrapped_env) {
        const auto &substitutions = func_wrappers_map[iter.second.get_contents()];
        for (const auto &pair : substitutions) {
            if (global_wrappers.find(Function(pair.second).name()) == global_wrappers.end()) {
                validate_custom_wrapper(iter.second, Function(pair.first), Function(pair.second));
            }
        }
    }

    return wrapped_env;
}

}  // namespace Internal
}  // namespace Halide
