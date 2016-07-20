#include <algorithm>
#include <map>
#include <string>
#include <limits>

#include "Prefetch.h"
#include "CodeGen_Internal.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;
using std::stack;

class InjectPrefetch : public IRMutator {
public:
    InjectPrefetch(const map<string, Function> &e)
        : env(e) { }
    Scope<int> scope;
private:
    const map<string, Function> &env;
#if 0 // not currently using realize
    Scope<int> realizations;
    stack<Function> cs;
#endif

private:
    using IRMutator::visit;

    // Strip down the tuple name, e.g. f.*.var into f
    string tuple_func(const string &name) {
        vector<string> v = split_string(name, ".");
        internal_assert(v.size() > 0);
        return v[0];
    }

    // Strip down the tuple name, e.g. f.*.var into var
    string tuple_var(const string &name) {
        vector<string> v = split_string(name, ".");
        internal_assert(v.size() > 0);
        return v[v.size()-1];
    }

    Function get_func(const string &name) {
        map<string, Function>::const_iterator iter = env.find(name);
        internal_assert(iter != env.end()) << "Realize node refers to function not in environment.\n";
        return iter->second;
    }

    void visit(const For *op) {
        Stmt body = op->body;

#if 0 // not currently using realize
        string func_name = cs.top().name();
        vector<Prefetch> &prefetches = cs.top().schedule().prefetches();
#endif

        string func_name = tuple_func(op->name);
        string var_name  = tuple_var(op->name);
        vector<Prefetch> &prefetches = get_func(func_name).schedule().prefetches();

        std::cerr << "Prefetch: " << op->name << " " << func_name << " " << var_name;
        if (prefetches.empty()) {
            std::cerr << " No prefetches in schedule\n";
        } else {
            std::cerr << " Checking prefetches\n";
        }

        // Todo: Check to see if op->name is in prefetches
        for (const Prefetch &p : prefetches) {
            std::cerr << "Prefetch: " << p.var
                               << " " << p.param << "\n";
            if (p.var == var_name) {
                std::cerr << " matched on " << var_name << "\n";
            }
        }
#if 0
        // Add prefetch to body on inputs
        // Todo: For each input...
        // Todo: support higher level param interface
        Stmt prefetch =
            Evaluate::make(Call::make(Int(32), Call::prefetch,
                                      {p.var, p.param}, Call::Intrinsic));
        body = Block::make({prefetch, body});
#endif

        body = mutate(body);
        stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
    }

#if 0 // not currently using realize
    void visit(const Realize *realize) {
        realizations.push(realize->name, 1);

        map<string, Function>::const_iterator iter = env.find(realize->name);
        internal_assert(iter != env.end()) << "Realize node refers to function not in environment.\n";
        cs.push(iter->second);

        Stmt body = mutate(realize->body);

        realizations.pop(realize->name);
        cs.pop();
    }
#endif

};

Stmt inject_prefetch(Stmt s, const std::map<std::string, Function> &env)
{
    return InjectPrefetch(env).mutate(s);
}

}
}
