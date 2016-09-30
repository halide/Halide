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

namespace {

class HasRealization : public IRVisitor {
public:
    std::string realization_name;
    bool has = false;

    HasRealization(std::string name) : realization_name(name) { }

private:
    using IRVisitor::visit;

    void visit(const Realize *op) {
        if (op->name == realization_name) {
            has = true;
        } else {
            IRVisitor::visit(op);
        }
    }
};

bool has_realization(Stmt s, const std::string &name) {
    HasRealization v(name);
    s.accept(&v);
    return v.has;
}

class InjectPrefetch : public IRMutator {
public:
    InjectPrefetch(const map<string, Function> &e)
        : env(e) { }

private:
    const map<string, Function> &env;
    Scope<Interval> intervals;          // Interval scope for boxes_required

private:
    using IRMutator::visit;

    // Strip down the tuple name, e.g. f.*.var into f
    string tuple_func(const string &name) {
        vector<string> v = split_string(name, ".");
        internal_assert(v.size() > 0);
        return v[0];
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
        Type t = UInt(8);       // default type
        map<string, Function>::const_iterator varit = env.find(varname);
        if (varit != env.end()) {
            Function varf = varit->second;
            if (varf.outputs()) {
                vector<Type> varts = varf.output_types();
                t = varts[0];
                has_static_type = true;
            }
        }

        return t;
    }

    // Generate the required prefetch code (lets and statements) for the
    // specified varname and box
    //
    Stmt prefetch_box(const string &varname, const Box &box)
    {
        vector<pair<string, Expr>> plets;  // Prefetch let assignments
        vector<Stmt> pstmts;               // Prefetch stmt sequence

        int dims = box.size();
        bool has_static_type = true;
        Type t = get_type(varname, has_static_type);
        string elem_size_name = varname + ".elem_size";
        Expr elem_size_bytes;

        if (has_static_type) {
            elem_size_bytes = t.bytes();
        } else {   // Use element size for inputs that don't have static types
            Expr elem_size_var = Variable::make(Int(32), elem_size_name);
            elem_size_bytes = elem_size_var;
        }

        std::ostringstream ss; ss << t;
        string type_name = ss.str();
        string pstr = unique_name('p');
        debug(1) << "  prefetch #" << pstr << ": "
                             << varname << " ("
                             << (has_static_type ? type_name : elem_size_name)
                             << ", dims:" << dims << ")\n";

        // TODO: Opt: check box if it should be prefetched?
        // TODO       - Only prefetch if varying by p.var?
        // TODO       - Don't prefetch if "small" all constant dimensions?
        // TODO         e.g. see: camera_pipe.cpp corrected matrix(4,3)

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

        // Create a buffer_t object for this prefetch.
        vector<Expr> args;

        Expr first_elem = Load::make(t, varname, 0, BufferPtr(), Parameter());
        args.push_back(Call::make(Handle(), Call::address_of, {first_elem}, Call::PureIntrinsic));
        args.push_back(make_zero(t));
        for (int i = 0; i < dims; i++) {
            args.push_back(min_var[i]);
            args.push_back(max_var[i] - min_var[i] + 1);
            args.push_back(stride_var[i]);
        }

        // Create the create_buffer_t call
        Expr prefetch_buf = Call::make(type_of<struct buffer_t *>(), Call::create_buffer_t,
                              args, Call::Intrinsic);

        // Create the prefetch call
        Expr num_elem = stride_var[dims-1] * extent_var[dims-1];
        vector<Expr> args_prefetch = {
            dims,
            elem_size_bytes,
            num_elem,
            prefetch_buf
        };
        Stmt stmt_prefetch = Evaluate::make(Call::make(Int(32), Call::prefetch_buffer_t,
                              args_prefetch, Call::Intrinsic));
        // TODO: Opt: Generate more control code for prefetch_buffer_t in Prefetch.cpp?
        // TODO       Passing box info through a buffer_t results in ~30 additional stores/loads

        pstmts.push_back(stmt_prefetch);

        // No guard needed, address range checked in prefetch runtime
        Stmt pbody = Block::make(pstmts);
        for (size_t i = plets.size(); i > 0; i--) {
            pbody = LetStmt::make(plets[i-1].first, plets[i-1].second, pbody);
        }

        return pbody;
    }

    void visit(const Let *op) {
        Interval in = bounds_of_expr_in_scope(op->value, intervals);
        intervals.push(op->name, in);
        IRMutator::visit(op);
        intervals.pop(op->name);
    }

    void visit(const LetStmt *op) {
        Interval in = bounds_of_expr_in_scope(op->value, intervals);
        intervals.push(op->name, in);
        IRMutator::visit(op);
        intervals.pop(op->name);
    }

    void visit(const For *op) {
        Stmt body = op->body;

        string func_name = tuple_func(op->name);
        const vector<Prefetch> &prefetches = get_func(func_name).schedule().prefetches();

        // Add loop variable to interval scope for any inner loop prefetch
        Expr loop_var = Variable::make(Int(32), op->name);
        Interval prein(loop_var, loop_var);
        intervals.push(op->name, prein);

        body = mutate(body);

        for (const Prefetch &p : prefetches) {
            if (!ends_with(op->name, "." + p.var)) {
                continue;
            }
            debug(1) << " " << func_name
                                 << " prefetch(" << p.var << ", " << p.offset << ")\n";

            // Add loop variable + prefetch offset to interval scope for box computation
            // Expr loop_var = Variable::make(Int(32), op->name);
            Interval prein(loop_var + p.offset, loop_var + p.offset);
            intervals.push(op->name, prein);

            map<string, Box> boxes = boxes_required(body, intervals);

            // TODO: Opt: prefetch the difference from previous iteration
            //            to the requested iteration (2 calls to boxes_required)

            for (auto &b : boxes) {
                const string &varname = b.first;
                const Box &box = b.second;
                if (has_realization(body, varname)) {
                    debug(1) << "  Info: not prefetching realized " << varname << "\n";
                    continue;
                }
                Stmt pbody = prefetch_box(varname, box);
                body = Block::make({pbody, body});
            }

            intervals.pop(op->name);
        }

        intervals.pop(op->name);

        stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
    }

};

} // Anonymous namespace

Stmt inject_prefetch(Stmt s, const std::map<std::string, Function> &env)
{
    debug(1) << "prefetch:\n";
    return InjectPrefetch(env).mutate(s);
}

}
}
