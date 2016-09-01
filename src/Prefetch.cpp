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
            debug(dbg_prefetch4) << ", no static type, defaulting to: " << t << "\n";
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
        debug(dbg_prefetch1) << "  prefetch" << ptmp << ": "
                             << varname << " (" << t
                             << (has_static_type ? "" : ":dynamic_type")
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
        vector<Expr> min_var(dims);
        vector<Expr> max_var(dims);
        for (int i = 0; i < dims; i++) {
            string istr = std::to_string(i);
            // string extent_name = varname + ".extent." + istr;
            string stride_name = varname + ".stride." + istr;
            string min_name = varname + "_prefetch_" + pstr + "_min_" + istr;
            string max_name = varname + "_prefetch_" + pstr + "_max_" + istr;

#if 0  // TODO: Determine if the stride varname is defined - check not yet working
            string stride_name_required = stride_name + ".required";
            string stride_name_constrained = stride_name + ".constrained";
            if (scope.contains(stride_name_required)) {
                stride_name = stride_name_required;
            }
            if (scope.contains(stride_name_constrained)) {
                stride_name = stride_name_constrained;
            }

            if (scope.contains(stride_name)) {
                debug(dbg_prefetch2) << "  Found: " << stride_name << "\n";
            } else {
                do_prefetch = false;
                debug(dbg_prefetch2) << "  " << stride_name << " undefined\n";
                break;
            }
#endif
            stride_var[i] = Variable::make(Int(32), stride_name);
            min_var[i] = Variable::make(Int(32), min_name);
            max_var[i] = Variable::make(Int(32), max_name);

            // Record let assignments
            // except for stride, already defined elsewhere
            plets.push_back(make_pair(min_name, box[i].min));
            plets.push_back(make_pair(max_name, box[i].max));
        }

        // This box should not be prefetched
        if (!do_prefetch) {
            debug(dbg_prefetch1) << "  not prefetching " << varname << "\n";
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
        vector<Expr> args_prefetch(3);
        args_prefetch[0] = dims;
        if (has_static_type) {
            args_prefetch[1] = t.bytes();
        } else {
            // Use element size for inputs that don't have static types
            string elem_size_name = varname + ".elem_size";
            Expr elem_size_var = Variable::make(Int(32), elem_size_name);
            args_prefetch[1] = elem_size_var;
        }
        args_prefetch[2] = var_prefetch_buf;
        // TODO: Opt: Keep running sum of bytes prefetched on this sequence
        // TODO: Opt: Keep running sum of number of prefetch instructions issued
        // TODO       on this sequence? (to not exceed MAX_PREFETCH)
        // TODO: Opt: Generate control code for prefetch_buffer_t in Prefetch.cpp
        // TODO       Passing box info through a buffer_t results in ~30 additional stores/loads
        Stmt stmt_prefetch = Evaluate::make(Call::make(Int(32), Call::prefetch_buffer_t,
                              args_prefetch, Call::Intrinsic));

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

            // Guard to not prefetch past the end of the iteration space
            // TODO: Opt: Use original extent of loop in guard condition to
            // TODO       prefetch valid iterations that are otherwise skipped
            // TODO       with the current extent when loop is stripmined
            Expr pcond = likely((Variable::make(Int(32), op->name) + p.offset)
                                                < (op->min + op->extent - 1));
            Stmt pguard = IfThenElse::make(pcond, pbody);

            body = Block::make({pguard, body});

            debug(dbg_prefetch4) << pguard << "\n";
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

    void visit(const For *op) {
        Stmt body = op->body;

        string func_name = tuple_func(op->name);
        string ivar_name = tuple_var(op->name);
        vector<Prefetch> &prefetches = get_func(func_name).schedule().prefetches();

        if (prefetches.empty()) {
            debug(dbg_prefetch4) << "InjectPrefetch: " << op->name << " " << func_name << " " << ivar_name;
            debug(dbg_prefetch4) << " No prefetch directives in schedule\n";
        } else {
            debug(dbg_prefetch4) << "InjectPrefetch: " << op->name << " " << func_name << " " << ivar_name;
            debug(dbg_prefetch4) << " Found prefetch directive(s) in schedule\n";
        }

        for (const Prefetch &p : prefetches) {
            debug(dbg_prefetch4) << "InjectPrefetch: check ivar:" << p.var << "\n";
            if (p.var == ivar_name) {
                debug(dbg_prefetch4) << " Found directive matching " << ivar_name << "\n";
                debug(dbg_prefetch1) << " " << func_name
                                     << " prefetch(" << ivar_name << ", " << p.offset << ")\n";

                // Add in prefetch offset
                Expr var = Variable::make(Int(32), op->name);
                Interval prein(var + p.offset, var + p.offset);
                scope.push(op->name, prein);
                debug(dbg_prefetch5) << "For scope.push(" << op->name << ")\n";

                map<string, Box> boxes;
                boxes = boxes_required(body, scope);

                vector<pair<string, Expr>> plets;  // Prefetch let assignments
                vector<Stmt> pstmts;               // Prefetch stmt sequence
                debug(dbg_prefetch3) << "  boxes required:\n";
                for (auto &b : boxes) {
                    const string &varname = b.first;
                    Box &box = b.second;
                    prefetch_box(varname, box, plets, pstmts);
                }

                inject_prefetch_stmts(op, p, plets, pstmts, body);

                debug(dbg_prefetch5) << "For scope.pop(" << op->name << ")\n";
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
        dbg_prefetch1 = MAX(dbg_prefetch1 - dbg_level, 0);
        dbg_prefetch2 = MAX(dbg_prefetch2 - dbg_level, 0);
        dbg_prefetch3 = MAX(dbg_prefetch3 - dbg_level, 0);
        dbg_prefetch4 = MAX(dbg_prefetch4 - dbg_level, 0);
        dbg_prefetch5 = MAX(dbg_prefetch5 - dbg_level, 0);
    }

    return InjectPrefetch(env).mutate(s);
}

}
}
