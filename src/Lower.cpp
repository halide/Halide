#include <iostream>
#include <set>
#include <sstream>
#include <algorithm>

#include "Lower.h"
#include "IROperator.h"
#include "Substitute.h"
#include "Function.h"
#include "Scope.h"
#include "Bounds.h"
#include "Simplify.h"
#include "IRPrinter.h"
#include "Debug.h"
#include "Tracing.h"
#include "Profiling.h"
#include "StorageFlattening.h"
#include "BoundsInference.h"
#include "VectorizeLoops.h"
#include "UnrollLoops.h"
#include "SlidingWindow.h"
#include "StorageFolding.h"
#include "RemoveTrivialForLoops.h"
#include "Deinterleave.h"
#include "DebugToFile.h"
#include "EarlyFree.h"
#include "UniquifyVariableNames.h"
#include "SkipStages.h"
#include "CSE.h"
#include "SpecializeClampedRamps.h"
#include "RemoveUndef.h"
#include "AllocationBoundsInference.h"
#include "Inline.h"
#include "Qualify.h"
#include "UnifyDuplicateLets.h"

namespace Halide {
namespace Internal {

using std::set;
using std::ostringstream;
using std::string;
using std::vector;
using std::map;
using std::pair;
using std::make_pair;

void lower_test() {

    Func f("f"), g("g"), h("h");
    Var x("x"), y("y"), xi, xo, yi, yo;

    h(x, y) = x-y;
    g(x, y) = h(x+1, y) + h(x-1, y);
    f(x, y) = g(x, y-1) + g(x, y+1);


    //f.split(x, xo, xi, 4).vectorize(xi).parallel(xo);
    //f.compute_root();

    //g.split(y, yo, yi, 2).unroll(yi);;
    g.store_at(f, y).compute_at(f, x);
    h.store_at(f, y).compute_at(f, y);

    Stmt result = lower(f.function());

    assert(result.defined() && "Lowering returned trivial function");

    std::cout << "Lowering test passed" << std::endl;
}

class ExprUsesVar : public IRVisitor {
    using IRVisitor::visit;

    string var;

