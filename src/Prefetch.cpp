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
using std::pair;

// Prefetch debug levels
int dbg_prefetch1 = 1;
int dbg_prefetch2 = 2;
int dbg_prefetch3 = 3;
int dbg_prefetch4 = 4;
int dbg_prefetch5 = 5;

#define MIN(x,y)        (((x)<(y)) ? (x) : (y))
#define MAX(x,y)        (((x)>(y)) ? (x) : (y))

class InjectPrefetch : public IRMutator {
public:
    InjectPrefetch(const map<string, Function> &e)
        : env(e) { }
private:
    const map<string, Function> &env;   // Environment
    Scope<Interval> scope;              // Interval scope
    Scope<int> rscope;                  // Realize scope
    stack<string> rstack;               // Realize stack
    unsigned long ptmp = 0;             // ID for all tmp vars in a prefetch op

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

    // Lookup a function in the environment
    Function get_func(const string &name) {
        map<string, Function>::const_iterator iter = env.find(name);
        internal_assert(iter != env.end()) << "function not in environment.\n";
        return iter->second;
    }

    // Determine the static type of a named buffer (if available)
    //
    // Note: If the type cannot be determined, the variable will
    //       be flagged as not having a static type (e.g. the type
    //       of input is only known at runtime, input.elem_size).
    //
    Type get_type(string varname, bool &has_static_type) {
        has_static_type = false;
        debug(dbg_prefetch4) << "    getType(" << varname << ")";
        Type t = UInt(8);       // default type
        map<string, Function>::const_iterator varit = env.find(varname);
        if (varit != env.end()) {
            Function varf = varit->second;
            debug(dbg_prefetch4) << " found: " << varit->first;
            if (varf.outputs()) {
                vector<Type> varts = varf.output_types();
                t = varts[0];
                has_static_type = true;
            }
        } else {
            debug(dbg_prefetch4) << " not found";
        }

        if (has_static_type) {
            debug(dbg_prefetch4) << ", type: " << t << "\n";
        } else {
            debug(dbg_prefetch4) << ", no static type\n";
        }

        return t;
    }

