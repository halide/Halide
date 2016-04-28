#include "WrapCalls.h"
#include "IRVisitor.h"

#include <set>

namespace Halide{
namespace Internal {

using std::map;
using std::set;
using std::string;

// If we haven't made deep-copy of 'in_func', create one first and mutate the copy.
// Otherwise, mutate the existing copy.
void wrap_func_calls_helper(map<string, Function> &env, const string &in_func,
                            const Function &wrapper, const string &wrapped_fname,
                            set<string> &copied) {
    if (copied.find(in_func) == copied.end()) {
        debug(0) << "  Deep copying function \"" << in_func << "\"\n";
        //env[in_func] = Function(env[in_func]); // Replace with deep-copy
    }
    env[in_func] = env[in_func].wrap_calls(wrapper, wrapped_fname);
}

map<string, Function> wrap_func_calls(const map<string, Function> &env) {
    map<string, Function> res(env);
    map<string, Function> wrappers;
    set<string> copied;

    for (const auto &it : env) {
        string wrapped_fname = it.first;
        Function &wrapped_func = res[wrapped_fname];
        for (const auto &iter : wrapped_func.schedule().wrappers()) {
            string in_func = iter.first;
            const Function &wrapper = iter.second;
            wrappers[wrapper.name()] = wrapper;
            if (in_func == "$global") { // Global wrapper
                for (const auto &res_iter : res) {
                    in_func = res_iter.first;
                    if (wrapped_func.schedule().wrappers().count(in_func)) {
                        // If the 'in_func' already has custom wrapper for 'wrapped_func',
                        // don't substitute in the global wrapper.
                        continue;
                    }
                    debug(4) << "Global wrapper: replacing reference of \""
                             << wrapped_fname <<  "\" in \"" << in_func
                             << "\" with \"" << wrapper.name() << "\"\n";
                    wrap_func_calls_helper(res, in_func, wrapper, wrapped_fname, copied);
                }
            } else {
                debug(4) << "Custom wrapper: replacing reference of \""
                         << wrapped_fname <<  "\" in \"" << in_func << "\" with \""
                         << wrapper.name() << "\"\n";
                wrap_func_calls_helper(res, in_func, wrapper, wrapped_fname, copied);
            }
        }
    }
    res.insert(wrappers.begin(), wrappers.end());
    return res;
}

}
}
