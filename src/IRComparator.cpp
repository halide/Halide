#include "IRComparator.h"
#include "FindCalls.h"
#include "Func.h"
#include "Function.h"
#include "IREquality.h"
#include "IRVisitor.h"
#include <map>
#include <string>

namespace Halide {
namespace Internal {
class IRComparator {
public:
    IRComparator() = default;

    bool compare_pipeline(const Pipeline &p1, const Pipeline &p2);

private:
    bool compare_function(const Function &f1, const Function &f2);
};

bool IRComparator::compare_pipeline(const Pipeline &p1, const Pipeline &p2) {
    std::map<std::string, Function> p1_env, p2_env;
    for (const Func &func : p1.outputs()) {
        const Halide::Internal::Function &f = func.function();
        std::map<std::string, Halide::Internal::Function> more_funcs = find_transitive_calls(f);
        p1_env.insert(more_funcs.begin(), more_funcs.end());
    }
    for (const Func &func : p2.outputs()) {
        const Halide::Internal::Function &f = func.function();
        std::map<std::string, Halide::Internal::Function> more_funcs = find_transitive_calls(f);
        p2_env.insert(more_funcs.begin(), more_funcs.end());
    }
    if (p1_env.size() != p2_env.size()) {
        return false;
    }
    for (auto it = p1_env.begin(); it != p1_env.end(); it++) {
        if (p2_env.find(it->first) == p2_env.end()) {
            return false;
        }
        if (!compare_function(it->second, p2_env[it->first])) {
            return false;
        }
    }
    for (size_t i = 0; i < p1.requirements().size() && i < p2.requirements().size(); i++) {
        if (!equal(p1.requirements()[i], p2.requirements()[i])) {
            return false;
        }
    }
    return true;
}

bool IRComparator::compare_function(const Function &f1, const Function &f2) {
    if (f1.name() != f2.name()) {
        return false;
    }
    if (f1.origin_name() != f2.origin_name()) {
        return false;
    }
    return true;
}

bool equal(const Halide::Pipeline &p1, const Halide::Pipeline &p2) {
    return IRComparator().compare_pipeline(p1, p2);
}
}  // namespace Internal
}  // namespace Halide