    // Generate the required prefetch code (lets and statements) for the
    // specified varname and box
    //
    void prefetch_box(const string &varname, Box &box,
                vector<pair<string, Expr>> &plets, vector<Stmt> &pstmts)
    {
        bool do_prefetch = true;
        int dims = box.size();
        bool has_static_type = true;
        Type t = get_type(varname, has_static_type);
        string elem_size_name = varname + ".elem_size";
        Expr elem_size_bytes;

        if (rscope.contains(varname)) {
            debug(dbg_prefetch2) << "  Found realize node for " << varname << "\n";
            debug(dbg_prefetch1) << "  Info: not prefetching " << varname << "\n";
            return;
        }
        if (has_static_type) {
            elem_size_bytes = t.bytes();
        } else {   // Use element size for inputs that don't have static types
            Expr elem_size_var = Variable::make(Int(32), elem_size_name);
            elem_size_bytes = elem_size_var;
        }

        std::ostringstream ss; ss << t;
        string type_name = ss.str();
        debug(dbg_prefetch1) << "  prefetch" << ptmp << ": "
                             << varname << " ("
                             << (has_static_type ? type_name : elem_size_name)
                             << ", dims:" << dims << ")\n";

        for (int i = 0; i < dims; i++) {
            debug(dbg_prefetch3) << "    ---\n";
            debug(dbg_prefetch3) << "    box[" << i << "].min: " << box[i].min << "\n";
            debug(dbg_prefetch3) << "    box[" << i << "].max: " << box[i].max << "\n";
        }
        debug(dbg_prefetch3) << "    ---------\n";

        // TODO: Opt: check box if it should be prefetched?
        // TODO       - Only prefetch if varying by ivar_name?
        // TODO       - Don't prefetch if "small" all constant dimensions?
        // TODO         e.g. see: camera_pipe.cpp corrected matrix(4,3)

        string pstr = std::to_string(ptmp++);
        string varname_prefetch_buf = varname + "_prefetch_" + pstr + "_buf";
        Expr var_prefetch_buf = Variable::make(Int(32), varname_prefetch_buf);

        // Establish the variables for buffer strides, box min & max
        vector<Expr> stride_var(dims);
        vector<Expr> extent_var(dims);
        vector<Expr> min_var(dims);
        vector<Expr> max_var(dims);
        for (int i = 0; i < dims; i++) {
            string istr = std::to_string(i);
            // string extent_name = varname + ".extent." + istr;
            string stride_name = varname + ".stride." + istr;
            string extent_name = varname + ".extent." + istr;
            string min_name = varname + "_prefetch_" + pstr + "_min_" + istr;
            string max_name = varname + "_prefetch_" + pstr + "_max_" + istr;

            stride_var[i] = Variable::make(Int(32), stride_name);
            extent_var[i] = Variable::make(Int(32), extent_name);
            min_var[i] = Variable::make(Int(32), min_name);
            max_var[i] = Variable::make(Int(32), max_name);

            // Record let assignments
            // except for stride, already defined elsewhere
            plets.push_back(make_pair(min_name, box[i].min));
            plets.push_back(make_pair(max_name, box[i].max));
        }

        // This box should not be prefetched
        if (!do_prefetch) {
            debug(dbg_prefetch1) << "  Info: not prefetching " << varname << "\n";
            return;
        }

        // Create a buffer_t object for this prefetch.
        vector<Expr> args(dims*3 + 2);

        Expr first_elem = Load::make(t, varname, 0, Buffer(), Parameter());
        args[0] = Call::make(Handle(), Call::address_of, {first_elem}, Call::PureIntrinsic);
        args[1] = make_zero(t);
        for (int i = 0; i < dims; i++) {
            args[3*i+2] = min_var[i];
            args[3*i+3] = max_var[i] - min_var[i] + 1;
            args[3*i+4] = stride_var[i];
        }

        // Create the create_buffer_t call
        Expr prefetch_buf = Call::make(type_of<struct buffer_t *>(), Call::create_buffer_t,
                              args, Call::Intrinsic);
        plets.push_back(make_pair(varname_prefetch_buf, prefetch_buf));

        // Create the prefetch call
        Expr num_elem = stride_var[dims-1] * extent_var[dims-1];
        vector<Expr> args_prefetch(4);
        args_prefetch[0] = dims;
        args_prefetch[1] = elem_size_bytes;
        args_prefetch[2] = num_elem;
        args_prefetch[3] = var_prefetch_buf;
        Stmt stmt_prefetch = Evaluate::make(Call::make(Int(32), Call::prefetch_buffer_t,
                              args_prefetch, Call::Intrinsic));
        // TODO: Opt: Keep running sum of bytes prefetched on this sequence
        // TODO: Opt: Keep running sum of number of prefetch instructions issued
        // TODO       on this sequence? (to not exceed MAX_PREFETCH)
        // TODO: Opt: Generate control code for prefetch_buffer_t in Prefetch.cpp
        // TODO       Passing box info through a buffer_t results in ~30 additional stores/loads

        pstmts.push_back(stmt_prefetch);
    }

    // Inject the generated prefetch code
    //
    // Note: plets/pstmts may be modified or partially emptied during injection
    //
    void inject_prefetch_stmts(const For *op, const Prefetch &p,
                vector<pair<string, Expr>> &plets, vector<Stmt> &pstmts, Stmt &body)
    {
        if (pstmts.size() > 0) {
            Stmt pbody = pstmts.back();    // Initialize prefetch body
            pstmts.pop_back();
            for (size_t i = pstmts.size(); i > 0; i--) {
                pbody = Block::make(pstmts[i-1], pbody);
            }
            for (size_t i = plets.size(); i > 0; i--) {
                pbody = LetStmt::make(plets[i-1].first, plets[i-1].second, pbody);
            }

#if 0
            // Guard to not prefetch past the end of the iteration space
            // TODO: Opt: Use original extent of loop in guard condition to
            // TODO       prefetch valid iterations that are otherwise skipped
            // TODO       with the current extent when loop is stripmined
            // TODO: Opt: Consider not using a guard at all and letting address
            // TODO       range check in prefetch runtime take care of guarding
            Expr pcond = likely((Variable::make(Int(32), op->name) + p.offset)
                                                < (op->min + op->extent - 1));
            Stmt pguard = IfThenElse::make(pcond, pbody);

            body = Block::make({pguard, body});
            debug(dbg_prefetch4) << pguard << "\n";
#else
            // Don't guard, perform address range check in prefetch runtime
            body = Block::make({pbody, body});
            debug(dbg_prefetch4) << pbody << "\n";
#endif

        }
    }

    void visit(const Let *op) {
        Interval in = bounds_of_expr_in_scope(op->value, scope);
        scope.push(op->name, in);
        debug(dbg_prefetch5) << "Let scope.push(" << op->name << ")\n";
        IRMutator::visit(op);
        debug(dbg_prefetch5) << "Let scope.pop(" << op->name << ")\n";
        scope.pop(op->name);
    }