    void visit(const Variable *v) {
        if (v->name == var) {
            result = true;
        }
    }
public:
    ExprUsesVar(const string &v) : var(v), result(false) {}
    bool result;
};
bool expr_uses_var(Expr e, const string &v) {
    ExprUsesVar uses(v);
    e.accept(&uses);
    return uses.result;
}


namespace {
// A structure representing a containing LetStmt or For loop. Used in
// build_provide_loop_nest below.
struct Container {
    int dim_idx; // index in the dims list. -1 for let statements.
    string name;
    Expr value;
};
}

// Build a loop nest about a provide node using a schedule
Stmt build_provide_loop_nest(Function f,
                             string prefix,
                             const vector<Expr> &site,
                             const vector<Expr> &values,
                             const Schedule &s,
                             bool is_update) {

    // We'll build it from inside out, starting from a store node,
    // then wrapping it in for loops.

    // Make the (multi-dimensional multi-valued) store node.
    assert(!values.empty());
    Stmt stmt = Provide::make(f.name(), values, site);

    // The dimensions for which we have a known static size.
    map<string, Expr> known_size_dims;
    // First hunt through the bounds for them.
    for (size_t i = 0; i < s.bounds.size(); i++) {
        known_size_dims[s.bounds[i].var] = s.bounds[i].extent;
    }

    vector<Schedule::Split> splits = s.splits;

    // Rebalance the split tree to make the outermost split first.
    for (size_t i = 0; i < splits.size(); i++) {
        for (size_t j = i+1; j < splits.size(); j++) {

            // Given two splits:
            // X  ->  a * Xo  + Xi
            // (splits stuff other than Xo, including Xi)
            // Xo ->  b * Xoo + Xoi

            // Re-write to:
            // X  -> ab * Xoo + s0
            // s0 ->  a * Xoi + Xi
            // (splits on stuff other than Xo, including Xi)

            // The name Xo went away, because it was legal for it to
            // be X before, but not after.

            Schedule::Split &first = splits[i];
            Schedule::Split &second = splits[j];
            if (first.outer == second.old_var) {
                assert(!second.is_rename() && "Rename of derived variable found in splits list. This should never happen.");
                second.old_var = unique_name('s');
                first.outer   = second.outer;
                second.outer  = second.inner;
                second.inner  = first.inner;
                first.inner   = second.old_var;
                Expr f = simplify(first.factor * second.factor);
                second.factor = first.factor;
                first.factor  = f;
                // Push the second split back to just after the first
                for (size_t k = j; k > i+1; k--) {
                    std::swap(splits[k], splits[k-1]);
                }
            }
        }
    }

    // Define the function args in terms of the loop variables using the splits
    map<string, pair<string, Expr> > base_values;
    for (size_t i = 0; i < splits.size(); i++) {
        const Schedule::Split &split = splits[i];
        Expr outer = Variable::make(Int(32), prefix + split.outer);
        if (split.is_split()) {
            Expr inner = Variable::make(Int(32), prefix + split.inner);
            Expr old_max = Variable::make(Int(32), prefix + split.old_var + ".loop_max");
            Expr old_min = Variable::make(Int(32), prefix + split.old_var + ".loop_min");

            known_size_dims[split.inner] = split.factor;

            Expr base = outer * split.factor + old_min;

            map<string, Expr>::iterator iter = known_size_dims.find(split.old_var);
            if ((iter != known_size_dims.end()) &&
                is_zero(simplify(iter->second % split.factor))) {

                // We have proved that the split factor divides the
                // old extent. No need to adjust the base.
                known_size_dims[split.outer] = iter->second / split.factor;
            } else if (!is_update) {
                // Adjust the base downwards to not compute off the
                // end of the realization.

                base = Min::make(base, old_max + (1 - split.factor));

            }

            string base_name = prefix + split.inner + ".base";
            Expr base_var = Variable::make(Int(32), base_name);
            //stmt = LetStmt::make(prefix + split.old_var, base_var + inner, stmt);
            stmt = substitute(prefix + split.old_var, base_var + inner, stmt);

            // Don't put the let here, put it just inside the loop over outer
            stmt = LetStmt::make(base_name, base, stmt);

        } else if (split.is_fuse()) {
            // Define the inner and outer in terms of the fused var
            Expr fused = Variable::make(Int(32), prefix + split.old_var);
            Expr inner_min = Variable::make(Int(32), prefix + split.inner + ".loop_min");
            Expr outer_min = Variable::make(Int(32), prefix + split.outer + ".loop_min");
            Expr inner_extent = Variable::make(Int(32), prefix + split.inner + ".loop_extent");

            Expr inner = fused % inner_extent + inner_min;
            Expr outer = fused / inner_extent + outer_min;

            //stmt = LetStmt::make(prefix + split.inner, inner, stmt);
            //stmt = LetStmt::make(prefix + split.outer, outer, stmt);
            stmt = substitute(prefix + split.inner, inner, stmt);
            stmt = substitute(prefix + split.outer, outer, stmt);

        } else {
            // stmt = LetStmt::make(prefix + split.old_var, outer, stmt);
            stmt = substitute(prefix + split.old_var, outer, stmt);
        }
    }

    // All containing lets and fors. Outermost first.
    vector<Container> nest;

    // Put the desired loop nest into the containers vector.
    for (int i = (int)s.dims.size() - 1; i >= 0; i--) {
        const Schedule::Dim &dim = s.dims[i];
        Container c = {i, prefix + dim.var, Expr()};
        nest.push_back(c);
    }

    // Strip off the lets into the containers vector.
    while (const LetStmt *let = stmt.as<LetStmt>()) {
        Container c = {-1, let->name, let->value};
        nest.push_back(c);
        stmt = let->body;
    }

    // Resort the containers vector so that lets are as far outwards
    // as possible. Use reverse insertion sort. Start at the first letstmt.
    for (int i = (int)s.dims.size(); i < (int)nest.size(); i++) {
        // Only push up LetStmts.
        assert(nest[i].value.defined());

        for (int j = i-1; j >= 0; j--) {
            // Try to push it up by one.
            assert(nest[j+1].value.defined());
            if (!expr_uses_var(nest[j+1].value, nest[j].name)) {
                std::swap(nest[j+1], nest[j]);
            } else {
                break;
            }
        }
    }

    // Rewrap the statement in the containing lets and fors.
    for (int i = (int)nest.size() - 1; i >= 0; i--) {
        if (nest[i].value.defined()) {
            stmt = LetStmt::make(nest[i].name, nest[i].value, stmt);
        } else {
            const Schedule::Dim &dim = s.dims[nest[i].dim_idx];
            Expr min = Variable::make(Int(32), nest[i].name + ".loop_min");
            Expr extent = Variable::make(Int(32), nest[i].name + ".loop_extent");
            stmt = For::make(nest[i].name, min, extent, dim.for_type, stmt);
        }
    }

    /*
    // Build the loop nest
    for (size_t i = 0; i < s.dims.size(); i++) {
        const Schedule::Dim &dim = s.dims[i];
        Expr min = Variable::make(Int(32), prefix + dim.var + ".loop_min");
        Expr extent = Variable::make(Int(32), prefix + dim.var + ".loop_extent");
        stmt = For::make(prefix + dim.var, min, extent, dim.for_type, stmt);
    }
    */

    // Define the bounds on the split dimensions using the bounds
    // on the function args
    for (size_t i = splits.size(); i > 0; i--) {
        const Schedule::Split &split = splits[i-1];
        Expr old_var_extent = Variable::make(Int(32), prefix + split.old_var + ".loop_extent");
        Expr old_var_max = Variable::make(Int(32), prefix + split.old_var + ".loop_max");
        Expr old_var_min = Variable::make(Int(32), prefix + split.old_var + ".loop_min");
        if (split.is_split()) {
            Expr inner_extent = split.factor;
            Expr outer_extent = (old_var_max - old_var_min + split.factor)/split.factor;
            stmt = LetStmt::make(prefix + split.inner + ".loop_min", 0, stmt);
            stmt = LetStmt::make(prefix + split.inner + ".loop_max", inner_extent-1, stmt);
            stmt = LetStmt::make(prefix + split.inner + ".loop_extent", inner_extent, stmt);
            stmt = LetStmt::make(prefix + split.outer + ".loop_min", 0, stmt);
            stmt = LetStmt::make(prefix + split.outer + ".loop_max", outer_extent-1, stmt);
            stmt = LetStmt::make(prefix + split.outer + ".loop_extent", outer_extent, stmt);
        } else if (split.is_fuse()) {
            // Define bounds on the fused var using the bounds on the inner and outer
            Expr inner_extent = Variable::make(Int(32), prefix + split.inner + ".loop_extent");
            Expr outer_extent = Variable::make(Int(32), prefix + split.outer + ".loop_extent");
            Expr fused_extent = inner_extent * outer_extent;
            stmt = LetStmt::make(prefix + split.old_var + ".loop_min", 0, stmt);
            stmt = LetStmt::make(prefix + split.old_var + ".loop_max", fused_extent - 1, stmt);
            stmt = LetStmt::make(prefix + split.old_var + ".loop_extent", fused_extent, stmt);
        } else {
            // rename
            stmt = LetStmt::make(prefix + split.outer + ".loop_min", old_var_min, stmt);
            stmt = LetStmt::make(prefix + split.outer + ".loop_max", old_var_max, stmt);
            stmt = LetStmt::make(prefix + split.outer + ".loop_extent", old_var_extent, stmt);
        }
    }

    // Define the loop mins and extents in terms of the mins and maxs produced by bounds inference
    for (size_t i = 0; i < f.args().size(); i++) {
        string var = prefix + f.args()[i];
        Expr max = Variable::make(Int(32), var + ".max");
        Expr min = Variable::make(Int(32), var + ".min"); // Inject instance name here? (compute instance names during lowering)
        stmt = LetStmt::make(var + ".loop_extent",
                             (max + 1) - min,
                             stmt);
        stmt = LetStmt::make(var + ".loop_min", min, stmt);
        stmt = LetStmt::make(var + ".loop_max", max, stmt);
    }

    return stmt;
}

// Turn a function into a loop nest that computes it. It will
// refer to external vars of the form function_name.arg_name.min
// and function_name.arg_name.extent to define the bounds over
// which it should be realized. It will compute at least those
// bounds (depending on splits, it may compute more). This loop
// won't do any allocation.
Stmt build_produce(Function f) {

    if (f.has_extern_definition()) {
        // Call the external function

        // Build an argument list
        vector<Expr> extern_call_args;
        const vector<ExternFuncArgument> &args = f.extern_arguments();

        const string &extern_name = f.extern_function_name();

        vector<pair<string, Expr> > lets;

        // Iterate through all of the input args to the extern
        // function building a suitable argument list for the
        // extern function call.
        for (size_t j = 0; j < args.size(); j++) {
            if (args[j].is_expr()) {
                extern_call_args.push_back(args[j].expr);
            } else if (args[j].is_func()) {
                Function input(args[j].func);
                for (int k = 0; k < input.outputs(); k++) {
                    string name = input.name();
                    if (input.outputs() > 1) {
                        name += "." + int_to_string(k);
                    }

                    vector<Expr> top_left;
                    for (int i = 0; i < input.dimensions(); i++) {
                        string d = int_to_string(i);
                        top_left.push_back(Variable::make(Int(32), input.name() + ".min." + d));
                    }
                    Expr host_ptr = Call::make(input, top_left, k);
                    host_ptr = Call::make(Handle(), Call::address_of, vec(host_ptr), Call::Intrinsic);

                    vector<Expr> buffer_args(2);
                    buffer_args[0] = host_ptr;
                    buffer_args[1] = input.output_types()[k].bytes();
                    for (int i = 0; i < input.dimensions(); i++) {
                        string d = int_to_string(i);
                        buffer_args.push_back(Variable::make(Int(32), input.name() + ".min." + d));
                        buffer_args.push_back(Variable::make(Int(32), input.name() + ".extent." + d));
                        buffer_args.push_back(Variable::make(Int(32), input.name() + ".stride." + d));
                    }

                    Expr buf = Call::make(Handle(), Call::create_buffer_t,
                                          buffer_args, Call::Intrinsic);

                    name += ".tmp_buffer";
                    lets.push_back(make_pair(name, buf));
                    extern_call_args.push_back(Variable::make(Handle(), name));
                }
            } else if (args[j].is_buffer()) {
                Buffer b = args[j].buffer;
                Parameter p(b.type(), true, b.name());
                p.set_buffer(b);
                Expr buf = Variable::make(Handle(), b.name() + ".buffer", p);
                extern_call_args.push_back(buf);
            } else if (args[j].is_image_param()) {
                Parameter p = args[j].image_param;
                Expr buf = Variable::make(Handle(), p.name() + ".buffer", p);
                extern_call_args.push_back(buf);
            } else {
                assert(false && "Bad ExternFuncArgument type");
            }
        }

        // Make the buffer_ts representing the output. They
        // all use the same size, but have differing types.
        string stride_name = f.name();
        if (f.outputs() > 1) {
            stride_name += ".0";
        }
        string stage_name = f.name() + ".s0.";

        for (int j = 0; j < f.outputs(); j++) {

            vector<Expr> buffer_args(2);

            vector<Expr> top_left;
            for (int k = 0; k < f.dimensions(); k++) {
                string var = stage_name + f.args()[k];
                top_left.push_back(Variable::make(Int(32), var + ".min"));
            }
            Expr host_ptr = Call::make(f, top_left, j);
            host_ptr = Call::make(Handle(), Call::address_of, vec(host_ptr), Call::Intrinsic);

            buffer_args[0] = host_ptr;
            buffer_args[1] = f.output_types()[j].bytes();
            for (int k = 0; k < f.dimensions(); k++) {
                string var = stage_name + f.args()[k];
                Expr min = Variable::make(Int(32), var + ".min");
                Expr max = Variable::make(Int(32), var + ".max");
                Expr stride = Variable::make(Int(32), stride_name + ".stride." + int_to_string(k));
                buffer_args.push_back(min);
                buffer_args.push_back(max - min + 1);
                buffer_args.push_back(stride);
            }

            Expr output_buffer_t = Call::make(Handle(), Call::create_buffer_t,
                                              buffer_args, Call::Intrinsic);

            string buf_name = f.name() + "." + int_to_string(j) + ".tmp_buffer";
            extern_call_args.push_back(Variable::make(Handle(), buf_name));
            lets.push_back(make_pair(buf_name, output_buffer_t));
        }

        // Make the extern call
        Expr e = Call::make(Int(32), extern_name,
                            extern_call_args, Call::Extern);
        // Check if it succeeded
        Stmt check = AssertStmt::make(EQ::make(e, 0), "Call to external func " +
                                      extern_name + " returned non-zero value");

        for (size_t i = 0; i < lets.size(); i++) {
            check = LetStmt::make(lets[i].first, lets[i].second, check);
        }

        return check;
    } else {

        string prefix = f.name() + ".s0.";

        // Compute the site to store to as the function args
        vector<Expr> site;

        vector<Expr> values(f.values().size());
        for (size_t i = 0; i < values.size(); i++) {
            values[i] = qualify(prefix, f.values()[i]);
        }

        for (size_t i = 0; i < f.args().size(); i++) {
            site.push_back(Variable::make(Int(32), prefix + f.args()[i]));
        }

        return build_provide_loop_nest(f, prefix, site, values, f.schedule(), false);
    }
}

// Build the loop nests that update a function (assuming it's a reduction).
vector<Stmt> build_update(Function f) {

    vector<Stmt> updates;

    for (size_t i = 0; i < f.reductions().size(); i++) {
        ReductionDefinition r = f.reductions()[i];

        string prefix = f.name() + ".s" + int_to_string(i+1) + ".";

        vector<Expr> site(r.args.size());
        vector<Expr> values(r.values.size());
        for (size_t i = 0; i < values.size(); i++) {
            Expr v = r.values[i];
            v = qualify(prefix, v);
            values[i] = v;
        }

        for (size_t i = 0; i < r.args.size(); i++) {
            Expr s = r.args[i];
            s = qualify(prefix, s);
            site[i] = s;
            debug(2) << "Reduction site " << i << " = " << s << "\n";
        }

        Stmt loop = build_provide_loop_nest(f, prefix, site, values, r.schedule, true);

        // Now define the bounds on the reduction domain
        if (r.domain.defined()) {
            const vector<ReductionVariable> &dom = r.domain.domain();
            for (size_t i = 0; i < dom.size(); i++) {
                string p = prefix + dom[i].var;
                Expr rmin = Variable::make(Int(32), p + ".min");
                Expr rmax = Variable::make(Int(32), p + ".max");
                loop = LetStmt::make(p + ".loop_min", rmin, loop);
                loop = LetStmt::make(p + ".loop_max", rmax, loop);
                loop = LetStmt::make(p + ".loop_extent", rmax - rmin + 1, loop);
            }
        }

        updates.push_back(loop);
    }

    return updates;
}

pair<Stmt, Stmt> build_production(Function func) {
    Stmt produce = build_produce(func);
    vector<Stmt> updates = build_update(func);

    // Build it from the last stage backwards.
    Stmt merged_updates;
    for (size_t s = updates.size(); s > 0; s--) {
        merged_updates = Block::make(updates[s-1], merged_updates);
    }
    return make_pair(produce, merged_updates);
}

// A schedule may include explicit bounds on some dimension. This
// injects let statements that set those bounds, and assertions that
// check that those bounds are sufficiently large to cover the
// inferred bounds required.
Stmt inject_explicit_bounds(Stmt body, Function func) {
    // Inject any explicit bounds
    const Schedule &s = func.schedule();
    for (size_t stage = 0; stage <= func.reductions().size(); stage++) {
        for (size_t i = 0; i < s.bounds.size(); i++) {
            Schedule::Bound b = s.bounds[i];
            Expr max_val = (b.extent + b.min) - 1;
            Expr min_val = b.min;
            string prefix = func.name() + ".s" + int_to_string(stage) + "." + b.var;
            string min_name = prefix + ".min_unbounded";
            string max_name = prefix + ".max_unbounded";
            Expr min_var = Variable::make(Int(32), min_name);
            Expr max_var = Variable::make(Int(32), max_name);
            Expr check = (min_val <= min_var) && (max_val >= max_var);
            string error_msg = "Bounds given for " + b.var + " in " + func.name() + " don't cover required region";

            // bounds inference has already respected these values for us
            //body = LetStmt::make(prefix + ".min", min_val, body);
            //body = LetStmt::make(prefix + ".max", max_val, body);

            body = Block::make(AssertStmt::make(check, error_msg), body);
        }
    }

    return body;
}

class IsCalledInStmt : public IRVisitor {
    string func;

