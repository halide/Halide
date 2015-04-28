#include <iostream>
#include <set>
#include <sstream>
#include <algorithm>

#include "Lower.h"
#include "IROperator.h"
#include "IRMutator.h"
#include "Substitute.h"
#include "Function.h"
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
#include "RemoveDeadAllocations.h"
#include "Deinterleave.h"
#include "DebugToFile.h"
#include "EarlyFree.h"
#include "UniquifyVariableNames.h"
#include "SkipStages.h"
#include "CSE.h"
#include "PartitionLoops.h"
#include "RemoveUndef.h"
#include "AllocationBoundsInference.h"
#include "Inline.h"
#include "Qualify.h"
#include "UnifyDuplicateLets.h"
#include "Func.h"
#include "ExprUsesVar.h"
#include "FindCalls.h"
#include "InjectShaderIntrinsics.h"
#include "InjectOpenGLIntrinsics.h"
#include "FuseGPUThreadLoops.h"
#include "InjectHostDevBufferCopies.h"
#include "Memoization.h"
#include "VaryingAttributes.h"

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

    Stmt result = lower(f.function(), get_host_target());

    internal_assert(result.defined()) << "Lowering returned undefined Stmt";

    std::cout << "Lowering test passed" << std::endl;
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
    Stmt stmt = Provide::make(f.name(), values, site);

    // The dimensions for which we have a known static size.
    map<string, Expr> known_size_dims;
    // First hunt through the bounds for them.
    for (size_t i = 0; i < s.bounds().size(); i++) {
        known_size_dims[s.bounds()[i].var] = s.bounds()[i].extent;
    }
    // Then use any reduction domain.
    const ReductionDomain &rdom = s.reduction_domain();
    if (rdom.defined()) {
        for (size_t i = 0; i < rdom.domain().size(); i++) {
            known_size_dims[rdom.domain()[i].var] = rdom.domain()[i].extent;
        }
    }

    vector<Split> splits = s.splits();

    // Rebalance the split tree to make the outermost split first.
    for (size_t i = 0; i < splits.size(); i++) {
        for (size_t j = i+1; j < splits.size(); j++) {

            Split &first = splits[i];
            Split &second = splits[j];
            if (first.outer == second.old_var) {
                internal_assert(!second.is_rename())
                    << "Rename of derived variable found in splits list. This should never happen.";

                if (first.is_rename()) {
                    // Given a rename:
                    // X -> Y
                    // And a split:
                    // Y -> f * Z + W
                    // Coalesce into:
                    // X -> f * Z + W
                    second.old_var = first.old_var;
                    // Drop first entirely
                    for (size_t k = i; k < splits.size()-1; k++) {
                        splits[k] = splits[k+1];
                    }
                    splits.pop_back();
                    // Start processing this split from scratch,
                    // because we just clobbered it.
                    j = i+1;
                } else {
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

                    first.exact |= second.exact;
                    second.exact = first.exact;
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
    }

    // Define the function args in terms of the loop variables using the splits
    map<string, pair<string, Expr> > base_values;
    for (size_t i = 0; i < splits.size(); i++) {
        const Split &split = splits[i];
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
            } else if (split.exact) {
                // It's an exact split but we failed to prove that the
                // extent divides the factor. This is a problem.
                user_error << "When splitting " << split.old_var << " into "
                           << split.outer << " and " << split.inner << ", "
                           << "could not prove the split factor (" << split.factor << ") "
                           << "divides the extent of " << split.old_var
                           << " (" << iter->second << "). This is required when "
                           << "the split originates from an RVar.\n";
            } else if (!is_update) {
                // Adjust the base downwards to not compute off the
                // end of the realization.

                base = Min::make(likely(base), old_max + (1 - split.factor));

            }

            string base_name = prefix + split.inner + ".base";
            Expr base_var = Variable::make(Int(32), base_name);
            // Substitute in the new expression for the split variable ...
            stmt = substitute(prefix + split.old_var, base_var + inner, stmt);
            // ... but also define it as a let for the benefit of bounds inference.
            stmt = LetStmt::make(prefix + split.old_var, base_var + inner, stmt);
            stmt = LetStmt::make(base_name, base, stmt);

        } else if (split.is_fuse()) {
            // Define the inner and outer in terms of the fused var
            Expr fused = Variable::make(Int(32), prefix + split.old_var);
            Expr inner_min = Variable::make(Int(32), prefix + split.inner + ".loop_min");
            Expr outer_min = Variable::make(Int(32), prefix + split.outer + ".loop_min");
            Expr inner_extent = Variable::make(Int(32), prefix + split.inner + ".loop_extent");

            // If the inner extent is zero, the loop will never be
            // entered, but the bounds expressions lifted out might
            // contain divides or mods by zero. In the cases where
            // simplification of inner and outer matter, inner_extent
            // is a constant, so the max will simplify away.
            Expr factor = max(inner_extent, 1);
            Expr inner = fused % factor + inner_min;
            Expr outer = fused / factor + outer_min;

            stmt = substitute(prefix + split.inner, inner, stmt);
            stmt = substitute(prefix + split.outer, outer, stmt);
            stmt = LetStmt::make(prefix + split.inner, inner, stmt);
            stmt = LetStmt::make(prefix + split.outer, outer, stmt);

            // Maintain the known size of the fused dim if
            // possible. This is important for possible later splits.
            map<string, Expr>::iterator inner_dim = known_size_dims.find(split.inner);
            map<string, Expr>::iterator outer_dim = known_size_dims.find(split.outer);
            if (inner_dim != known_size_dims.end() &&
                outer_dim != known_size_dims.end()) {
                known_size_dims[split.old_var] = inner_dim->second*outer_dim->second;
            }

        } else {
            stmt = substitute(prefix + split.old_var, outer, stmt);
            stmt = LetStmt::make(prefix + split.old_var, outer, stmt);
        }
    }

    // All containing lets and fors. Outermost first.
    vector<Container> nest;

    // Put the desired loop nest into the containers vector.
    for (int i = (int)s.dims().size() - 1; i >= 0; i--) {
        const Dim &dim = s.dims()[i];
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
    for (int i = (int)s.dims().size(); i < (int)nest.size(); i++) {
        // Only push up LetStmts.
        internal_assert(nest[i].value.defined());

        for (int j = i-1; j >= 0; j--) {
            // Try to push it up by one.
            internal_assert(nest[j+1].value.defined());
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
            const Dim &dim = s.dims()[nest[i].dim_idx];
            Expr min = Variable::make(Int(32), nest[i].name + ".loop_min");
            Expr extent = Variable::make(Int(32), nest[i].name + ".loop_extent");
            stmt = For::make(nest[i].name, min, extent, dim.for_type, dim.device_api, stmt);
        }
    }

    // Define the bounds on the split dimensions using the bounds
    // on the function args
    for (size_t i = splits.size(); i > 0; i--) {
        const Split &split = splits[i-1];
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

    // Define the bounds on the outermost dummy dimension.
    {
        string o = prefix + Var::outermost().name();
        stmt = LetStmt::make(o + ".loop_min", 0, stmt);
        stmt = LetStmt::make(o + ".loop_max", 1, stmt);
        stmt = LetStmt::make(o + ".loop_extent", 1, stmt);
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

    // Make any specialized copies
    for (size_t i = s.specializations().size(); i > 0; i--) {
        Expr c = s.specializations()[i-1].condition;
        Schedule sched = s.specializations()[i-1].schedule;
        const EQ *eq = c.as<EQ>();
        const Variable *var = eq ? eq->a.as<Variable>() : c.as<Variable>();

        Stmt then_case =
            build_provide_loop_nest(f, prefix, site, values, sched, is_update);

        if (var && eq) {
            then_case = simplify_exprs(substitute(var->name, eq->b, then_case));
            Stmt else_case = stmt;
            if (eq->b.type().is_bool()) {
                else_case = simplify_exprs(substitute(var->name, !eq->b, else_case));
            }
            stmt = IfThenElse::make(c, then_case, else_case);
        } else if (var) {
            then_case = simplify_exprs(substitute(var->name, const_true(), then_case));
            Stmt else_case = simplify_exprs(substitute(var->name, const_false(), stmt));
            stmt = IfThenElse::make(c, then_case, else_case);
        } else {
            stmt = IfThenElse::make(c, then_case, stmt);
        }
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
                    string buf_name = input.name();
                    if (input.outputs() > 1) {
                        buf_name += "." + int_to_string(k);
                    }
                    buf_name += ".buffer";
                    Expr buffer = Variable::make(Handle(), buf_name);
                    extern_call_args.push_back(buffer);
                }
            } else if (args[j].is_buffer()) {
                Buffer b = args[j].buffer;
                Parameter p(b.type(), true, b.dimensions(), b.name());
                p.set_buffer(b);
                Expr buf = Variable::make(Handle(), b.name() + ".buffer", p);
                extern_call_args.push_back(buf);
            } else if (args[j].is_image_param()) {
                Parameter p = args[j].image_param;
                Expr buf = Variable::make(Handle(), p.name() + ".buffer", p);
                extern_call_args.push_back(buf);
            } else {
                internal_error << "Bad ExternFuncArgument type\n";
            }
        }

        // Grab the buffer_ts representing the output. If the store
        // level matches the compute level, then we can use the ones
        // already injected by allocation bounds inference. If it's
        // the output to the pipeline then it will similarly be in the
        // symbol table.
        if (f.schedule().store_level() == f.schedule().compute_level()) {
            for (int j = 0; j < f.outputs(); j++) {
                string buf_name = f.name();
                if (f.outputs() > 1) {
                    buf_name += "." + int_to_string(j);
                }
                buf_name += ".buffer";
                Expr buffer = Variable::make(Handle(), buf_name);
                extern_call_args.push_back(buffer);
            }
        } else {
            // Store level doesn't match compute level. Make an output
            // buffer just for this subregion.
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
        }

        // Make the extern call
        Expr e = Call::make(Int(32), extern_name,
                            extern_call_args, Call::Extern);
        string result_name = unique_name('t');
        Expr result = Variable::make(Int(32), result_name);
        // Check if it succeeded
        Expr error = Call::make(Int(32), "halide_error_extern_stage_failed",
                                vec<Expr>(extern_name, result), Call::Extern);
        Stmt check = AssertStmt::make(EQ::make(result, 0), error);
        check = LetStmt::make(result_name, e, check);

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

    for (size_t i = 0; i < f.updates().size(); i++) {
        UpdateDefinition r = f.updates()[i];

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
            debug(2) << "Update site " << i << " = " << s << "\n";
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
// injects assertions that check that those bounds are sufficiently
// large to cover the inferred bounds required.
Stmt inject_explicit_bounds(Stmt body, Function func) {
    const Schedule &s = func.schedule();
    for (size_t stage = 0; stage <= func.updates().size(); stage++) {
        for (size_t i = 0; i < s.bounds().size(); i++) {
            Bound b = s.bounds()[i];
            Expr max_val = (b.extent + b.min) - 1;
            Expr min_val = b.min;
            string prefix = func.name() + ".s" + int_to_string(stage) + "." + b.var;
            string min_name = prefix + ".min_unbounded";
            string max_name = prefix + ".max_unbounded";
            Expr min_var = Variable::make(Int(32), min_name);
            Expr max_var = Variable::make(Int(32), max_name);
            Expr check = (min_val <= min_var) && (max_val >= max_var);
            Expr error_msg = Call::make(Int(32), "halide_error_explicit_bounds_too_small",
                                        vec<Expr>(b.var, func.name(), min_val, max_val, min_var, max_var),
                                        Call::Extern);

            // bounds inference has already respected these values for us
            //body = LetStmt::make(prefix + ".min", min_val, body);
            //body = LetStmt::make(prefix + ".max", max_val, body);

            body = Block::make(AssertStmt::make(check, error_msg), body);
        }
    }

    return body;
}

class IsUsedInStmt : public IRVisitor {
    string func;

    using IRVisitor::visit;

    void visit(const Call *op) {
        IRVisitor::visit(op);
        if (op->name == func) result = true;
    }

    // A reference to the function's buffers counts as a use
    void visit(const Variable *op) {
        if (op->type == Handle() &&
            starts_with(op->name, func + ".") &&
            ends_with(op->name, ".buffer")) {
            result = true;
        }
    }

public:
    bool result;
    IsUsedInStmt(Function f) : func(f.name()), result(false) {
    }

};

bool function_is_used_in_stmt(Function f, Stmt s) {
    IsUsedInStmt is_called(f);
    s.accept(&is_called);
    return is_called.result;
}

// Inject the allocation and realization of a function into an
// existing loop nest using its schedule
class InjectRealization : public IRMutator {
public:
    const Function &func;
    bool found_store_level, found_compute_level;
    const Target &target;

    InjectRealization(const Function &f, const Target &t) :
        func(f), found_store_level(false), found_compute_level(false), target(t) {}
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

        s = Realize::make(name, func.output_types(), bounds, const_true(), s);

        // This is also the point at which we inject explicit bounds
        // for this realization.
        if (target.has_feature(Target::NoAsserts)) {
            return s;
        } else {
            return inject_explicit_bounds(s, func);
        }
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
        const LoopLevel &compute_level = func.schedule().compute_level();
        const LoopLevel &store_level = func.schedule().store_level();

        Stmt body = for_loop->body;

        // Dig through any let statements
        vector<pair<string, Expr> > lets;
        while (const LetStmt *l = body.as<LetStmt>()) {
            lets.push_back(make_pair(l->name, l->value));
            body = l->body;
        }

        // Can't schedule extern things inside a vector for loop
        if (func.has_extern_definition() &&
            func.schedule().compute_level().is_inline() &&
            for_loop->for_type == ForType::Vectorized &&
            function_is_used_in_stmt(func, for_loop)) {

            // If we're trying to inline an extern function, schedule it here and bail out
            debug(2) << "Injecting realization of " << func.name() << " around node " << Stmt(for_loop) << "\n";
            stmt = build_realize(build_pipeline(for_loop));
            found_store_level = found_compute_level = true;
            return;
        }

        body = mutate(body);

        if (compute_level.match(for_loop->name)) {
            debug(3) << "Found compute level\n";
            if (function_is_used_in_stmt(func, body)) {
                body = build_pipeline(body);
            }
            found_compute_level = true;
        }

        if (store_level.match(for_loop->name)) {
            debug(3) << "Found store level\n";
            internal_assert(found_compute_level)
                << "The compute loop level was not found within the store loop level!\n";

            if (function_is_used_in_stmt(func, body)) {
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
                             for_loop->device_api,
                             body);
        }
    }

    // If we're an inline update or extern, we may need to inject a realization here
    virtual void visit(const Provide *op) {
        if (op->name != func.name() &&
            !func.is_pure() &&
            func.schedule().compute_level().is_inline() &&
            function_is_used_in_stmt(func, op)) {

            // Prefix all calls to func in op
            stmt = build_realize(build_pipeline(op));
            found_store_level = found_compute_level = true;
        } else {
            stmt = op;
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

    void visit(const Variable *op) {
        if (ends_with(op->name, ".buffer") &&
            op->param.defined() &&
            op->param.is_buffer() &&
            buffers.find(op->param.name()) == buffers.end()) {
            Result r;
            r.param = op->param;
            r.type = op->param.type();
            r.dimensions = op->param.dimensions();
            buffers[op->param.name()] = r;
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

void realization_order_dfs(string current, map<string, set<string> > &graph, set<string> &visited, set<string> &result_set, vector<string> &order) {
    set<string> &inputs = graph[current];
    visited.insert(current);

    for (set<string>::const_iterator i = inputs.begin();
        i != inputs.end(); ++i) {

        if (visited.find(*i) == visited.end()) {
            realization_order_dfs(*i, graph, visited, result_set, order);
        } else if (*i != current) {
            internal_assert(result_set.find(*i) != result_set.end())
                << "Stuck in a loop computing a realization order. Perhaps this pipeline has a loop?\n";
        }
    }

    result_set.insert(current);
    order.push_back(current);
}

vector<string> realization_order(string output, const map<string, Function> &env, map<string, set<string> > &graph) {
    // Make a DAG representing the pipeline. Each function maps to the set describing its inputs.
    // Populate the graph
    for (map<string, Function>::const_iterator iter = env.begin();
         iter != env.end(); ++iter) {
        map<string, Function> calls = find_direct_calls(iter->second);

        for (map<string, Function>::const_iterator j = calls.begin();
             j != calls.end(); ++j) {
            graph[iter->first].insert(j->first);
        }
    }

    vector<string> order;
    set<string> result_set;
    set<string> visited;

    realization_order_dfs(output, graph, visited, result_set, order);

    return order;
}

Stmt create_initial_loop_nest(Function f, const Target &t) {
    // Generate initial loop nest
    pair<Stmt, Stmt> r = build_production(f);
    Stmt s = r.first;
    // This must be in a pipeline so that bounds inference understands the update step
    s = Pipeline::make(f.name(), r.first, r.second, Evaluate::make(0));
    if (t.has_feature(Target::NoAsserts)) {
        return s;
    } else {
        return inject_explicit_bounds(s, f);
    }
}

class ComputeLegalSchedules : public IRVisitor {
public:
    struct Site {
        bool is_parallel;
        LoopLevel loop_level;
    };
    vector<Site> sites_allowed;

    ComputeLegalSchedules(Function f) : func(f), found(false) {}

private:
    using IRVisitor::visit;

    vector<Site> sites;
    Function func;
    bool found;

    void visit(const For *f) {
        f->min.accept(this);
        f->extent.accept(this);
        size_t first_dot = f->name.find('.');
        size_t last_dot = f->name.rfind('.');
        internal_assert(first_dot != string::npos && last_dot != string::npos);
        string func = f->name.substr(0, first_dot);
        string var = f->name.substr(last_dot + 1);
        Site s = {f->for_type == ForType::Parallel ||
                  f->for_type == ForType::Vectorized,
                  LoopLevel(func, var)};
        sites.push_back(s);
        f->body.accept(this);
        sites.pop_back();
    }

    void register_use() {
        if (!found) {
            found = true;
            sites_allowed = sites;
        } else {
            // Take the common sites between loops and loops allowed
            for (size_t i = 0; i < sites_allowed.size(); i++) {
                bool ok = false;
                for (size_t j = 0; j < sites.size(); j++) {
                    if (sites_allowed[i].loop_level.match(sites[j].loop_level)) {
                        ok = true;
                    }
                }
                if (!ok) {
                    sites_allowed[i] = sites_allowed.back();
                    sites_allowed.pop_back();
                }
            }
        }
    }

    void visit(const Call *c) {
        IRVisitor::visit(c);

        if (c->name == func.name()) {
            register_use();
        }
    }

    void visit(const Variable *v) {
        if (v->type == Handle() &&
            starts_with(v->name, func.name() + ".") &&
            ends_with(v->name, ".buffer")) {
            register_use();
        }
    }
};

string schedule_to_source(Function f,
                          LoopLevel store_at,
                          LoopLevel compute_at) {
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
                if (g.schedule().compute_level().is_inline()) {
                    user_error
                        << "Function " << g.name() << " cannot be scheduled to be computed inline, "
                        << "because it is used in the externally-computed function " << f.name() << "\n";
                }
            }
        }
    }

    // Emit a warning if only some of the steps have been scheduled.
    bool any_scheduled = f.schedule().touched();
    for (size_t i = 0; i < f.updates().size(); i++) {
        const UpdateDefinition &r = f.updates()[i];
        any_scheduled = any_scheduled || r.schedule.touched();
    }
    if (any_scheduled) {
        for (size_t i = 0; i < f.updates().size(); i++) {
            const UpdateDefinition &r = f.updates()[i];
            if (!r.schedule.touched()) {
                std::cerr << "Warning: Update step " << i
                          << " of function " << f.name()
                          << " has not been scheduled, even though some other"
                          << " steps have been. You may have forgotten to"
                          << " schedule it. If this was intentional, call "
                          << f.name() << ".update(" << i << ") to suppress"
                          << " this warning.\n";
            }
        }
    }

    LoopLevel store_at = f.schedule().store_level();
    LoopLevel compute_at = f.schedule().compute_level();
    // Inlining is always allowed
    if (store_at.is_inline() && compute_at.is_inline()) {
        return;
    }

    if (is_output) {
        if ((store_at.is_inline() || store_at.is_root()) &&
            (compute_at.is_inline() || compute_at.is_root())) {
            return;
        } else {
            user_error << "Function " << f.name() << " is the output, so must"
                       << " be scheduled compute_root (which is the default).\n";
        }
    }

    // Otherwise inspect the uses to see what's ok.
    ComputeLegalSchedules legal(f);
    s.accept(&legal);

    bool store_at_ok = false, compute_at_ok = false;
    const vector<ComputeLegalSchedules::Site> &sites = legal.sites_allowed;
    size_t store_idx = 0, compute_idx = 0;
    for (size_t i = 0; i < sites.size(); i++) {
        if (sites[i].loop_level.match(store_at)) {
            store_at_ok = true;
            store_idx = i;
        }
        if (sites[i].loop_level.match(compute_at)) {
            compute_at_ok = store_at_ok;
            compute_idx = i;
        }
    }

    // Check there isn't a parallel loop between the compute_at and the store_at
    std::ostringstream err;

    if (store_at_ok && compute_at_ok) {
        for (size_t i = store_idx + 1; i <= compute_idx; i++) {
            if (sites[i].is_parallel) {
                err << "Function \"" << f.name()
                    << "\" is stored outside the parallel loop over "
                    << sites[i].loop_level.func << "." << sites[i].loop_level.var
                    << " but computed within it. This is a potential race condition.\n";
                store_at_ok = compute_at_ok = false;
            }
        }
    }

    if (!store_at_ok || !compute_at_ok) {
        err << "Function \"" << f.name() << "\" is computed and stored in the following invalid location:\n"
            << schedule_to_source(f, store_at, compute_at) << "\n"
            << "Legal locations for this function are:\n";
        for (size_t i = 0; i < sites.size(); i++) {
            for (size_t j = i; j < sites.size(); j++) {
                if (j > i && sites[j].is_parallel) break;
                err << schedule_to_source(f, sites[i].loop_level, sites[j].loop_level) << "\n";

            }
        }
        user_error << err.str();
    }
}

class RemoveLoopsOverOutermost : public IRMutator {
    using IRMutator::visit;

    void visit(const For *op) {
        if (ends_with(op->name, ".__outermost")) {
            stmt = mutate(op->body);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Variable *op) {
        if (ends_with(op->name, ".__outermost.loop_extent")) {
            expr = 1;
        } else if (ends_with(op->name, ".__outermost.loop_min")) {
            expr = 0;
        } else if (ends_with(op->name, ".__outermost.loop_max")) {
            expr = 1;
        } else {
            expr = op;
        }
    }

    void visit(const LetStmt *op) {
        if (ends_with(op->name, ".__outermost.loop_extent") ||
            ends_with(op->name, ".__outermost.loop_min") ||
            ends_with(op->name, ".__outermost.loop_max")) {
            stmt = mutate(op->body);
        } else {
            IRMutator::visit(op);
        }
    }
};

Stmt schedule_functions(Stmt s, const vector<string> &order,
                        const map<string, Function> &env,
                        const map<string, set<string> > &graph, const Target &t) {

    // Inject a loop over root to give us a scheduling point
    string root_var = LoopLevel::root().func + "." + LoopLevel::root().var;
    s = For::make(root_var, 0, 1, ForType::Serial, DeviceAPI::Host, s);

    for (size_t i = order.size(); i > 0; i--) {
        Function f = env.find(order[i-1])->second;

        validate_schedule(f, s, i == order.size());

        // We don't actually want to schedule the output function here.
        if (i == order.size()) continue;

        if (f.has_pure_definition() &&
            !f.has_update_definition() &&
            f.schedule().compute_level().is_inline()) {
            debug(1) << "Inlining " << order[i-1] << '\n';
            s = inline_function(s, f);
        } else {
            debug(1) << "Injecting realization of " << order[i-1] << '\n';
            InjectRealization injector(f, t);
            s = injector.mutate(s);
            internal_assert(injector.found_store_level && injector.found_compute_level);
        }
        debug(2) << s << '\n';
    }

    // We can remove the loop over root now
    const For *root_loop = s.as<For>();
    internal_assert(root_loop);
    s = root_loop->body;

    // We can also remove all the loops over __outermost now.
    RemoveLoopsOverOutermost r;
    s = r.mutate(s);

    return s;
}

// Insert checks to make sure that parameters are within their
// declared range.
Stmt add_parameter_checks(Stmt s, const Target &t) {
    // First, find all the parameters
    FindParameters finder;
    s.accept(&finder);

    map<string, Expr> replace_with_constrained;
    vector<pair<string, Expr> > lets;

    struct ParamAssert {
        Expr condition;
        Expr value, limit_value;
        string param_name;
    };

    vector<ParamAssert> asserts;

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
                ParamAssert p = {
                    constrained_value >= param.get_min_value(),
                    constrained_value, param.get_min_value(),
                    param.name()
                };
                asserts.push_back(p);
                constrained_value = max(constrained_value, param.get_min_value());
            }

            if (param.get_max_value().defined()) {
                ParamAssert p = {
                    constrained_value <= param.get_max_value(),
                    constrained_value, param.get_max_value(),
                    param.name()
                };
                asserts.push_back(p);
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

    if (t.has_feature(Target::NoAsserts)) {
        asserts.clear();
    }

    // Inject the assert statements
    for (size_t i = 0; i < asserts.size(); i++) {
        ParamAssert p = asserts[i];
        // Upgrade the types to 64-bit versions for the error call
        Type wider = p.value.type();
        wider.bits = 64;
        p.limit_value = cast(wider, p.limit_value);
        p.value       = cast(wider, p.value);

        string error_call_name = "halide_error_param";

        if (p.condition.as<LE>()) {
            error_call_name += "_too_large";
        } else {
            internal_assert(p.condition.as<GE>());
            error_call_name += "_too_small";
        }

        if (wider.is_int()) {
            error_call_name += "_i64";
        } else if (wider.is_uint()) {
            error_call_name += "_u64";
        } else {
            internal_assert(wider.is_float());
            error_call_name += "_f64";
        }

        Expr error = Call::make(Int(32), error_call_name,
                                vec<Expr>(p.param_name, p.value, p.limit_value),
                                Call::Extern);

        s = Block::make(AssertStmt::make(p.condition, error), s);
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
Stmt add_image_checks(Stmt s, Function f, const Target &t,
                      const vector<string> &order,
                      const map<string, Function> &env,
                      const FuncValueBounds &fb) {

    bool no_asserts = t.has_feature(Target::NoAsserts);
    bool no_bounds_query = t.has_feature(Target::NoBoundsQuery);

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

    Scope<Interval> empty_scope;
    map<string, Box> boxes = boxes_touched(s, empty_scope, fb);

    // Now iterate through all the buffers, creating a list of lets
    // and a list of asserts.
    vector<pair<string, Expr> > lets_overflow;
    vector<pair<string, Expr> > lets_required;
    vector<pair<string, Expr> > lets_constrained;
    vector<pair<string, Expr> > lets_proposed;
    vector<Stmt> dims_no_overflow_asserts;
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
        internal_assert(touched.empty() || (int)(touched.size()) == dimensions);

        // The buffer may be used in one or more extern stage. If so we need to
        // expand the box touched to include the results of the
        // top-level bounds query calls to those extern stages.
        if (param.defined()) {
            // Find the extern users.
            vector<string> extern_users;
            for (size_t i = 0; i < order.size(); i++) {
                Function f = env.find(order[i])->second;
                if (f.has_extern_definition()) {
                    const vector<ExternFuncArgument> &args = f.extern_arguments();
                    for (size_t j = 0; j < args.size(); j++) {
                        if ((args[j].image_param.defined() &&
                             args[j].image_param.name() == param.name()) ||
                            (args[j].buffer.defined() &&
                             args[j].buffer.name() == param.name())) {
                            extern_users.push_back(order[i]);
                        }
                    }
                }
            }

            // Expand the box by the result of the bounds query from each.
            for (size_t i = 0; i < extern_users.size(); i++) {
                const string &extern_user = extern_users[i];
                Box query_box;
                Expr query_buf = Variable::make(Handle(), param.name() + ".bounds_query." + extern_user);
                for (int j = 0; j < dimensions; j++) {
                    Expr min = Call::make(Int(32), Call::extract_buffer_min,
                                          vec<Expr>(query_buf, j), Call::Intrinsic);
                    Expr max = Call::make(Int(32), Call::extract_buffer_max,
                                          vec<Expr>(query_buf, j), Call::Intrinsic);
                    query_box.push_back(Interval(min, max));
                }
                merge_boxes(touched, query_box);
            }
        }

        // An expression returning whether or not we're in inference mode
        ReductionDomain rdom;
        Expr inference_mode = Variable::make(UInt(1), name + ".host_and_dev_are_null", image, param, rdom);

        maybe_return_condition = maybe_return_condition || inference_mode;

        // Come up with a name to refer to this buffer in the error messages
        string error_name = (is_output_buffer ? "Output" : "Input");
        error_name += " buffer " + name;

        // Check the elem size matches the internally-understood type
        {
            string elem_size_name = name + ".elem_size";
            Expr elem_size = Variable::make(Int(32), elem_size_name, image, param, rdom);
            int correct_size = type.bytes();
            ostringstream type_name;
            type_name << type;
            Expr error = Call::make(Int(32), "halide_error_bad_elem_size",
                                    vec<Expr>(error_name, type_name.str(),
                                              elem_size, correct_size),
                                    Call::Extern);
            asserts_elem_size.push_back(AssertStmt::make(elem_size == correct_size, error));
        }

        if (touched.maybe_unused()) {
            debug(3) << "Image " << name << " is only used when " << touched.used << "\n";
        }

        // Check that the region passed in (after applying constraints) is within the region used
        debug(3) << "In image " << name << " region touched is:\n";


        for (int j = 0; j < dimensions; j++) {
            string dim = int_to_string(j);
            string actual_min_name = name + ".min." + dim;
            string actual_extent_name = name + ".extent." + dim;
            string actual_stride_name = name + ".stride." + dim;
            Expr actual_min = Variable::make(Int(32), actual_min_name, image, param, rdom);
            Expr actual_extent = Variable::make(Int(32), actual_extent_name, image, param, rdom);
            Expr actual_stride = Variable::make(Int(32), actual_stride_name, image, param, rdom);
            if (!touched[j].min.defined() || !touched[j].max.defined()) {
                user_error << "Buffer " << name
                           << " may be accessed in an unbounded way in dimension "
                           << j << "\n";
            }

            Expr min_required = touched[j].min;
            Expr extent_required = touched[j].max + 1 - touched[j].min;

            if (touched.maybe_unused()) {
                min_required = select(touched.used, min_required, actual_min);
                extent_required = select(touched.used, extent_required, actual_extent);
            }

            string min_required_name = name + ".min." + dim + ".required";
            string extent_required_name = name + ".extent." + dim + ".required";

            Expr min_required_var = Variable::make(Int(32), min_required_name);
            Expr extent_required_var = Variable::make(Int(32), extent_required_name);

            lets_required.push_back(make_pair(extent_required_name, extent_required));
            lets_required.push_back(make_pair(min_required_name, min_required));

            Expr actual_max = actual_min + actual_extent - 1;
            Expr max_required = min_required_var + extent_required_var - 1;

            if (touched.maybe_unused()) {
                max_required = select(touched.used, max_required, actual_max);
            }

            Expr oob_condition = actual_min <= min_required_var && actual_max >= max_required;

            Expr oob_error = Call::make(Int(32), "halide_error_access_out_of_bounds",
                                        vec<Expr>(error_name, j,
                                                  min_required_var, max_required,
                                                  actual_min, actual_max),
                                        Call::Extern);

            asserts_required.push_back(AssertStmt::make(oob_condition, oob_error));

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

            // Insert checks to make sure the total size of all input
            // and output buffers is <= 2^31 - 1.  And that no product
            // of extents overflows 2^31 - 1. This second test is
            // likely only needed if a fuse directive is used in the
            // schedule to combine multiple extents, but it is here
            // for extra safety. Ultimately we will want to make
            // Halide handle larger single buffers, at least on 64-bit
            // systems.
            Expr max_size = cast<int64_t>(0x7fffffff);
            Expr actual_size = cast<int64_t>(actual_extent) * actual_stride;
            Expr allocation_size_error = Call::make(Int(32), "halide_error_buffer_allocation_too_large",
                                                    vec<Expr>(name, actual_size, max_size), Call::Extern);
            Stmt check = AssertStmt::make(actual_size <= max_size, allocation_size_error);
            dims_no_overflow_asserts.push_back(check);

            // Don't repeat extents check for secondary buffers as extents must be the same as for the first one.
            if (!is_secondary_output_buffer) {
                if (j == 0) {
                    lets_overflow.push_back(make_pair(name + ".total_extent." + dim, cast<int64_t>(actual_extent)));
                } else {
                    Expr last_dim = Variable::make(Int(64), name + ".total_extent." + int_to_string(j-1));
                    Expr this_dim = actual_extent * last_dim;
                    Expr this_dim_var = Variable::make(Int(64), name + ".total_extent." + dim);
                    lets_overflow.push_back(make_pair(name + ".total_extent." + dim, this_dim));
                    Expr error = Call::make(Int(32), "halide_error_buffer_extents_too_large",
                                            vec<Expr>(name, this_dim_var, max_size), Call::Extern);
                    Stmt check = AssertStmt::make(this_dim_var <= max_size, error);
                    dims_no_overflow_asserts.push_back(check);
                }
            }
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
        Expr call = Call::make(UInt(1), Call::rewrite_buffer, args, Call::Intrinsic, Function(), 0, image, param);
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

            Expr stride_orig = Variable::make(Int(32), stride_name, image, param, rdom);
            Expr extent_orig = Variable::make(Int(32), extent_name, image, param, rdom);
            Expr min_orig    = Variable::make(Int(32), min_name, image, param, rdom);

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
                    user_assert(!param.extent_constraint(i).defined() &&
                                !param.min_constraint(i).defined())
                        << "Can't constrain the min or extent of an output buffer beyond the "
                        << "first. They are implicitly constrained to have the same min and extent "
                        << "as the first output buffer.\n";

                    stride_constrained = param.stride_constraint(i);
                } else if (image.defined() && (int)i < image.dimensions()) {
                    stride_constrained = image.stride(i);
                }

                std::string min0_name = f.name() + ".0.min." + dim;
                if (replace_with_constrained.count(min0_name) > 0 ) {
                    min_constrained = replace_with_constrained[min0_name];
                } else {
                    min_constrained = Variable::make(Int(32), min0_name);
                }

                std::string extent0_name = f.name() + ".0.extent." + dim;
                if (replace_with_constrained.count(extent0_name) > 0 ) {
                    extent_constrained = replace_with_constrained[extent0_name];
                } else {
                    extent_constrained = Variable::make(Int(32), extent0_name);
                }
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
            Expr max_proposed = min_proposed + extent_proposed - 1;
            Expr max_required = min_required + extent_required - 1;
            Expr check = (min_proposed <= min_required) && (max_proposed >= max_required);
            Expr error = Call::make(Int(32), "halide_error_constraints_make_required_region_smaller",
                                    vec<Expr>(error_name, i,
                                              min_proposed, max_proposed,
                                              min_required, max_required), Call::Extern);
            asserts_proposed.push_back(AssertStmt::make((!inference_mode) || check, error));

            // stride_required is just a suggestion. It's ok if the
            // constraints shuffle them around in ways that make it
            // smaller.
            /*
            check = (stride_proposed >= stride_required);
            error = "Applying the constraints to the required stride made it smaller";
            asserts_proposed.push_back(AssertStmt::make((!inference_mode) || check, error, vector<Expr>()));
            */
        }

        // Assert all the conditions, and set the new values
        for (size_t i = 0; i < constraints.size(); i++) {
            Expr var = Variable::make(Int(32), constraints[i].first);
            Expr constrained_var = Variable::make(Int(32), constraints[i].first + ".constrained");

            const string &var_str = constraints[i].first;
            ostringstream ss;
            ss << constraints[i].second;
            string constrained_var_str = ss.str();

            replace_with_constrained[var_str] = constrained_var;

            lets_constrained.push_back(make_pair(var_str + ".constrained", constraints[i].second));

            Expr error = Call::make(Int(32), "halide_error_constraint_violated",
                                    vec<Expr>(var_str, var, constrained_var_str, constrained_var),
                                    Call::Extern);

            // Check the var passed in equals the constrained version (when not in inference mode)
            asserts_constrained.push_back(AssertStmt::make(var == constrained_var, error));
        }
    }

    // Inject the code that checks that no dimension math overflows
    if (!no_asserts) {
        for (size_t i = dims_no_overflow_asserts.size(); i > 0; i--) {
            s = Block::make(dims_no_overflow_asserts[i-1], s);
        }

        // Inject the code that defines the proposed sizes.
        for (size_t i = lets_overflow.size(); i > 0; i--) {
            s = LetStmt::make(lets_overflow[i-1].first, lets_overflow[i-1].second, s);
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

    if (!no_asserts) {
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
    }

    // Inject the code that returns early for inference mode.
    if (!no_bounds_query) {
        s = IfThenElse::make(!maybe_return_condition, s);

        // Inject the code that does the buffer rewrites for inference mode.
        for (size_t i = buffer_rewrites.size(); i > 0; i--) {
            s = Block::make(buffer_rewrites[i-1], s);
        }
    }

    if (!no_asserts) {
        // Inject the code that checks the proposed sizes still pass the bounds checks
        for (size_t i = asserts_proposed.size(); i > 0; i--) {
            s = Block::make(asserts_proposed[i-1], s);
        }
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

class PropagateInheritedAttributes : public IRMutator {
    using IRMutator::visit;

    DeviceAPI for_device;

    void visit(const For *op) {
        DeviceAPI save_device = for_device;
        for_device = (op->device_api == DeviceAPI::Parent) ? for_device : op->device_api;

        Expr min = mutate(op->min);
        Expr extent = mutate(op->extent);
        Stmt body = mutate(op->body);

        if (min.same_as(op->min) &&
            extent.same_as(op->extent) &&
            body.same_as(op->body) &&
            for_device == op->device_api) {
            stmt = op;
        } else {
            stmt = For::make(op->name, min, extent, op->for_type, for_device, body);
        }

        for_device = save_device;
    }

public:
    PropagateInheritedAttributes() : for_device(DeviceAPI::Host) {
    }
};

Stmt propagate_inherited_attributes(Stmt s) {
    PropagateInheritedAttributes propagator;

    return propagator.mutate(s);
}

Stmt lower(Function f, const Target &t, const vector<IRMutator *> &custom_passes) {
    // Compute an environment
    map<string, Function> env = find_transitive_calls(f);

    // Compute a realization order
    map<string, set<string> > graph;
    vector<string> order = realization_order(f.name(), env, graph);
    Stmt s = create_initial_loop_nest(f, t);

    debug(2) << "Lowering before everything:" << '\n' << s << '\n';
    s = schedule_functions(s, order, env, graph, t);
    debug(2) << "Lowering after injecting realizations:\n" << s << '\n';

    debug(2) << "Injecting memoization...\n";
    s = inject_memoization(s, env, f.name());
    debug(2) << "Lowering after injecting memoization:\n" << s << '\n';

    debug(1) << "Injecting tracing...\n";
    s = inject_tracing(s, env, f);
    debug(2) << "Lowering after injecting tracing:\n" << s << '\n';

    debug(1) << "Injecting profiling...\n";
    s = inject_profiling(s, f.name());
    debug(2) << "Lowering after injecting profiling:\n" << s << '\n';

    debug(1) << "Adding checks for parameters\n";
    s = add_parameter_checks(s, t);
    debug(2) << "Lowering after injecting parameter checks:\n" << s << '\n';

    // Compute the maximum and minimum possible value of each
    // function. Used in later bounds inference passes.
    debug(1) << "Computing bounds of each function's value\n";
    FuncValueBounds func_bounds = compute_function_value_bounds(order, env);

    // The checks will be in terms of the symbols defined by bounds
    // inference.
    debug(1) << "Adding checks for images\n";
    s = add_image_checks(s, f, t, order, env, func_bounds);
    debug(2) << "Lowering after injecting image checks:\n" << s << '\n';

    // This pass injects nested definitions of variable names, so we
    // can't simplify statements from here until we fix them up. (We
    // can still simplify Exprs).
    debug(1) << "Performing computation bounds inference...\n";
    s = bounds_inference(s, order, env, func_bounds);
    debug(2) << "Lowering after computation bounds inference:\n" << s << '\n';

    debug(1) << "Performing sliding window optimization...\n";
    s = sliding_window(s, env);
    debug(2) << "Lowering after sliding window:\n" << s << '\n';

    debug(1) << "Performing allocation bounds inference...\n";
    s = allocation_bounds_inference(s, env, func_bounds);
    debug(2) << "Lowering after allocation bounds inference:\n" << s << '\n';

    debug(1) << "Removing code that depends on undef values...\n";
    s = remove_undef(s);
    debug(2) << "Lowering after removing code that depends on undef values:\n" << s << "\n\n";

    // This uniquifies the variable names, so we're good to simplify
    // after this point. This lets later passes assume syntactic
    // equivalence means semantic equivalence.
    debug(1) << "Uniquifying variable names...\n";
    s = uniquify_variable_names(s);
    debug(2) << "Lowering after uniquifying variable names:\n" << s << "\n\n";

    debug(1) << "Performing storage folding optimization...\n";
    s = storage_folding(s);
    debug(2) << "Lowering after storage folding:\n" << s << '\n';

    debug(1) << "Injecting debug_to_file calls...\n";
    s = debug_to_file(s, order.back(), env);
    debug(2) << "Lowering after injecting debug_to_file calls:\n" << s << '\n';

    debug(1) << "Simplifying...\n"; // without removing dead lets, because storage flattening needs the strides
    s = simplify(s, false);
    debug(2) << "Lowering after first simplification:\n" << s << "\n\n";

    debug(1) << "Dynamically skipping stages...\n";
    s = skip_stages(s, order);
    debug(2) << "Lowering after dynamically skipping stages:\n" << s << "\n\n";

    if (t.has_feature(Target::OpenGL) || t.has_feature(Target::Renderscript)) {
        debug(1) << "Injecting shader intrinsics...\n";
        s = inject_shader_intrinsics(s);
        debug(2) << "Lowering after shader intrinsics:\n" << s << "\n\n";
    }

    debug(1) << "Performing storage flattening...\n";
    s = storage_flattening(s, order.back(), env);
    debug(2) << "Lowering after storage flattening:\n" << s << "\n\n";

    if (t.has_gpu_feature() || t.has_feature(Target::OpenGL)) {
        debug(1) << "Injecting host <-> dev buffer copies...\n";
        s = inject_host_dev_buffer_copies(s, t);
        debug(2) << "Lowering after injecting host <-> dev buffer copies:\n" << s << "\n\n";
    }

    if (t.has_feature(Target::OpenGL)) {
        debug(1) << "Injecting OpenGL texture intrinsics...\n";
        s = inject_opengl_intrinsics(s);
        debug(2) << "Lowering after OpenGL intrinsics:\n" << s << "\n\n";
    }

    if (t.has_gpu_feature()) {
        debug(1) << "Injecting per-block gpu synchronization...\n";
        s = fuse_gpu_thread_loops(s);
        debug(2) << "Lowering after injecting per-block gpu synchronization:\n" << s << "\n\n";
    }

    debug(1) << "Simplifying...\n";
    s = simplify(s);
    s = unify_duplicate_lets(s);
    s = remove_trivial_for_loops(s);
    debug(2) << "Lowering after second simplifcation:\n" << s << "\n\n";

    debug(1) << "Unrolling...\n";
    s = unroll_loops(s);
    s = simplify(s);
    debug(2) << "Lowering after unrolling:\n" << s << "\n\n";

    debug(1) << "Vectorizing...\n";
    s = vectorize_loops(s);
    s = simplify(s);
    debug(2) << "Lowering after vectorizing:\n" << s << "\n\n";

    debug(1) << "Detecting vector interleavings...\n";
    s = rewrite_interleavings(s);
    s = simplify(s);
    debug(2) << "Lowering after rewriting vector interleavings:\n" << s << "\n\n";

    debug(1) << "Partitioning loops to simplify boundary conditions...\n";
    s = partition_loops(s);
    s = simplify(s);
    debug(2) << "Lowering after partitioning loops:\n" << s << "\n\n";

    debug(1) << "Injecting early frees...\n";
    s = inject_early_frees(s);
    debug(2) << "Lowering after injecting early frees:\n" << s << "\n\n";

    debug(1) << "Simplifying...\n";
    s = common_subexpression_elimination(s);

    if (t.has_feature(Target::OpenGL)) {
        debug(1) << "Detecting varying attributes...\n";
        s = find_linear_expressions(s);
        debug(2) << "Lowering after detecting varying attributes:\n" << s << "\n\n";

        debug(1) << "Moving varying attribute expressions out of the shader...\n";
        s = setup_gpu_vertex_buffer(s);
        debug(2) << "Lowering after removing varying attributes:\n" << s << "\n\n";
    }

    // This is envisioned as a catch all pass to propagate attributes
    // which are inherited from a parent node by a child node so
    // every CodeGen backend does not have to keep track of these
    // attributes as the IR tree is traversed. At present, only the
    // GPU device attribute on For nodes is propagated (to contained
    // For nodes which have their device set to DeviceAPI::Parent).
    debug(1) << "Propagating inherited attributes downward.\n";
    s = propagate_inherited_attributes(s);
    debug(2) << "Lowering after propagating inherited attributes:\n" << s << "\n\n";

    s = remove_trivial_for_loops(s);
    s = simplify(s);
    debug(1) << "Lowering after final simplification:\n" << s << "\n\n";

    if (!custom_passes.empty()) {
        for (size_t i = 0; i < custom_passes.size(); i++) {
            debug(1) << "Running custom lowering pass " << i << "...\n";
            s = custom_passes[i]->mutate(s);
            debug(1) << "Lowering after custom pass " << i << ":\n" << s << "\n\n";
        }
    }

    return s;
}

}
}
