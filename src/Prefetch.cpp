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
    Stmt prefetch_box(const string &varname, const Box &box) {
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

        string varname_prefetch_buf = varname + "_prefetch_buf";
        Expr var_prefetch_buf = Variable::make(Int(32), varname_prefetch_buf);

        // Establish the variables for buffer strides, box min & max
        vector<Expr> args;

        Expr first_elem = Load::make(t, varname, 0, BufferPtr(), Parameter());
        args.push_back(Call::make(Handle(), Call::address_of, {first_elem}, Call::PureIntrinsic));
        args.push_back(elem_size_bytes);
        for (size_t i = 0; i < box.size(); i++) {
            string dim_name = std::to_string(i);
            Expr buffer_min = Variable::make(Int(32), varname + ".min." + dim_name);
            Expr buffer_extent = Variable::make(Int(32), varname + ".extent." + dim_name);
            Expr buffer_stride = Variable::make(Int(32), varname + ".stride." + dim_name);

            Expr buffer_max = buffer_min + buffer_extent - 1;

            Expr prefetch_min = clamp(box[i].min, buffer_min, buffer_max);
            Expr prefetch_max = clamp(box[i].max, buffer_min, buffer_max);

            args.push_back(prefetch_min);
            args.push_back(prefetch_max - prefetch_min + 1);
            args.push_back(buffer_stride);
        }

        // Create a call to prefetch_buffer_t.
        Expr prefetch_buf = Call::make(type_of<struct buffer_t *>(), Call::create_buffer_t,
                                       args, Call::Intrinsic);
        string prefetch_buf_name = "prefetch_" + varname + "_buf";
        Expr prefetch_buf_var = Variable::make(type_of<struct buffer_t *>(), prefetch_buf_name);
        Stmt prefetch = Evaluate::make(Call::make(Int(32), Call::prefetch_buffer_t,
                                                  {prefetch_buf_var}, Call::Intrinsic));
        return LetStmt::make(prefetch_buf_name, prefetch_buf, prefetch);
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

Stmt inject_prefetch(Stmt s, const std::map<std::string, Function> &env) {
    debug(1) << "prefetch:\n";
    return InjectPrefetch(env).mutate(s);
}

}
}
