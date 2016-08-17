#include <algorithm>
#include <map>
#include <string>
#include <limits>

#include "Prefetch.h"
#include "CodeGen_Internal.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Bounds.h"
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

int dbg_prefetch  = 1;
int dbg_prefetch2 = 2;

class InjectPrefetch : public IRMutator {
public:
    InjectPrefetch(const map<string, Function> &e)
        : env(e) { }
private:
    const map<string, Function> &env;
    Scope<Interval> scope;

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

    void visit(const Let *op) {
        Interval in = bounds_of_expr_in_scope(op->value, scope);
        scope.push(op->name, in);
        IRMutator::visit(op);
        scope.pop(op->name);
    }
    void visit(const LetStmt *op) {
        Interval in = bounds_of_expr_in_scope(op->value, scope);
        scope.push(op->name, in);
        IRMutator::visit(op);
        scope.pop(op->name);
    }

    void visit(const For *op) {
        Stmt body = op->body;

        string func_name = tuple_func(op->name);
        string var_name  = tuple_var(op->name);
        vector<Prefetch> &prefetches = get_func(func_name).schedule().prefetches();

        if (prefetches.empty()) {
            debug(dbg_prefetch2) << "InjectPrefetch: " << op->name << " " << func_name << " " << var_name;
            debug(dbg_prefetch2) << " No prefetch\n";
        } else {
            debug(dbg_prefetch) << "InjectPrefetch: " << op->name << " " << func_name << " " << var_name;
            debug(dbg_prefetch) << " Found prefetch directive(s)\n";
        }

        // Todo: Check to see if op->name is in prefetches
        for (const Prefetch &p : prefetches) {
            debug(dbg_prefetch) << "InjectPrefetch: check var:" << p.var
                               << " offset:" << p.offset << "\n";
            if (p.var == var_name) {
                debug(dbg_prefetch) << " prefetch on " << var_name << "\n";
                string fetch_func = "halide.hexagon.l2fetch.Rtt";

                // Interval prein(op->name, op->name);
                // Add in prefetch offset
                Expr var = Variable::make(Int(32), op->name);
                Interval prein(var + p.offset, var + p.offset);
                scope.push(op->name, prein);

                map<string, Box> boxes;
                boxes = boxes_required(body, scope);

                debug(dbg_prefetch) << "  boxes required:\n";
                for (auto &i : boxes) {
                    const string &varname = i.first;
                    Box &box = i.second;
                    debug(dbg_prefetch) << "  var:" << varname << ":\n";
                    for (size_t k = 0; k < box.size(); k++) {
                        debug(dbg_prefetch) << "    ---\n";
                        debug(dbg_prefetch) << "    box[" << k << "].min: " << box[k].min << "\n";
                        debug(dbg_prefetch) << "    box[" << k << "].max: " << box[k].max << "\n";
                    }
                    debug(dbg_prefetch) << "    ---------\n";

#if 0 // todo create_buffer_t from box (see BoundsInference.cpp)
                    Expr prefetch_buf_t = Call::make(type_of<struct buffer_t *>(), Call::create_buffer_t,
                                                      output_buffer_t_args, Call::Intrinsic);
                    // Add prefetch to body on inputs
                    // Todo: For each input...
                    Expr tmp = Expr(0);
                    Stmt prefetch =
                        Evaluate::make(Call::make(Int(32), fetch_func,
                                          {tmp, prefetch_buf_t}, Call::Extern));
                    body = Block::make({prefetch, body});
#endif
                }

                scope.pop(op->name);
            }
        }

        body = mutate(body);
        stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
    }

};

Stmt inject_prefetch(Stmt s, const std::map<std::string, Function> &env)
{
    if (char *dbg = getenv("HL_DBG_PREFETCH")) {
        dbg_prefetch  -= atoi(dbg);
        dbg_prefetch2 -= atoi(dbg);
        if (dbg_prefetch < 0) {
            dbg_prefetch = 0;
        }
        if (dbg_prefetch2 < 0) {
            dbg_prefetch2 = 0;
        }
    }
    return InjectPrefetch(env).mutate(s);
}

}
}
