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
    Scope<int> realizations;
    stack<Function> cs;

private:
    using IRMutator::visit;

    void visit(const For *op) {
        Stmt body = op->body;

        vector<Prefetch> &prefetches = cs.top().schedule().prefetches();

        if (prefetches.empty()) {
            std::cerr << "Prefetch: " << cs.top().name() << " " << op->name << " No prefetches\n";
        } else {
            std::cerr << "Prefetch: " << cs.top().name() << " " << op->name << " Found prefetches\n";
        }

        // Todo: Check to see if op->name is in prefetches
        for (const Prefetch &p : prefetches) {
            std::cerr << "Prefetch: " << p.var
                               << " " << p.param << "\n";
        }
#if 0
        // Add prefetch to body
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

    void visit(const Realize *realize) {
        realizations.push(realize->name, 1);

        map<string, Function>::const_iterator iter = env.find(realize->name);
        internal_assert(iter != env.end()) << "Realize node refers to function not in environment.\n";
        cs.push(iter->second);

        Stmt body = mutate(realize->body);

        realizations.pop(realize->name);
        cs.pop();
    }

};

Stmt inject_prefetch(Stmt s, const map<string, Function> &env)
{
    return InjectPrefetch(env).mutate(s);
}

}
}
