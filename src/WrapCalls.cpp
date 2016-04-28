#include "FindCalls.h"
#include "IRVisitor.h"

namespace Halide{
namespace Internal {

using std::map;
using std::string;

map<string, Function> wrap_func_calls(const map<string, Function> &env) {
    map<string, Function> res = env;

    for (const auto &it : env) {
        string fname = it.first;
        Function &func = res[fname]; // Function being wrapped if applicable
        for (const auto &iter : func.schedule().wrappers()) {
            const Function &wrapper = iter.second;
            res[wrapper.name()] = wrapper;
            if (iter.first == "__global") { // Global wrapper
                debug(0) << "GLOBAL WRAPPER\n";
                internal_assert(func.schedule().wrappers().size() == 1);
                for (auto &res_iter : res) {
                    res_iter.second = res_iter.second.wrap_calls(wrapper, fname);
                }
                break;
            } else {
                res[iter.first] = res[iter.first].wrap_calls(wrapper, fname);
            }
        }
    }
    return res;
}

}
}