    using IRVisitor::visit;

    void visit(const Call *op) {
        IRVisitor::visit(op);
        if (op->name == func) result = true;
    }

public:
    bool result;
    IsCalledInStmt(Function f) : func(f.name()), result(false) {
    }

};

bool function_is_called_in_stmt(Function f, Stmt s) {
    IsCalledInStmt is_called(f);
    s.accept(&is_called);
    return is_called.result;
}

// Inject the allocation and realization of a function into an
// existing loop nest using its schedule
class InjectRealization : public IRMutator {
public:
    const Function &func;
    bool found_store_level, found_compute_level;

    InjectRealization(const Function &f) :
        func(f), found_store_level(false), found_compute_level(false) {}
private:

    string producing;

    Stmt build_pipeline(Stmt s) {
        pair<Stmt, Stmt> realization = build_production(func);
        return Pipeline::make(func.name(), realization.first, realization.second, s);
    }

    Stmt build_realize(Stmt s) {
        Region bounds;
        string name = func.name();
        for (int i = 0; i < func.dimensions(); i++) {
            string arg = func.args()[i];
            Expr min = Variable::make(Int(32), name + "." + arg + ".min_realized");
            Expr extent = Variable::make(Int(32), name + "." + arg + ".extent_realized");
            bounds.push_back(Range(min, extent));
        }

        s = Realize::make(name, func.output_types(), bounds, s);

        // This is also the point at which we inject explicit bounds
        // for this realization.
        return inject_explicit_bounds(s, func);
    }