    void visit(const LetStmt *op) {
        Interval in = bounds_of_expr_in_scope(op->value, scope);
        scope.push(op->name, in);
        debug(dbg_prefetch5) << "LetStmt scope.push(" << op->name << ")\n";
        IRMutator::visit(op);
        debug(dbg_prefetch5) << "LetStmt scope.pop(" << op->name << ")\n";
        scope.pop(op->name);
    }

    void visit(const Realize *op) {
        rscope.push(op->name, 1);
        rstack.push(op->name);
        debug(dbg_prefetch5) << "Realize push(" << op->name << ")\n";
        IRMutator::visit(op);
        // Note: the realize scope/stack is cleaned up elsewhere
    }

    void visit(const For *op) {
        Stmt body = op->body;

        string func_name = tuple_func(op->name);
        string ivar_name = tuple_var(op->name);
        vector<Prefetch> &prefetches = get_func(func_name).schedule().prefetches();

        debug(dbg_prefetch4) << "For: " << op->name << " " << func_name << " " << ivar_name << "\n";
        if (prefetches.empty()) {
            debug(dbg_prefetch4) << " No prefetch directives in schedule\n";
        } else {
            debug(dbg_prefetch4) << " Found prefetch directive(s) in schedule\n";
        }

        // Record current position on realize stack
        size_t rpos = rstack.size();
        debug(dbg_prefetch5) << " Realize stack for " << op->name << " on entry:" << rstack.size() << "\n";

        body = mutate(body);

        if (rstack.size() > rpos) {
            debug(dbg_prefetch5) << " Realize stack for " << op->name << " during:" << rstack.size() << "\n";
        }

        for (const Prefetch &p : prefetches) {
            debug(dbg_prefetch4) << " Check prefetch ivar: " << p.var << " == " << ivar_name << "\n";
            if (p.var == ivar_name) {
                debug(dbg_prefetch4) << " Found directive matching " << ivar_name << "\n";
                debug(dbg_prefetch1) << " " << func_name
                                     << " prefetch(" << ivar_name << ", " << p.offset << ")\n";

                // Add in prefetch offset
                Expr var = Variable::make(Int(32), op->name);
                Interval prein(var + p.offset, var + p.offset);
                scope.push(op->name, prein);
                debug(dbg_prefetch5) << " For scope.push(" << op->name << ", ("
                                     << var << " + " << p.offset << ", "
                                     << var << " + " << p.offset << "))\n";

                map<string, Box> boxes;
                boxes = boxes_required(body, scope);
                // TODO: Opt: prefetch the difference from previous iteration
                //            to the requested iteration (2 calls to boxes_required)

                vector<pair<string, Expr>> plets;  // Prefetch let assignments
                vector<Stmt> pstmts;               // Prefetch stmt sequence
                debug(dbg_prefetch3) << "  boxes required:\n";
                for (auto &b : boxes) {
                    const string &varname = b.first;
                    Box &box = b.second;
                    prefetch_box(varname, box, plets, pstmts);
                }

                inject_prefetch_stmts(op, p, plets, pstmts, body);

                debug(dbg_prefetch5) << " For scope.pop(" << op->name << ")\n";
                scope.pop(op->name);
            }
        }

        // Restore previous position on realize stack
        while (rstack.size() > rpos) {
            string &rname = rstack.top();
            rscope.pop(rname);
            rstack.pop();
            debug(dbg_prefetch5) << "Realize pop(" << rname << ")\n";
        }
        debug(dbg_prefetch5) << "Realize stack for " << op->name << " on exit:" << rstack.size() << "\n";

        stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
        debug(dbg_prefetch4) << "EndFor: " << op->name << "\n";
    }

};

Stmt inject_prefetch(Stmt s, const std::map<std::string, Function> &env)
{
    size_t read;
    std::string lvl = get_env_variable("HL_DEBUG_PREFETCH", read);
    if (read) {
        int dbg_level = atoi(lvl.c_str());
        dbg_prefetch1 = MAX(dbg_prefetch1 - dbg_level, 0);
        dbg_prefetch2 = MAX(dbg_prefetch2 - dbg_level, 0);
        dbg_prefetch3 = MAX(dbg_prefetch3 - dbg_level, 0);
        dbg_prefetch4 = MAX(dbg_prefetch4 - dbg_level, 0);
        dbg_prefetch5 = MAX(dbg_prefetch5 - dbg_level, 0);
    }

    debug(dbg_prefetch1) << "prefetch:\n";
    return InjectPrefetch(env).mutate(s);
}

}
}
