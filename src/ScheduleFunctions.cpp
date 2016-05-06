#include "ScheduleFunctions.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"
#include "ExprUsesVar.h"
#include "Var.h"
#include "Qualify.h"
#include "IRMutator.h"
#include "Target.h"
#include "Inline.h"
#include "CodeGen_GPU_Dev.h"
#include "IRPrinter.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::vector;
using std::pair;
using std::make_pair;

namespace {
// A structure representing a containing LetStmt, IfThenElse, or For
// loop. Used in build_provide_loop_nest below.
struct Container {
    enum Type {For, Let, If};
    Type type;
    // If it's a for loop, the index in the dims list.
    int dim_idx;
    string name;
    Expr value;
};
}

class ContainsImpureCall : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Call *op) {
        if (!op->is_pure()) {
            result = true;
        } else {
            IRVisitor::visit(op);
        }
    }

public:
    bool result = false;
    ContainsImpureCall() {}
};

bool contains_impure_call(const Expr &expr) {
    ContainsImpureCall is_not_pure;
    expr.accept(&is_not_pure);
    return is_not_pure.result;
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
    for (const Bound &i : s.bounds()) {
        known_size_dims[i.var] = i.extent;
    }
    // Then use any reduction domain.
    const ReductionDomain &rdom = s.reduction_domain();
    if (rdom.defined()) {
        for (const ReductionVariable &i : rdom.domain()) {
            known_size_dims[i.var] = i.extent;
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

    Dim innermost_non_trivial_loop;
    for (const Dim &d : s.dims()) {
        if (d.for_type != ForType::Vectorized &&
            d.for_type != ForType::Unrolled) {
            innermost_non_trivial_loop = d;
            break;
        }
    }

    // Define the function args in terms of the loop variables using the splits
    for (const Split &split : splits) {
        Expr outer = Variable::make(Int(32), prefix + split.outer);
        Expr outer_max = Variable::make(Int(32), prefix + split.outer + ".loop_max");
        if (split.is_split()) {
            Expr inner = Variable::make(Int(32), prefix + split.inner);
            Expr old_max = Variable::make(Int(32), prefix + split.old_var + ".loop_max");
            Expr old_min = Variable::make(Int(32), prefix + split.old_var + ".loop_min");
            Expr old_extent = Variable::make(Int(32), prefix + split.old_var + ".loop_extent");

            known_size_dims[split.inner] = split.factor;

            Expr base = outer * split.factor + old_min;
            string base_name = prefix + split.inner + ".base";
            Expr base_var = Variable::make(Int(32), base_name);
            string old_var_name = prefix + split.old_var;
            Expr old_var = Variable::make(Int(32), old_var_name);

            map<string, Expr>::iterator iter = known_size_dims.find(split.old_var);

            if (is_update) {
                user_assert(split.tail != TailStrategy::ShiftInwards)
                    << "When splitting Var " << split.old_var
                    << " ShiftInwards is not a legal tail strategy for update definitions, as"
                    << " it may change the meaning of the algorithm\n";
            }

            if (split.exact) {
                user_assert(split.tail == TailStrategy::Auto ||
                            split.tail == TailStrategy::GuardWithIf)
                    << "When splitting Var " << split.old_var
                    << " the tail strategy must be GuardWithIf or Auto. "
                    << "Anything else may change the meaning of the algorithm\n";
            }

            TailStrategy tail = split.tail;
            if (tail == TailStrategy::Auto) {
                if (split.exact) {
                    tail = TailStrategy::GuardWithIf;
                } else if (is_update) {
                    tail = TailStrategy::RoundUp;
                } else {
                    tail = TailStrategy::ShiftInwards;
                }
            }

            if ((iter != known_size_dims.end()) &&
                is_zero(simplify(iter->second % split.factor))) {
                // We have proved that the split factor divides the
                // old extent. No need to adjust the base or add an if
                // statement.
                known_size_dims[split.outer] = iter->second / split.factor;
            } else if (is_one(split.factor)) {
                // The split factor trivially divides the old extent,
                // but we know nothing new about the outer dimension.
            } else if (tail == TailStrategy::GuardWithIf) {
                // It's an exact split but we failed to prove that the
                // extent divides the factor. Use predication.

                // Make a var representing the original var minus its
                // min. It's important that this is a single Var so
                // that bounds inference has a chance of understanding
                // what it means for it to be limited by the if
                // statement's condition.
                Expr rebased = outer * split.factor + inner;
                string rebased_var_name = prefix + split.old_var + ".rebased";
                Expr rebased_var = Variable::make(Int(32), rebased_var_name);
                stmt = substitute(prefix + split.old_var, rebased_var + old_min, stmt);

                // Tell Halide to optimize for the case in which this
                // condition is true by partitioning some outer loop.
                Expr cond = likely(rebased_var < old_extent);
                stmt = IfThenElse::make(cond, stmt, Stmt());
                stmt = LetStmt::make(rebased_var_name, rebased, stmt);

            } else if (tail == TailStrategy::ShiftInwards) {
                // Adjust the base downwards to not compute off the
                // end of the realization.

                // Only mark the base as likely (triggering a loop
                // partition) if the outer var is the innermost
                // non-trivial loop and it's a serial loop. This
                // usually is due to an unroll or vectorize call.
                if (split.outer == innermost_non_trivial_loop.var &&
                    innermost_non_trivial_loop.for_type == ForType::Serial) {
                    base = likely(base);
                }

                base = Min::make(base, old_max + (1 - split.factor));
            } else {
                internal_assert(tail == TailStrategy::RoundUp);
            }

            // Substitute in the new expression for the split variable ...
            stmt = substitute(old_var_name, base_var + inner, stmt);
            // ... but also define it as a let for the benefit of bounds inference.
            stmt = LetStmt::make(old_var_name, base_var + inner, stmt);
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
        Container c = {Container::For, i, prefix + dim.var, Expr()};
        nest.push_back(c);
    }

    // Strip off the lets into the containers vector.
    while (const LetStmt *let = stmt.as<LetStmt>()) {
        Container c = {Container::Let, 0, let->name, let->value};
        nest.push_back(c);
        stmt = let->body;
    }

    // Put all the reduction domain predicate into the containers vector.
    int n_predicates = 0;
    if (rdom.defined()) {
        vector<Expr> predicates = rdom.split_predicate();
        n_predicates = predicates.size();
        for (Expr pred : predicates) {
            pred = qualify(prefix, pred);
            Container c = {Container::If, 0, "", pred};
            nest.push_back(c);
        }
    }

    // Resort the containers vector so that lets are as far outwards
    // as possible. Use reverse insertion sort. Start at the first letstmt.
    for (int i = (int)s.dims().size(); i < (int)nest.size() - n_predicates; i++) {
        // Only push up LetStmts.
        internal_assert(nest[i].value.defined());
        internal_assert(nest[i].type == Container::Let);

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

    // Sort the predicate guards so they are as far outwards as possible.
    for (int i = (int)nest.size() - n_predicates; i < (int)nest.size(); i++) {
        // Only push up LetStmts.
        internal_assert(nest[i].value.defined());
        internal_assert(nest[i].type == Container::If);

        // Cannot lift out the predicate guard if it contains call to non-pure function
        if (contains_impure_call(nest[i].value)) {
            continue;
        }

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
        if (nest[i].type == Container::Let) {
            internal_assert(nest[i].value.defined());
            stmt = LetStmt::make(nest[i].name, nest[i].value, stmt);
        } else if (nest[i].type == Container::If) {
            internal_assert(nest[i].value.defined());
            stmt = IfThenElse::make(likely(nest[i].value), stmt, Stmt());
        } else {
            internal_assert(nest[i].type == Container::For);
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
        stmt = LetStmt::make(o + ".loop_max", 0, stmt);
        stmt = LetStmt::make(o + ".loop_extent", 1, stmt);
    }

    // Define the loop mins and extents in terms of the mins and maxs produced by bounds inference
    for (const std::string &i : f.args()) {
        string var = prefix + i;
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

        vector<pair<string, Expr>> lets;

        // Iterate through all of the input args to the extern
        // function building a suitable argument list for the
        // extern function call.
        for (const ExternFuncArgument &arg : args) {
            if (arg.is_expr()) {
                extern_call_args.push_back(arg.expr);
            } else if (arg.is_func()) {
                Function input(arg.func);
                for (int k = 0; k < input.outputs(); k++) {
                    string buf_name = input.name();
                    if (input.outputs() > 1) {
                        buf_name += "." + std::to_string(k);
                    }
                    buf_name += ".buffer";
                    Expr buffer = Variable::make(type_of<struct buffer_t *>(), buf_name);
                    extern_call_args.push_back(buffer);
                }
            } else if (arg.is_buffer()) {
                Buffer b = arg.buffer;
                Parameter p(b.type(), true, b.dimensions(), b.name());
                p.set_buffer(b);
                Expr buf = Variable::make(type_of<struct buffer_t *>(), b.name() + ".buffer", p);
                extern_call_args.push_back(buf);
            } else if (arg.is_image_param()) {
                Parameter p = arg.image_param;
                Expr buf = Variable::make(type_of<struct buffer_t *>(), p.name() + ".buffer", p);
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
                    buf_name += "." + std::to_string(j);
                }
                buf_name += ".buffer";
                Expr buffer = Variable::make(type_of<struct buffer_t *>(), buf_name);
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
                host_ptr = Call::make(Handle(), Call::address_of, {host_ptr}, Call::Intrinsic);

                buffer_args[0] = host_ptr;
                buffer_args[1] = make_zero(f.output_types()[j]);
                for (int k = 0; k < f.dimensions(); k++) {
                    string var = stage_name + f.args()[k];
                    Expr min = Variable::make(Int(32), var + ".min");
                    Expr max = Variable::make(Int(32), var + ".max");
                    Expr stride = Variable::make(Int(32), stride_name + ".stride." + std::to_string(k));
                    buffer_args.push_back(min);
                    buffer_args.push_back(max - min + 1);
                    buffer_args.push_back(stride);
                }

                Expr output_buffer_t = Call::make(type_of<struct buffer_t *>(), Call::create_buffer_t,
                                                  buffer_args, Call::Intrinsic);

                string buf_name = f.name() + "." + std::to_string(j) + ".tmp_buffer";
                extern_call_args.push_back(Variable::make(type_of<struct buffer_t *>(), buf_name));
                lets.push_back(make_pair(buf_name, output_buffer_t));
            }
        }

        // Make the extern call
        Expr e = Call::make(Int(32), extern_name, extern_call_args,
                            f.extern_definition_is_c_plus_plus() ? Call::ExternCPlusPlus
                                                                 : Call::Extern);
        string result_name = unique_name('t');
        Expr result = Variable::make(Int(32), result_name);
        // Check if it succeeded
        Expr error = Call::make(Int(32), "halide_error_extern_stage_failed",
                                {extern_name, result}, Call::Extern);
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

        string prefix = f.name() + ".s" + std::to_string(i+1) + ".";

        vector<Expr> site(r.args.size());
        vector<Expr> values(r.values.size());
        for (size_t i = 0; i < values.size(); i++) {
            Expr v = r.values[i];
            v = qualify(prefix, v);
            values[i] = v;
            debug(3) << "Update value " << i << " = " << v << "\n";
        }

        for (size_t i = 0; i < r.args.size(); i++) {
            Expr s = r.args[i];
            s = qualify(prefix, s);
            site[i] = s;
            debug(3) << "Update site " << i << " = " << s << "\n";
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
            string prefix = func.name() + ".s" + std::to_string(stage) + "." + b.var;
            string min_name = prefix + ".min_unbounded";
            string max_name = prefix + ".max_unbounded";
            Expr min_var = Variable::make(Int(32), min_name);
            Expr max_var = Variable::make(Int(32), max_name);
            if (!b.min.defined()) {
                b.min = min_var;
            }

            Expr max_val = (b.extent + b.min) - 1;
            Expr min_val = b.min;

            Expr check = (min_val <= min_var) && (max_val >= max_var);
            Expr error_msg = Call::make(Int(32), "halide_error_explicit_bounds_too_small",
                                        {b.var, func.name(), min_val, max_val, min_var, max_var},
                                        Call::Extern);
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
        if (op->type.is_handle() &&
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
    bool is_output, found_store_level, found_compute_level;
    const Target &target;

    InjectRealization(const Function &f, bool o, const Target &t) :
        func(f), is_output(o),
        found_store_level(false), found_compute_level(false),
        target(t) {}

private:

    string producing;

    Stmt build_pipeline(Stmt s) {
        pair<Stmt, Stmt> realization = build_production(func);

        return ProducerConsumer::make(func.name(), realization.first, realization.second, s);
    }

    Stmt build_realize(Stmt s) {
        if (!is_output) {
            Region bounds;
            string name = func.name();
            for (int i = 0; i < func.dimensions(); i++) {
                string arg = func.args()[i];
                Expr min = Variable::make(Int(32), name + "." + arg + ".min_realized");
                Expr extent = Variable::make(Int(32), name + "." + arg + ".extent_realized");
                bounds.push_back(Range(min, extent));
            }

            s = Realize::make(name, func.output_types(), bounds, const_true(), s);
        }

        // This is also the point at which we inject explicit bounds
        // for this realization.
        if (target.has_feature(Target::NoAsserts)) {
            return s;
        } else {
            return inject_explicit_bounds(s, func);
        }
    }

    using IRMutator::visit;

    void visit(const ProducerConsumer *op) {
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
            stmt = ProducerConsumer::make(op->name, produce, update, consume);
        }
    }

    void visit(const For *for_loop) {
        debug(3) << "InjectRealization of " << func.name() << " entering for loop over " << for_loop->name << "\n";
        const LoopLevel &compute_level = func.schedule().compute_level();
        const LoopLevel &store_level = func.schedule().store_level();

        Stmt body = for_loop->body;

        // Dig through any let statements
        vector<pair<string, Expr>> lets;
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
            if (function_is_used_in_stmt(func, body) || is_output) {
                body = build_pipeline(body);
            }
            found_compute_level = true;
        }

        if (store_level.match(for_loop->name)) {
            debug(3) << "Found store level\n";
            internal_assert(found_compute_level)
                << "The compute loop level was not found within the store loop level!\n";

            if (function_is_used_in_stmt(func, body) || is_output) {
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
            vector<Site> common_sites;

            // Take the common sites between sites and sites_allowed
            for (const Site &s1 : sites) {
                for (const Site &s2 : sites_allowed) {
                    if (s1.loop_level.match(s2.loop_level)) {
                        common_sites.push_back(s1);
                        break;
                    }
                }
            }

            sites_allowed.swap(common_sites);
        }
    }

    void visit(const Call *c) {
        IRVisitor::visit(c);

        if (c->name == func.name()) {
            register_use();
        }
    }

    void visit(const Variable *v) {
        if (v->type.is_handle() &&
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
        string store_var_name = store_at.var;
        string compute_var_name = compute_at.var;
        if (store_var_name == Var::outermost().name()) {
            store_var_name = "Var::outermost()";
        }
        if (compute_var_name == Var::outermost().name()) {
            compute_var_name = "Var::outermost()";
        }
        if (!store_at.match(compute_at)) {
            if (store_at.is_root()) {
                ss << ".store_root()";
            } else {
                ss << ".store_at(" << store_at.func << ", " << store_var_name << ")";
            }
        }
        if (compute_at.is_root()) {
            ss << ".compute_root()";
        } else {
            ss << ".compute_at(" << compute_at.func << ", " << compute_var_name << ")";
        }
    }
    ss << ";";
    return ss.str();
}

class StmtUsesFunc : public IRVisitor {
    using IRVisitor::visit;
    string func;
    void visit(const Call *op) {
        if (op->name == func) {
            result = true;
        }
        IRVisitor::visit(op);
    }
public:
    bool result = false;
    StmtUsesFunc(string f) : func(f) {}
};

class PrintUsesOfFunc : public IRVisitor {
    using IRVisitor::visit;

    int indent = 1;
    string func, caller;
    bool last_print_was_ellipsis = false;
    std::ostream &stream;

    void do_indent() {
        for (int i = 0; i < indent; i++) {
            stream << "  ";
        }
    }

    void visit(const For *op) {
        if (ends_with(op->name, Var::outermost().name()) ||
            ends_with(op->name, LoopLevel::root().var)) {
            IRVisitor::visit(op);
        } else {

            int old_indent = indent;

            StmtUsesFunc uses(func);
            op->body.accept(&uses);
            if (!uses.result) {
                if (!last_print_was_ellipsis) {
                    do_indent();
                    stream << "...\n";
                    last_print_was_ellipsis = true;
                }
            } else {
                do_indent();
                stream << "for " << op->name << ":\n";
                last_print_was_ellipsis = false;
                indent++;
            }

            IRVisitor::visit(op);
            indent = old_indent;
        }
    }

    void visit(const ProducerConsumer *op) {
        string old_caller = caller;
        caller = op->name;
        op->produce.accept(this);
        if (op->update.defined()) {
            op->update.accept(this);
        }
        caller = old_caller;
        op->consume.accept(this);
    }

    void visit(const Call *op) {
        if (op->name == func) {
            do_indent();
            stream << caller << " uses " << func << "\n";
            last_print_was_ellipsis = false;
        } else {
            IRVisitor::visit(op);
        }
    }

public:
    PrintUsesOfFunc(string f, std::ostream &s) : func(f), stream(s) {}
};

void validate_schedule(Function f, Stmt s, const Target &target, bool is_output) {

    // If f is extern, check that none of its inputs are scheduled inline.
    if (f.has_extern_definition()) {
        for (const ExternFuncArgument &arg : f.extern_arguments()) {
            if (arg.is_func()) {
                Function g(arg.func);
                if (g.schedule().compute_level().is_inline()) {
                    user_error
                        << "Func " << g.name() << " cannot be scheduled to be computed inline, "
                        << "because it is used in the externally-computed function " << f.name() << "\n";
                }
            }
        }
    }

    // Emit a warning if only some of the steps have been scheduled.
    bool any_scheduled = f.schedule().touched();
    for (const UpdateDefinition &r : f.updates()) {
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

    // If the func is scheduled on the gpu, check that the relevant
    // api is enabled in the target.
    vector<Schedule> schedules;
    schedules.push_back(f.schedule());
    for (const UpdateDefinition &u : f.updates()) {
        schedules.push_back(u.schedule);
    }
    for (size_t i = 0; i < schedules.size(); i++) {
        for (const Specialization &s : schedules[i].specializations()) {
            schedules.push_back(s.schedule);
        }
    }
    for (const Schedule &s : schedules) {
        for (const Dim &d : s.dims()) {
            if (!target.supports_device_api(d.device_api)) {
                user_error << "Schedule for Func " << f.name()
                           << " requires " << d.device_api
                           << " but no compatible target feature is enabled in target "
                           << target.to_string() << "\n";
            }
        }
    }

    LoopLevel store_at = f.schedule().store_level();
    LoopLevel compute_at = f.schedule().compute_level();

    // Outputs must be compute_root and store_root. They're really
    // store_in_user_code, but store_root is close enough.
    if (is_output) {
        if (store_at.is_root() && compute_at.is_root()) {
            return;
        } else {
            user_error << "Func " << f.name() << " is the output, so must"
                       << " be scheduled compute_root (which is the default).\n";
        }
    }

    // Inlining is always allowed
    if (store_at.is_inline() && compute_at.is_inline()) {
        return;
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
                err << "Func \"" << f.name()
                    << "\" is stored outside the parallel loop over "
                    << sites[i].loop_level.func << "." << sites[i].loop_level.var
                    << " but computed within it. This is a potential race condition.\n";
                store_at_ok = compute_at_ok = false;
            }
        }
    }

    if (!store_at_ok || !compute_at_ok) {
        err << "Func \"" << f.name() << "\" is computed at the following invalid location:\n"
            << "  " << schedule_to_source(f, store_at, compute_at) << "\n"
            << "Legal locations for this function are:\n";
        for (size_t i = 0; i < sites.size(); i++) {
            err << "  " << schedule_to_source(f, sites[i].loop_level, sites[i].loop_level) << "\n";
        }
        err << "\"" << f.name() << "\" is used in the following places:\n";
        PrintUsesOfFunc printer(f.name(), err);
        s.accept(&printer);

        user_error << err.str();
    }
}

class RemoveLoopsOverOutermost : public IRMutator {
    using IRMutator::visit;

    void visit(const For *op) {
        if (ends_with(op->name, ".__outermost") && is_one(simplify(op->extent))) {
            stmt = mutate(substitute(op->name, op->min, op->body));
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const LetStmt *op) {
        if (ends_with(op->name, ".__outermost.loop_extent") ||
            ends_with(op->name, ".__outermost.loop_min") ||
            ends_with(op->name, ".__outermost.loop_max")) {
            stmt = mutate(substitute(op->name, simplify(op->value), op->body));
        } else {
            IRMutator::visit(op);
        }
    }
};

Stmt schedule_functions(const vector<Function> &outputs,
                        const vector<string> &order,
                        const map<string, Function> &env,
                        const Target &target,
                        bool &any_memoized) {

    string root_var = LoopLevel::root().func + "." + LoopLevel::root().var;
    Stmt s = For::make(root_var, 0, 1, ForType::Serial, DeviceAPI::Host, Evaluate::make(0));

    any_memoized = false;

    for (size_t i = order.size(); i > 0; i--) {
        Function f = env.find(order[i-1])->second;

        bool is_output = false;
        for (Function o : outputs) {
            is_output |= o.same_as(f);
        }

        validate_schedule(f, s, target, is_output);

        if (f.has_pure_definition() &&
            !f.has_update_definition() &&
            f.schedule().compute_level().is_inline()) {
            debug(1) << "Inlining " << order[i-1] << '\n';
            s = inline_function(s, f);
        } else {
            debug(1) << "Injecting realization of " << order[i-1] << '\n';
            InjectRealization injector(f, is_output, target);
            s = injector.mutate(s);
            internal_assert(injector.found_store_level && injector.found_compute_level);
        }
        any_memoized = any_memoized || f.schedule().memoized();
        debug(2) << s << '\n';
    }

    // We can remove the loop over root now
    const For *root_loop = s.as<For>();
    internal_assert(root_loop);
    s = root_loop->body;

    // We can also remove all the loops over __outermost now.
    s = RemoveLoopsOverOutermost().mutate(s);

    return s;

}

}
}
