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

// Prefetch debug levels
int dbg_prefetch  = 1;
int dbg_prefetch2 = 2;
int dbg_prefetch3 = 10;

class InjectPrefetch : public IRMutator {
public:
    InjectPrefetch(const map<string, Function> &e)
        : env(e) { }
private:
    const map<string, Function> &env;
    Scope<Interval> scope;
    unsigned long ptmp = 0;   // ID for all tmp vars in a prefetch op

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

        for (const Prefetch &p : prefetches) {
            debug(dbg_prefetch) << "InjectPrefetch: check var:" << p.var
                               << " offset:" << p.offset << "\n";
            if (p.var == var_name) {
                debug(dbg_prefetch) << " prefetch on " << var_name << "\n";

                // Interval prein(op->name, op->name);
                // Add in prefetch offset
                Expr var = Variable::make(Int(32), op->name);
                Interval prein(var + p.offset, var + p.offset);
                scope.push(op->name, prein);

                map<string, Box> boxes;
                boxes = boxes_required(body, scope);

                int cnt = 0;            // Number of prefetch ops generated
                Stmt pstmts;            // Generated prefetch statements
                debug(dbg_prefetch) << "  boxes required:\n";
                for (auto &b : boxes) {
                    const string &varname = b.first;
                    Box &box = b.second;
                    int dims = box.size();
                    debug(dbg_prefetch) << "  var:" << varname << ":\n";
                    for (int i = 0; i < dims; i++) {
                        debug(dbg_prefetch) << "    ---\n";
                        debug(dbg_prefetch) << "    box[" << i << "].min: " << box[i].min << "\n";
                        debug(dbg_prefetch) << "    box[" << i << "].max: " << box[i].max << "\n";
                    }
                    debug(dbg_prefetch) << "    ---------\n";

                    string pstr = std::to_string(ptmp++);
                    string varname_prefetch_buf = varname + ".prefetch_" + pstr + "_buf";
                    Expr var_prefetch_buf = Variable::make(Int(32), varname_prefetch_buf);

                    // Make the names for accessing the buffer strides
                    vector<Expr> stride_var(dims);
                    for (int i = 0; i < dims; i++) {
                        string istr = std::to_string(i);
                        string stride_name = varname + ".stride." + istr;
                        stride_var[i] = Variable::make(Int(32), stride_name);
                    }

                    // Make the names for the prefetch box mins & maxes
                    vector<string> min_name(dims), max_name(dims);
                    for (int i = 0; i < dims; i++) {
                        string istr = std::to_string(i);
                        min_name[i] = varname + ".prefetch_" + pstr + "_min_" + istr;
                        max_name[i] = varname + ".prefetch_" + pstr + "_max_" + istr;
                    }
                    vector<Expr> min_var(dims), max_var(dims);
                    for (int i = 0; i < dims; i++) {
                        min_var[i] = Variable::make(Int(32), min_name[i]);
                        max_var[i] = Variable::make(Int(32), max_name[i]);
                    }

                    // Create a buffer_t object for this prefetch.
                    Type t = Int(16);  // FIXME (need type of box...or extract elem_size)
                    vector<Expr> args(dims*3 + 2);
                    Expr first_elem = Load::make(t, varname, 0, Buffer(), Parameter());
                    args[0] = Call::make(Handle(), Call::address_of, {first_elem}, Call::PureIntrinsic);
                    args[1] = make_zero(t);
                    for (int i = 0; i < dims; i++) {
                        args[3*i+2] = min_var[i];
                        args[3*i+3] = max_var[i] - min_var[i] + 1;
                        args[3*i+4] = stride_var[i];
                    }

                    // Inject the prefetch call
                    vector<Expr> args_prefetch(2);
                    args_prefetch[0] = dims;
                    args_prefetch[1] = var_prefetch_buf;
                    Stmt stmt_prefetch = Evaluate::make(Call::make(Int(32), Call::prefetch_buffer_t,
                                          args_prefetch, Call::Intrinsic));
                    if (cnt == 0) {
                        pstmts = stmt_prefetch;
                    } else {
                        pstmts = Block::make({stmt_prefetch, pstmts});
                    }
                    cnt++;

                    // Inject the create_buffer_t call
                    Expr prefetch_buf = Call::make(type_of<struct buffer_t *>(), Call::create_buffer_t,
                                          args, Call::Intrinsic);
                    pstmts = LetStmt::make(varname_prefetch_buf, prefetch_buf, pstmts);

                    // Inject bounds variable assignments
                    for (int i = dims-1; i >= 0; i--) {
                        pstmts = LetStmt::make(max_name[i], box[i].max, pstmts);
                        pstmts = LetStmt::make(min_name[i], box[i].min, pstmts);
                        // stride already defined by input buffer
                    }
                }

                if (cnt) {
#if 1
                    // Guard to not prefetch past the end of the iteration space
                    Expr pcond = likely((Variable::make(Int(32), op->name) + p.offset)
                                                        < (op->min + op->extent - 1));
                    Stmt pguard = IfThenElse::make(pcond, pstmts);
                    body = Block::make({pguard, body});

                    debug(dbg_prefetch3) << "    prefetch: (cnt:" << cnt << ")\n";
                    debug(dbg_prefetch3) << pguard << "\n";
#else
                    body = Block::make({pstmts, body});
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
    size_t read;
    std::string lvl = get_env_variable("HL_DEBUG_PREFETCH", read);
    if (read) {
        int dbg_level = atoi(lvl.c_str());
        dbg_prefetch  -= dbg_level;
        dbg_prefetch2 -= dbg_level;
        dbg_prefetch3 -= dbg_level;
        if (dbg_prefetch < 0) {
            dbg_prefetch = 0;
        }
        if (dbg_prefetch2 < 0) {
            dbg_prefetch2 = 0;
        }
        if (dbg_prefetch3 < 0) {
            dbg_prefetch3 = 0;
        }
    }
    return InjectPrefetch(env).mutate(s);
}

}
}
