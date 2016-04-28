#include "FindCalls.h"
#include "IRVisitor.h"

namespace Halide{
namespace Internal {

using std::map;
using std::string;

map<string, Function> wrap_func_calls(const map<string, Function> &env) {
    map<string, Function> res(env);
    map<string, Function> wrappers;

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
                    res[in_func] = res[in_func].wrap_calls(wrapper, wrapped_fname);
                }
            } else {
                debug(4) << "Custom wrapper: replacing reference of \""
                         << wrapped_fname <<  "\" in \"" << in_func << "\" with \""
                         << wrapper.name() << "\"\n";
                res[in_func] = res[in_func].wrap_calls(wrapper, wrapped_fname);
            }
        }
    }
    res.insert(wrappers.begin(), wrappers.end());
    return res;
}

}
}
