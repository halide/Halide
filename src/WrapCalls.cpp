#include "FindCalls.h"
#include "IRVisitor.h"

namespace Halide{
namespace Internal {

using std::map;
using std::string;

map<string, Function> wrap_func_calls(const map<string, Function> &env) {
    map<string, Function> res(env);
    map<string, Function> wrappers;

    for (const auto &iter : res) {
        debug(0) << "....FUNC: " << iter.first << "\n"  ;
    }


    for (const auto &it : env) {
        string wrapped_fname = it.first;
        debug(0) << "\n***********Wrapping call of function: " << wrapped_fname << "\n";
        Function &wrapped_func = res[wrapped_fname]; // Function being wrapped if applicable
        for (const auto &iter : wrapped_func.schedule().wrappers()) {
            string in_func = iter.first;
            const Function &wrapper = iter.second;
            wrappers[wrapper.name()] = wrapper;
            debug(0) << "   Adding wrapper: " << wrapper.name() << "  to func: " << in_func << "\n";
            if (in_func == "$global") { // Global wrapper
                debug(0) << "GLOBAL WRAPPER\n";
                for (const auto &res_iter : res) {
                    in_func = res_iter.first;
                    if (wrapped_func.schedule().wrappers().count(in_func)) {
                        debug(0) << "+++++++SKIPPING " << in_func << " global wrapper\n";
                        continue;
                    }
                    debug(0) << "....in_func: " << res_iter.first << "\n"  ;
                    res[in_func] = res[in_func].wrap_calls(wrapper, wrapped_fname);
                    debug(0) << "       Replacing reference of '" << wrapped_fname <<  "' in '" << in_func << "' with '" << wrapper.name() << "'\n";
                }
            } else {
                res[in_func] = res[in_func].wrap_calls(wrapper, wrapped_fname);
                debug(0) << "       Replacing reference of '" << wrapped_fname <<  "' in '" << in_func << "' with '" << wrapper.name() << "'\n";
            }
        }
    }
    res.insert(wrappers.begin(), wrappers.end());
    return res;
}

}
}