    using IRMutator::visit;

    void visit(const Pipeline *op) {
        string old = producing;
        producing = op->name;
        Stmt produce = mutate(op->produce);
        Stmt update;
        if (op->update.defined()) {
            update = mutate(op->update);
        }
        producing = old;
        Stmt consume = mutate(op->consume);

        if (produce.same_as(op->produce) &&
            update.same_as(op->update) &&
            consume.same_as(op->consume)) {
            stmt = op;
        } else {
            stmt = Pipeline::make(op->name, produce, update, consume);
        }
    }

    void visit(const For *for_loop) {
        debug(3) << "InjectRealization of " << func.name() << " entering for loop over " << for_loop->name << "\n";
        const Schedule::LoopLevel &compute_level = func.schedule().compute_level;
        const Schedule::LoopLevel &store_level = func.schedule().store_level;

        Stmt body = for_loop->body;

        // Dig through any let statements
        vector<pair<string, Expr> > lets;
        while (const LetStmt *l = body.as<LetStmt>()) {
            lets.push_back(make_pair(l->name, l->value));
            body = l->body;
        }

        // Can't schedule extern things inside a vector for loop
        if (func.has_extern_definition() &&
            func.schedule().compute_level.is_inline() &&
            for_loop->for_type == For::Vectorized &&
            function_is_called_in_stmt(func, for_loop)) {

            // If we're trying to inline an extern function, schedule it here and bail out
            debug(2) << "Injecting realization of " << func.name() << " around node " << Stmt(for_loop) << "\n";
            stmt = build_realize(build_pipeline(for_loop));
            found_store_level = found_compute_level = true;
            return;
        }

        body = mutate(body);

        if (compute_level.match(for_loop->name)) {
            debug(3) << "Found compute level\n";
            if (function_is_called_in_stmt(func, body)) {
                body = build_pipeline(body);
            }
            found_compute_level = true;
        }

        if (store_level.match(for_loop->name)) {
            debug(3) << "Found store level\n";
            assert(found_compute_level &&
                   "The compute loop level was not found within the store loop level!");

            if (function_is_called_in_stmt(func, body)) {
                body = build_realize(body);
            }

            found_store_level = true;
        }

        // Reinstate the let statements
        for (size_t i = lets.size(); i > 0; i--) {
            body = LetStmt::make(lets[i - 1].first, lets[i - 1].second, body);
        }

        if (body.same_as(for_loop->body)) {
            stmt = for_loop;
        } else {
            stmt = For::make(for_loop->name,
                           for_loop->min,
                           for_loop->extent,
                           for_loop->for_type,
                           body);
        }
    }

    // If we're an inline reduction or extern, we may need to inject a realization here
    virtual void visit(const Provide *op) {
        if (op->name != func.name() &&
            !func.is_pure() &&
            func.schedule().compute_level.is_inline() &&
            function_is_called_in_stmt(func, op)) {

            // Prefix all calls to func in op
            stmt = build_realize(build_pipeline(op));
            found_store_level = found_compute_level = true;
        } else {
            stmt = op;
        }
    }
};

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
            assert(iter->second.same_as(f) &&
                   "Can't compile a pipeline using multiple functions with same name");
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

/* Find all the externally referenced buffers in a stmt */
class FindBuffers : public IRGraphVisitor {
public:
    struct Result {
        Buffer image;
        Parameter param;
        Type type;
        int dimensions;
        Result() : dimensions(0) {}
    };

    map<string, Result> buffers;

    using IRGraphVisitor::visit;

    void visit(const Call *op) {
        IRGraphVisitor::visit(op);
        if (op->image.defined()) {
            Result r;
            r.image = op->image;
            r.type = op->type.element_of();
            r.dimensions = (int)op->args.size();
            buffers[op->name] = r;
        } else if (op->param.defined()) {
            Result r;
            r.param = op->param;
            r.type = op->type.element_of();
            r.dimensions = (int)op->args.size();
            buffers[op->name] = r;
        }
    }
};

/* Find all the externally referenced scalar parameters */
class FindParameters : public IRGraphVisitor {
public:
    map<string, Parameter> params;

    using IRGraphVisitor::visit;

