#include "DeepCopy.h"

namespace Halide{
namespace Internal {

using std::map;
using std::pair;
using std::string;
using std::vector;

pair<vector<Function>, map<string, Function>> deep_copy(
            const vector<Function> &outputs, const map<string, Function> &env) {
    vector<Function> copy_outputs;
    map<string, Function> copy_env;

    // Create empty deep-copies of all Functions in 'env'
    map<Function, Function, Function::Compare> copied_map; // Original Function -> Deep-copy
    for (const auto &iter : env) {
        copied_map[iter.second] = Function(iter.second.name());
    }

    // Deep copy all Functions in 'env' into their corresponding empty copies
    for (const auto &iter : env) {
        iter.second.deep_copy(copied_map[iter.second], copied_map);
    }

    // Need to substitute-in all old Function references in all Exprs referenced
    // within the Function with the deep-copy versions
    for (auto &iter : copied_map) {
        iter.second.substitute_calls(copied_map);
    }

    // Populate the env with the deep-copy version
    for (const auto &iter : copied_map) {
        copy_env.emplace(iter.first.name(), iter.second);
    }

    for (const auto &func : outputs) {
        const auto &iter = copied_map.find(func);
        if (iter != copied_map.end()) {
            debug(4) << "Adding deep-copied version to outputs: " << func.name() << "\n";
            copy_outputs.push_back(iter->second);
        } else {
            debug(4) << "Adding original version to outputs: " << func.name() << "\n";
            copy_outputs.push_back(func);
        }
    }

    return { copy_outputs, copy_env };
}

}
}
