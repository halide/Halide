#include "FindCalls.h"

#include "ExternFuncArgument.h"
#include "Function.h"
#include "IRVisitor.h"
#include "Parameter.h"
#include <set>
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
        bool inserted = env->emplace(f.name(), f).second;
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

    // Validate the environment: no Parameter (ImageParam, Generator
    // Input<Buffer>, or scalar Param) may share a name with a Func in the
    // pipeline. Such a collision otherwise causes confusing internal errors
    // later in lowering (or, worse, silently aliases the two buffers). A
    // Func is allowed to reference its own output buffer Parameter (e.g.
    // via Func::output_buffer()), since that Parameter is created by the
    // Func itself and necessarily shares its name; we detect that case by
    // identity (same_as), not by name, so a distinct buffer Parameter that
    // merely happens to share a Func's name is still caught.
    class FindParamNames : public IRVisitor {
        using IRVisitor::visit;
        void record(const Parameter &p) {
            if (!p.defined()) {
                return;
            }
            if (p.is_buffer()) {
                buffer_params[p.name()].push_back(p);
            } else {
                scalar_names.insert(p.name());
            }
        }
        void visit(const Variable *op) override {
            record(op->param);
        }
        void visit(const Call *op) override {
            IRVisitor::visit(op);
            record(op->param);
        }

    public:
        std::map<std::string, std::vector<Parameter>> buffer_params;
        std::set<std::string> scalar_names;
    } finder;
    for (const auto &p : env) {
        p.second.accept(&finder);
    }
    for (const auto &entry : finder.buffer_params) {
        const std::string &name = entry.first;
        auto func_it = env.find(name);
        if (func_it == env.end()) {
            continue;
        }
        const Function &f = func_it->second;
        bool all_are_own_output_buffer = true;
        for (const Parameter &p : entry.second) {
            bool matches_own_output = false;
            for (const Parameter &out_p : f.output_buffers()) {
                if (p.same_as(out_p)) {
                    matches_own_output = true;
                    break;
                }
            }
            if (!matches_own_output) {
                all_are_own_output_buffer = false;
                break;
            }
        }
        if (!all_are_own_output_buffer) {
            user_error << "The name \"" << name << "\" is used for both "
                       << "an input buffer (ImageParam or Generator Input<Buffer>) "
                       << "and a Func in the same pipeline. "
                       << "Input buffers and Funcs must have distinct names.\n";
        }
    }
    for (const std::string &name : finder.scalar_names) {
        if (env.count(name)) {
            user_error << "The name \"" << name << "\" is used for both "
                       << "a scalar Param (or Generator Input scalar) "
                       << "and a Func in the same pipeline. "
                       << "Params and Funcs must have distinct names.\n";
        }
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