    void visit(const Variable *op) {
        if (op->param.defined()) {
            params[op->name] = op->param;
        }
    }
};

void populate_environment(Function f, map<string, Function> &env, bool recursive = true) {
    map<string, Function>::const_iterator iter = env.find(f.name());
    if (iter != env.end()) {
        assert(iter->second.same_as(f) &&
               "Can't compile a pipeline using multiple functions with same name");
        return;
    }

    FindCalls calls;
    for (size_t i = 0; i < f.values().size(); i++) {
        f.values()[i].accept(&calls);
    }

    // Consider reductions
    for (size_t j = 0; j < f.reductions().size(); j++) {
        ReductionDefinition r = f.reductions()[j];
        for (size_t i = 0; i < r.values.size(); i++) {
            r.values[i].accept(&calls);
        }
        for (size_t i = 0; i < r.args.size(); i++) {
            r.args[i].accept(&calls);
        }

        ReductionDomain d = r.domain;
        if (r.domain.defined()) {
            for (size_t i = 0; i < d.domain().size(); i++) {
                d.domain()[i].min.accept(&calls);
                d.domain()[i].extent.accept(&calls);
            }
        }
    }

    // Consider extern calls
    if (f.has_extern_definition()) {
        for (size_t i = 0; i < f.extern_arguments().size(); i++) {
            ExternFuncArgument arg = f.extern_arguments()[i];
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

        for (map<string, Function>::const_iterator iter = calls.calls.begin();
             iter != calls.calls.end(); ++iter) {
            populate_environment(iter->second, env);
        }
    }
}

vector<string> realization_order(string output, const map<string, Function> &env, map<string, set<string> > &graph) {
    // Make a DAG representing the pipeline. Each function maps to the set describing its inputs.
    // Populate the graph
    for (map<string, Function>::const_iterator iter = env.begin();
         iter != env.end(); ++iter) {
        map<string, Function> calls;
        populate_environment(iter->second, calls, false);

        for (map<string, Function>::const_iterator j = calls.begin();
             j != calls.end(); ++j) {
            graph[iter->first].insert(j->first);
        }
    }

    vector<string> result;
    set<string> result_set;

    while (true) {
        // Find a function not in result_set, for which all its inputs are
        // in result_set. Stop when we reach the output function.
        bool scheduled_something = false;
        // Inject a dummy use of this var in case asserts are off.
        (void)scheduled_something;
        for (map<string, Function>::const_iterator iter = env.begin();
             iter != env.end(); ++iter) {
            const string &f = iter->first;
            if (result_set.find(f) == result_set.end()) {
                bool good_to_schedule = true;
                const set<string> &inputs = graph[f];
                for (set<string>::const_iterator i = inputs.begin();
                     i != inputs.end(); ++i) {
                    if (*i != f && result_set.find(*i) == result_set.end()) {
                        good_to_schedule = false;
                    }
                }

                if (good_to_schedule) {
                    scheduled_something = true;
                    result_set.insert(f);
                    result.push_back(f);
                    debug(4) << "Realization order: " << f << "\n";
                    if (f == output) return result;
                }
            }
        }

        assert(scheduled_something && "Stuck in a loop computing a realization order. Perhaps this pipeline has a loop?");
    }

}

Stmt create_initial_loop_nest(Function f) {
    // Generate initial loop nest
    pair<Stmt, Stmt> r = build_production(f);
    Stmt s = r.first;
    // This must be in a pipeline so that bounds inference understands the update step
    s = Pipeline::make(f.name(), r.first, r.second, AssertStmt::make(const_true(), "Dummy consume step"));
    return inject_explicit_bounds(s, f);
}

class ComputeLegalSchedules : public IRVisitor {
public:
    vector<Schedule::LoopLevel> loops_allowed;

    ComputeLegalSchedules(Function f) : func(f), found(false) {}

private:
    using IRVisitor::visit;

    vector<Schedule::LoopLevel> loops;
    Function func;
    bool found;

    void visit(const For *f) {
        f->min.accept(this);
        f->extent.accept(this);
        size_t first_dot = f->name.find('.');
        size_t last_dot = f->name.rfind('.');
        assert(first_dot != string::npos && last_dot != string::npos);
        string func = f->name.substr(0, first_dot);
        string var = f->name.substr(last_dot + 1);
        loops.push_back(Schedule::LoopLevel(func, var));
        f->body.accept(this);
        loops.pop_back();
    }

    void visit(const Call *c) {
        IRVisitor::visit(c);

        if (c->name == func.name()) {

            if (!found) {
                found = true;
                loops_allowed = loops;
            } else {
                // Take the common prefix between loops and loops allowed
                for (size_t i = 0; i < loops_allowed.size(); i++) {
                    if (i >= loops.size() || !loops[i].match(loops_allowed[i])) {
                        loops_allowed.resize(i);
                        break;
                    }
                }
            }
        }
    }
};

string schedule_to_source(Function f,
                          Schedule::LoopLevel store_at,
                          Schedule::LoopLevel compute_at) {
    std::ostringstream ss;
    ss << f.name();
    if (compute_at.is_inline()) {
        ss << ".compute_inline()";
    } else {
        if (!store_at.match(compute_at)) {
            if (store_at.is_root()) {
                ss << ".store_root()";
            } else {
                ss << ".store_at(" << store_at.func << ", " << store_at.var << ")";
            }
        }
        if (compute_at.is_root()) {
            ss << ".compute_root()";
        } else {
            ss << ".compute_at(" << compute_at.func << ", " << compute_at.var << ")";
        }
    }
    ss << ";";
    return ss.str();
}

void validate_schedule(Function f, Stmt s, bool is_output) {

    // If f is extern, check that none of its inputs are scheduled inline.
    if (f.has_extern_definition()) {
        for (size_t i = 0; i < f.extern_arguments().size(); i++) {
            ExternFuncArgument arg = f.extern_arguments()[i];
            if (arg.is_func()) {
                Function g(arg.func);
                if (g.schedule().compute_level.is_inline()) {
                    std::cerr << "Function " << g.name() << " cannot be scheduled to be computed inline, "
                              << "because it is used in the externally-computed function " << f.name() << "\n";
                    assert(false);
                }
            }
        }
    }

    // Check the rvars haven't been illegally reordered
    for (size_t i = 0; i < f.reductions().size(); i++) {
        const ReductionDefinition &r = f.reductions()[i];

        if (!r.domain.defined()) {
            // No reduction domain
            continue;
        }

        const vector<ReductionVariable> &rvars = r.domain.domain();
        const vector<Schedule::Dim> &dims = r.schedule.dims;

        // Look for the rvars in order
        size_t next = 0;
        for (size_t j = 0; j < dims.size() && next < rvars.size(); j++) {
            if (rvars[next].var == dims[j].var) {
                next++;
            }
        }

        if (next != rvars.size()) {
            std::cerr << "In function " << f.name() << " stage " << i
                      << ", the reduction variables have been illegally reordered.\n"
                      << "Correct order:";
            for (size_t j = 0; j < rvars.size(); j++) {
                std::cerr << " " << rvars[j].var;
            }
            std::cerr << "\nOrder specified by schedule:";
            for (size_t j = 0; j < dims.size(); j++) {
                std::cerr << " " << dims[j].var;
            }
            std::cerr << "\n";
            assert(false);
        }
    }

    Schedule::LoopLevel store_at = f.schedule().store_level;
    Schedule::LoopLevel compute_at = f.schedule().compute_level;
    // Inlining is always allowed
    if (store_at.is_inline() && compute_at.is_inline()) {
        return;
    }

    if (is_output) {
        if ((store_at.is_inline() || store_at.is_root()) &&
            (compute_at.is_inline() || compute_at.is_root())) {
            return;
        } else {
            std::cerr << "Function " << f.name() << " is the output, so must"
                      << " be scheduled compute_root (which is the default).\n";
            assert(false);
        }
    }

    // Otherwise inspect the uses to see what's ok.
    ComputeLegalSchedules legal(f);
    s.accept(&legal);

    bool store_at_ok = false, compute_at_ok = false;
    const vector<Schedule::LoopLevel> &loops = legal.loops_allowed;
    for (size_t i = 0; i < loops.size(); i++) {
        if (loops[i].match(store_at)) {
            store_at_ok = true;
        }
        if (loops[i].match(compute_at)) {
            compute_at_ok = store_at_ok;
        }
    }

    if (!store_at_ok || !compute_at_ok) {
        std::cerr << "Function " << f.name() << " is computed and stored in the following invalid location:\n"
                  << schedule_to_source(f, store_at, compute_at) << "\n"
                  << "Legal locations for this function are:\n";
        for (size_t i = 0; i < loops.size(); i++) {
            for (size_t j = i; j < loops.size(); j++) {
                std::cerr << schedule_to_source(f, loops[i], loops[j]) << "\n";
            }
        }
        assert(false);
    }
}

Stmt schedule_functions(Stmt s, const vector<string> &order,
                        const map<string, Function> &env,
                        const map<string, set<string> > &graph) {

    // Inject a loop over root to give us a scheduling point
    string root_var = Schedule::LoopLevel::root().func + "." + Schedule::LoopLevel::root().var;
    s = For::make(root_var, 0, 1, For::Serial, s);

    for (size_t i = order.size(); i > 0; i--) {
        Function f = env.find(order[i-1])->second;

        validate_schedule(f, s, i == order.size());

        // We don't actually want to schedule the output function here.
        if (i == order.size()) continue;

        if (f.has_pure_definition() &&
            !f.has_reduction_definition() &&
            f.schedule().compute_level.is_inline()) {
            debug(1) << "Inlining " << order[i-1] << '\n';
            s = inline_function(s, f);
        } else {
            debug(1) << "Injecting realization of " << order[i-1] << '\n';
            InjectRealization injector(f);
            s = injector.mutate(s);
            assert(injector.found_store_level && injector.found_compute_level);
        }
        debug(2) << s << '\n';
    }

    // We can remove the loop over root now
    const For *root_loop = s.as<For>();
    assert(root_loop);

    return root_loop->body;
}

// Insert checks to make sure that parameters are within their
// declared range.
Stmt add_parameter_checks(Stmt s) {
    // First, find all the parameters
    FindParameters finder;
    s.accept(&finder);

    map<string, Expr> replace_with_constrained;
    vector<pair<string, Expr> > lets;
    vector<Expr> asserts;

    // Make constrained versions of the params
    for (map<string, Parameter>::iterator iter = finder.params.begin();
         iter != finder.params.end(); iter++) {
        Parameter param = iter->second;

        if (!param.is_buffer() &&
            (param.get_min_value().defined() ||
             param.get_max_value().defined())) {

            string constrained_name = iter->first + ".constrained";

            Expr constrained_var = Variable::make(param.type(), constrained_name);
            Expr constrained_value = Variable::make(param.type(), iter->first, param);
            replace_with_constrained[iter->first] = constrained_var;
            if (param.get_min_value().defined()) {
                asserts.push_back(constrained_value >= param.get_min_value());
                constrained_value = max(constrained_value, param.get_min_value());
            }

            if (param.get_max_value().defined()) {
                Expr condition = constrained_value >= param.get_min_value();
                std::ostringstream oss;
                asserts.push_back(constrained_value <= param.get_max_value());
                constrained_value = min(constrained_value, param.get_max_value());
            }

            lets.push_back(make_pair(constrained_name, constrained_value));
        }
    }

    // Replace the params with their constrained version in the rest of the pipeline
    s = substitute(replace_with_constrained, s);

    // Inject the let statements
    for (size_t i = 0; i < lets.size(); i++) {
        s = LetStmt::make(lets[i].first, lets[i].second, s);
    }

    // Inject the assert statements
    for (size_t i = 0; i < asserts.size(); i++) {
        std::ostringstream oss;
        oss << "Static bounds constraint on parameter violated: " << asserts[i];
        s = Block::make(AssertStmt::make(asserts[i], oss.str()), s);
    }

    return s;
}


// Insert checks to make sure a statement doesn't read out of bounds
// on inputs or outputs, and that the inputs and outputs conform to
// the format required (e.g. stride.0 must be 1). Returns two
// statements, the first one is the mutated statement with the checks
// inserted. The second is a piece of code which will rewrite the
// buffer_t sizes, mins, and strides in order to satisfy the
// requirements.
Stmt add_image_checks(Stmt s, Function f) {

    // First hunt for all the referenced buffers
    FindBuffers finder;
    s.accept(&finder);
    map<string, FindBuffers::Result> bufs = finder.buffers;

    // Add the output buffer(s)
    for (size_t i = 0; i < f.values().size(); i++) {
        FindBuffers::Result output_buffer;
        output_buffer.type = f.values()[i].type();
        output_buffer.param = f.output_buffers()[i];
        output_buffer.dimensions = f.dimensions();
        if (f.values().size() > 1) {
            bufs[f.name() + '.' + int_to_string(i)] = output_buffer;
        } else {
            bufs[f.name()] = output_buffer;
        }
    }

    map<string, Box> boxes = boxes_touched(s);

    // Now iterate through all the buffers, creating a list of lets
    // and a list of asserts.
    vector<pair<string, Expr> > lets_required;
    vector<pair<string, Expr> > lets_constrained;
    vector<pair<string, Expr> > lets_proposed;
    vector<Stmt> asserts_required;
    vector<Stmt> asserts_constrained;
    vector<Stmt> asserts_proposed;
    vector<Stmt> asserts_elem_size;
    vector<Stmt> buffer_rewrites;

    // Inject the code that conditionally returns if we're in inference mode
    Expr maybe_return_condition = const_false();

    // We're also going to apply the constraints to the required min
    // and extent. To do this we have to substitute all references to
    // the actual sizes of the input images in the constraints with
    // references to the required sizes.
    map<string, Expr> replace_with_required;

    for (map<string, FindBuffers::Result>::iterator iter = bufs.begin();
         iter != bufs.end(); ++iter) {
        const string &name = iter->first;

        for (int i = 0; i < 4; i++) {
            string dim = int_to_string(i);

            Expr min_required = Variable::make(Int(32), name + ".min." + dim + ".required");
            replace_with_required[name + ".min." + dim] = min_required;

            Expr extent_required = Variable::make(Int(32), name + ".extent." + dim + ".required");
            replace_with_required[name + ".extent." + dim] = extent_required;

            Expr stride_required = Variable::make(Int(32), name + ".stride." + dim + ".required");
            replace_with_required[name + ".stride." + dim] = stride_required;
        }
    }

    // We also want to build a map that lets us replace values passed
    // in with the constrained version. This is applied to the rest of
    // the lowered pipeline to take advantage of the constraints,
    // e.g. for constant folding.
    map<string, Expr> replace_with_constrained;

    for (map<string, FindBuffers::Result>::iterator iter = bufs.begin();
         iter != bufs.end(); ++iter) {
        const string &name = iter->first;
        Buffer &image = iter->second.image;
        Parameter &param = iter->second.param;
        Type type = iter->second.type;
        int dimensions = iter->second.dimensions;

        // Detect if this is one of the outputs of a multi-output pipeline.
        bool is_output_buffer = false;
        bool is_secondary_output_buffer = false;
        for (size_t i = 0; i < f.output_buffers().size(); i++) {
            if (param.defined() &&
                param.same_as(f.output_buffers()[i])) {
                is_output_buffer = true;
                if (i > 0) {
                    is_secondary_output_buffer = true;
                }
            }
        }

        // If we're one of multiple output buffers, we should use the
        // region inferred for the output Func.
        string buffer_name = is_output_buffer ? f.name() : name;

        Box touched = boxes[buffer_name];
        assert((int)(touched.size()) == dimensions);

        // An expression returning whether or not we're in inference mode
        Expr inference_mode = Variable::make(UInt(1), name + ".host_and_dev_are_null", param);

        maybe_return_condition = maybe_return_condition || inference_mode;

        // Come up with a name to refer to this buffer in the error messages
        string error_name = (is_output_buffer ? "Output" : "Input");
        error_name += " buffer " + name;

        // Check the elem size matches the internally-understood type
        {
            string elem_size_name = name + ".elem_size";
            Expr elem_size = Variable::make(Int(32), elem_size_name);
            int correct_size = type.bytes();
            ostringstream error_msg;
            error_msg << error_name << " has type " << type
                      << ", but elem_size of the buffer_t passed in is not " << correct_size;
            asserts_elem_size.push_back(AssertStmt::make(elem_size == correct_size, error_msg.str()));
        }

        // Check that the region passed in (after applying constraints) is within the region used
        debug(3) << "In image " << name << " region touched is:\n";

        for (int j = 0; j < dimensions; j++) {
            string dim = int_to_string(j);
            string actual_min_name = name + ".min." + dim;
            string actual_extent_name = name + ".extent." + dim;
            Expr actual_min = Variable::make(Int(32), actual_min_name);
            Expr actual_extent = Variable::make(Int(32), actual_extent_name);
            Expr min_required = touched[j].min;
            Expr extent_required = touched[j].max + 1 - touched[j].min;
            string error_msg_extent = error_name + " is accessed beyond the extent in dimension " + dim;
            string error_msg_min = error_name + " is accessed before the min in dimension " + dim;

            string min_required_name = name + ".min." + dim + ".required";
            string extent_required_name = name + ".extent." + dim + ".required";

            Expr min_required_var = Variable::make(Int(32), min_required_name);
            Expr extent_required_var = Variable::make(Int(32), extent_required_name);

            lets_required.push_back(make_pair(extent_required_name, extent_required));
            lets_required.push_back(make_pair(min_required_name, min_required));

            asserts_required.push_back(AssertStmt::make(actual_min <= min_required_var, error_msg_min));
            asserts_required.push_back(AssertStmt::make(actual_min + actual_extent >=
                                                        min_required_var + extent_required_var, error_msg_extent));

            // Come up with a required stride to use in bounds
            // inference mode. We don't assert it. It's just used to
            // apply the constraints to to come up with a proposed
            // stride. Strides actually passed in may not be in this
            // order (e.g if storage is swizzled relative to dimension
            // order).
            Expr stride_required;
            if (j == 0) {
                stride_required = 1;
            } else {
                string last_dim = int_to_string(j-1);
                stride_required = (Variable::make(Int(32), name + ".stride." + last_dim + ".required") *
                                   Variable::make(Int(32), name + ".extent." + last_dim + ".required"));
            }
            lets_required.push_back(make_pair(name + ".stride." + dim + ".required", stride_required));
        }

        // Create code that mutates the input buffers if we're in bounds inference mode.
        Expr buffer_name_expr = Variable::make(Handle(), name + ".buffer");
        vector<Expr> args = vec(buffer_name_expr, Expr(type.bits/8));
        for (int i = 0; i < dimensions; i++) {
            string dim = int_to_string(i);
            args.push_back(Variable::make(Int(32), name + ".min." + dim + ".proposed"));
            args.push_back(Variable::make(Int(32), name + ".extent." + dim + ".proposed"));
            args.push_back(Variable::make(Int(32), name + ".stride." + dim + ".proposed"));
        }
        Expr call = Call::make(UInt(1), Call::rewrite_buffer, args, Call::Intrinsic);
        Stmt rewrite = Evaluate::make(call);
        rewrite = IfThenElse::make(inference_mode, rewrite);
        buffer_rewrites.push_back(rewrite);

        // Build the constraints tests and proposed sizes.
        vector<pair<string, Expr> > constraints;
        for (int i = 0; i < dimensions; i++) {
            string dim = int_to_string(i);
            string min_name = name + ".min." + dim;
            string stride_name = name + ".stride." + dim;
            string extent_name = name + ".extent." + dim;

            Expr stride_constrained, extent_constrained, min_constrained;

            Expr stride_orig = Variable::make(Int(32), stride_name);
            Expr extent_orig = Variable::make(Int(32), extent_name);
            Expr min_orig    = Variable::make(Int(32), min_name);

            Expr stride_required = Variable::make(Int(32), stride_name + ".required");
            Expr extent_required = Variable::make(Int(32), extent_name + ".required");
            Expr min_required = Variable::make(Int(32), min_name + ".required");

            Expr stride_proposed = Variable::make(Int(32), stride_name + ".proposed");
            Expr extent_proposed = Variable::make(Int(32), extent_name + ".proposed");
            Expr min_proposed = Variable::make(Int(32), min_name + ".proposed");

            debug(2) << "Injecting constraints for " << name << "." << i << "\n";
            if (is_secondary_output_buffer) {
                // For multi-output (Tuple) pipelines, output buffers
                // beyond the first implicitly have their min and extent
                // constrained to match the first output.

                if (param.defined()) {
                    assert(!param.extent_constraint(i).defined() &&
                           !param.min_constraint(i).defined() &&
                           "Can't constrain the min or extent of an output buffer beyond the "
                           "first. They are implicitly constrained to have the same min and extent "
                           "as the first output buffer.");

                    stride_constrained = param.stride_constraint(i);
                } else if (image.defined() && (int)i < image.dimensions()) {
                    stride_constrained = image.stride(i);
                }

                min_constrained = Variable::make(Int(32), f.name() + ".0.min." + dim);
                extent_constrained = Variable::make(Int(32), f.name() + ".0.extent." + dim);
            } else if (image.defined() && (int)i < image.dimensions()) {
                stride_constrained = image.stride(i);
                extent_constrained = image.extent(i);
                min_constrained = image.min(i);
            } else if (param.defined()) {
                stride_constrained = param.stride_constraint(i);
                extent_constrained = param.extent_constraint(i);
                min_constrained = param.min_constraint(i);
            }

            if (stride_constrained.defined()) {
                // Come up with a suggested stride by passing the
                // required region through this constraint.
                constraints.push_back(make_pair(stride_name, stride_constrained));
                stride_constrained = substitute(replace_with_required, stride_constrained);
                lets_proposed.push_back(make_pair(stride_name + ".proposed", stride_constrained));
            } else {
                lets_proposed.push_back(make_pair(stride_name + ".proposed", stride_required));
            }

            if (min_constrained.defined()) {
                constraints.push_back(make_pair(min_name, min_constrained));
                min_constrained = substitute(replace_with_required, min_constrained);
                lets_proposed.push_back(make_pair(min_name + ".proposed", min_constrained));
            } else {
                lets_proposed.push_back(make_pair(min_name + ".proposed", min_required));
            }

            if (extent_constrained.defined()) {
                constraints.push_back(make_pair(extent_name, extent_constrained));
                extent_constrained = substitute(replace_with_required, extent_constrained);
                lets_proposed.push_back(make_pair(extent_name + ".proposed", extent_constrained));
            } else {
                lets_proposed.push_back(make_pair(extent_name + ".proposed", extent_required));
            }

            // In bounds inference mode, make sure the proposed
            // versions still satisfy the constraints.
            Expr check = ((min_proposed <= min_required) &&
                          (min_proposed + extent_proposed >=
                           min_required + extent_required));
            string error = "Applying the constraints to the required region made it smaller";
            asserts_proposed.push_back(AssertStmt::make((!inference_mode) || check, error));

            check = (stride_proposed >= stride_required);
            error = "Applying the constraints to the required stride made it smaller";
            asserts_proposed.push_back(AssertStmt::make((!inference_mode) || check, error));
        }

        // Assert all the conditions, and set the new values
        for (size_t i = 0; i < constraints.size(); i++) {
            Expr var = Variable::make(Int(32), constraints[i].first);
            Expr constrained_var = Variable::make(Int(32), constraints[i].first + ".constrained");
            Expr value = constraints[i].second;
            ostringstream error;
            error << "Static constraint violated: " << constraints[i].first << " == " << value;

            replace_with_constrained[constraints[i].first] = constrained_var;

            lets_constrained.push_back(make_pair(constraints[i].first + ".constrained", value));

            // Check the var passed in equals the constrained version (when not in inference mode)
            asserts_constrained.push_back(AssertStmt::make(var == constrained_var, error.str()));
        }
    }

    // Replace uses of the var with the constrained versions in the
    // rest of the program. We also need to respect the existence of
    // constrained versions during storage flattening and bounds
    // inference.
    s = substitute(replace_with_constrained, s);

    // Now we add a bunch of code to the top of the pipeline. This is
    // all in reverse order compared to execution, as we incrementally
    // prepending code.

    // Inject the code that checks the constraints are correct.
    for (size_t i = asserts_constrained.size(); i > 0; i--) {
        s = Block::make(asserts_constrained[i-1], s);
    }

    // Inject the code that checks for out-of-bounds access to the buffers.
    for (size_t i = asserts_required.size(); i > 0; i--) {
        s = Block::make(asserts_required[i-1], s);
    }

    // Inject the code that checks that elem_sizes are ok.
    for (size_t i = asserts_elem_size.size(); i > 0; i--) {
        s = Block::make(asserts_elem_size[i-1], s);
    }

    // Inject the code that returns early for inference mode.
    s = IfThenElse::make(!maybe_return_condition, s);

    // Inject the code that does the buffer rewrites for inference mode.
    for (size_t i = buffer_rewrites.size(); i > 0; i--) {
        s = Block::make(buffer_rewrites[i-1], s);
    }

    // Inject the code that checks the proposed sizes still pass the bounds checks
    for (size_t i = asserts_proposed.size(); i > 0; i--) {
        s = Block::make(asserts_proposed[i-1], s);
    }

    // Inject the code that defines the proposed sizes.
    for (size_t i = lets_proposed.size(); i > 0; i--) {
        s = LetStmt::make(lets_proposed[i-1].first, lets_proposed[i-1].second, s);
    }

    // Inject the code that defines the constrained sizes.
    for (size_t i = lets_constrained.size(); i > 0; i--) {
        s = LetStmt::make(lets_constrained[i-1].first, lets_constrained[i-1].second, s);
    }

    // Inject the code that defines the required sizes produced by bounds inference.
    for (size_t i = lets_required.size(); i > 0; i--) {
        s = LetStmt::make(lets_required[i-1].first, lets_required[i-1].second, s);
    }

    return s;
}

Stmt lower(Function f) {

    // Compute an environment
    map<string, Function> env;
    populate_environment(f, env);

    // Compute a realization order
    map<string, set<string> > graph;
    vector<string> order = realization_order(f.name(), env, graph);
    Stmt s = create_initial_loop_nest(f);

    debug(2) << "Initial statement: " << '\n' << s << '\n';
    s = schedule_functions(s, order, env, graph);
    debug(2) << "All realizations injected:\n" << s << '\n';

    debug(1) << "Injecting tracing...\n";
    s = inject_tracing(s, env, f);
    debug(2) << "Tracing injected:\n" << s << '\n';

    debug(1) << "Injecting profiling...\n";
    s = inject_profiling(s, f.name());
    debug(2) << "Profiling injected:\n" << s << '\n';

    debug(1) << "Adding checks for parameters\n";
    s = add_parameter_checks(s);
    debug(2) << "Parameter checks injected:\n" << s << '\n';

    // The checks will be in terms of the symbols defined by bounds
    // inference.
    debug(1) << "Adding checks for images\n";
    s = add_image_checks(s, f);
    debug(2) << "Image checks injected:\n" << s << '\n';

    // This pass injects nested definitions of variable names, so we
    // can't simplify statements from here until we fix them up. (We
    // can still simplify Exprs).
    debug(1) << "Performing computation bounds inference...\n";
    s = bounds_inference(s, order, env);
    debug(2) << "Computation bounds inference:\n" << s << '\n';

    debug(1) << "Performing sliding window optimization...\n";
    s = sliding_window(s, env);
    debug(2) << "Sliding window:\n" << s << '\n';

    debug(1) << "Performing allocation bounds inference...\n";
    s = allocation_bounds_inference(s, env);
    debug(2) << "Allocation bounds inference:\n" << s << '\n';

    // This uniquifies the variable names, so we're good to simplify
    // after this point. This lets later passes assume syntactic
    // equivalence means semantic equivalence.
    debug(1) << "Uniquifying variable names...\n";
    s = uniquify_variable_names(s);
    debug(2) << "Uniquified variable names: \n" << s << "\n\n";

    debug(1) << "Performing storage folding optimization...\n";
    s = storage_folding(s);
    debug(2) << "Storage folding:\n" << s << '\n';

    debug(1) << "Injecting debug_to_file calls...\n";
    s = debug_to_file(s, order[order.size()-1], env);
    debug(2) << "Injected debug_to_file calls:\n" << s << '\n';

    debug(1) << "Simplifying...\n"; // without removing dead lets, because storage flattening needs the strides
    s = simplify(s, false);
    debug(2) << "Simplified: \n" << s << "\n\n";

    debug(1) << "Dynamically skipping stages...\n";
    s = skip_stages(s, order);
    debug(2) << "Dynamically skipped stages: \n" << s << "\n\n";

    debug(1) << "Performing storage flattening...\n";
    s = storage_flattening(s, env);
    debug(2) << "Storage flattening: \n" << s << "\n\n";

    debug(1) << "Removing code that depends on undef values...\n";
    s = remove_undef(s);
    debug(2) << "Removed code that depends on undef values: \n" << s << "\n\n;";

    debug(1) << "Simplifying...\n";
    s = simplify(s);
    s = unify_duplicate_lets(s);
    s = remove_trivial_for_loops(s);
    debug(2) << "Simplified: \n" << s << "\n\n";

    debug(1) << "Unrolling...\n";
    s = unroll_loops(s);
    debug(2) << "Unrolled: \n" << s << "\n\n";

    debug(1) << "Simplifying...\n";
    s = simplify(s);
    debug(2) << "Simplified: \n" << s << "\n\n";

    debug(1) << "Vectorizing...\n";
    s = vectorize_loops(s);
    debug(2) << "Vectorized: \n" << s << "\n\n";

    debug(1) << "Simplifying...\n";
    s = simplify(s);
    debug(2) << "Simplified: \n" << s << "\n\n";

    debug(1) << "Specializing clamped ramps...\n";
    s = specialize_clamped_ramps(s);
    s = simplify(s);
    debug(2) << "Specialized clamped ramps: \n" << s << "\n\n";

    debug(1) << "Detecting vector interleavings...\n";
    s = rewrite_interleavings(s);
    debug(2) << "Rewrote vector interleavings: \n" << s << "\n\n";

    debug(1) << "Injecting early frees...\n";
    s = inject_early_frees(s);
    debug(2) << "Injected early frees: \n" << s << "\n\n";

    debug(1) << "Simplifying...\n";
    s = common_subexpression_elimination(s);
    s = simplify(s);
    debug(1) << "Simplified: \n" << s << "\n\n";

    return s;
}

}
}
