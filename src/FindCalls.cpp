#include "FindCalls.h"

#include "ExternFuncArgument.h"
#include "Function.h"
#include "IRVisitor.h"
#include <utility>

namespace Halide {
namespace Internal {

namespace {

/* Find all the internal halide calls in an expr */
class FindCalls : public IRVisitor {
public:
    std::map<std::string, Function> calls;
    std::vector<Function> order;

    using IRVisitor::visit;

    void include_function(const Function &f) {
        auto [it, inserted] = calls.emplace(f.name(), f);
        if (inserted) {
            order.push_back(f);
        } else {
            user_assert(it->second.same_as(f))
                << "Can't compile a pipeline using multiple functions with same name: "
                << f.name() << "\n";
        }
    }

    void visit(const Call *call) override {
        IRVisitor::visit(call);

        if (call->call_type == Call::Halide && call->func.defined()) {
            Function f(call->func);
            include_function(f);
        }
    }
};

void populate_environment_helper(const Function &f,
                                 std::map<std::string, Function> *env,
                                 std::vector<Function> *order,
                                 bool recursive = true,
                                 bool include_wrappers = false) {
    std::map<std::string, Function>::const_iterator iter = env->find(f.name());
    if (iter != env->end()) {
        user_assert(iter->second.same_as(f))
            << "Can't compile a pipeline using multiple functions with same name: "
            << f.name() << "\n";
        return;
    }

    auto insert_func = [](const Function &f,
                          std::map<std::string, Function> *env,
                          std::vector<Function> *order) {
        auto [it, inserted] = env->emplace(f.name(), f);
        if (inserted) {
            order->push_back(f);
        }
    };

    FindCalls calls;
    f.accept(&calls);
    if (f.has_extern_definition()) {
        for (const ExternFuncArgument &arg : f.extern_arguments()) {
            if (arg.is_func()) {
                insert_func(Function{arg.func}, &calls.calls, &calls.order);
            }
        }
    }

    if (include_wrappers) {
        for (const auto &it : f.schedule().wrappers()) {
            insert_func(Function{it.second}, &calls.calls, &calls.order);
        }
    }

    if (!recursive) {
        for (const Function &g : calls.order) {
            insert_func(g, env, order);
        }
    } else {
        insert_func(f, env, order);
        for (const Function &g : calls.order) {
            populate_environment_helper(g, env, order, recursive, include_wrappers);
        }
    }
}

}  // namespace

std::map<std::string, Function> build_environment(const std::vector<Function> &funcs) {
    std::map<std::string, Function> env;
    std::vector<Function> order;
    for (const Function &f : funcs) {
        populate_environment_helper(f, &env, &order, true, true);
    }
    return env;
}

std::vector<Function> called_funcs_in_order_found(const std::vector<Function> &funcs) {
    std::map<std::string, Function> env;
    std::vector<Function> order;
    for (const Function &f : funcs) {
        populate_environment_helper(f, &env, &order, true, true);
    }
    return order;
}

std::map<std::string, Function> find_transitive_calls(const Function &f) {
    std::map<std::string, Function> res;
    std::vector<Function> order;
    populate_environment_helper(f, &res, &order, true, false);
    return res;
}

std::map<std::string, Function> find_direct_calls(const Function &f) {
    std::map<std::string, Function> res;
    std::vector<Function> order;
    populate_environment_helper(f, &res, &order, false, false);
    return res;
}

}  // namespace Internal
}  // namespace Halide
