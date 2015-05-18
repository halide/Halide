#include "FindCalls.h"
#include "IRVisitor.h"

namespace Halide{
namespace Internal {

using std::map;
using std::vector;
using std::string;
using std::pair;

/* Find all the internal halide calls in an expr */
class FindCalls : public IRVisitor {
public:
    map<string, Function> calls;

    using IRVisitor::visit;

    void include_function(Function f) {
        map<string, Function>::iterator iter = calls.find(f.name());
        if (iter == calls.end()) {
            calls[f.name()] = f;
        } else {
            user_assert(iter->second.same_as(f))
                << "Can't compile a pipeline using multiple functions with same name: "
                << f.name() << "\n";
        }
    }

    void visit(const Call *call) {
        IRVisitor::visit(call);

        if (call->call_type == Call::Halide) {
            Function f = call->func;
            include_function(f);
        }

    }
};

void populate_environment(Function f, map<string, Function> &env, bool recursive = true) {
    map<string, Function>::const_iterator iter = env.find(f.name());
    if (iter != env.end()) {
        user_assert(iter->second.same_as(f))
            << "Can't compile a pipeline using multiple functions with same name: "
            << f.name() << "\n";
        return;
    }

    FindCalls calls;
    f.accept(&calls);
    if (f.has_extern_definition()) {
        for (ExternFuncArgument arg : f.extern_arguments()) {
            if (arg.is_func()) {
                Function g(arg.func);
                calls.calls[g.name()] = g;
            }
        }
    }

    if (!recursive) {
        env.insert(calls.calls.begin(), calls.calls.end());
    } else {
        env[f.name()] = f;

        for (pair<string, Function> i : calls.calls) {
            populate_environment(i.second, env);
        }
    }
}

map<string, Function> find_transitive_calls(Function f) {
    map<string, Function> res;
    populate_environment(f, res, true);
    return res;
}

map<string, Function> find_direct_calls(Function f) {
    map<string, Function> res;
    populate_environment(f, res, false);
    return res;
}

}
}
