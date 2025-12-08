#include "FindCalls.h"

#include "ExternFuncArgument.h"
#include "Function.h"
#include "IRVisitor.h"
#include <utility>

namespace Halide {
namespace Internal {

namespace {

struct CallInfo {
    std::map<std::string, Function> calls;
    std::vector<Function> order;
};

/* Find all the internal halide calls in an expr */
template<typename T>
CallInfo find_calls(const T &ir) {
    CallInfo info;
    visit_with(ir, [&](auto *self, const Call *call) {
        self->visit_base(call);
        if (call->call_type == Call::Halide && call->func.defined()) {
            Function f(call->func);
            auto [it, inserted] = info.calls.emplace(f.name(), f);
            if (inserted) {
                info.order.push_back(f);
            } else {
                user_assert(it->second.same_as(f))
                    << "Can't compile a pipeline using multiple functions with same name: "
                    << f.name() << "\n";
            }
        }
    });
    return info;
}

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
        bool inserted = env->emplace(f.name(), f).second;
        if (inserted) {
            order->push_back(f);
        }
    };

    auto [f_calls, f_order] = find_calls(f);
    if (f.has_extern_definition()) {
        for (const ExternFuncArgument &arg : f.extern_arguments()) {
            if (arg.is_func()) {
                insert_func(Function{arg.func}, &f_calls, &f_order);
            }
        }
    }

    if (include_wrappers) {
        for (const auto &it : f.schedule().wrappers()) {
            insert_func(Function{it.second}, &f_calls, &f_order);
        }
    }

    if (!recursive) {
        for (const Function &g : f_order) {
            insert_func(g, env, order);
        }
    } else {
        insert_func(f, env, order);
        for (const Function &g : f_order) {
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
