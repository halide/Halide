#include <algorithm>
#include <set>
#include <utility>

#include "ApplySplit.h"
#include "CSE.h"
#include "CodeGen_GPU_Dev.h"
#include "ExprUsesVar.h"
#include "Func.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Inline.h"
#include "Prefetch.h"
#include "Qualify.h"
#include "ScheduleFunctions.h"
#include "Simplify.h"
#include "Solve.h"
#include "Substitute.h"
#include "Target.h"
#include "Var.h"

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace {

// A structure representing a containing LetStmt, IfThenElse, or For
// loop. Used in build_provide_loop_nest below. Both If and IfInner represent
// IfThenElse stmts, however, IfInner should not be reordered to outside of
// a for loop.
struct Container {
    enum Type { For,
                Let,
                If,
                IfInner };
    Type type;
    // If it's a for loop, the index in the dims list.
    int dim_idx;
    string name;
    Expr value;

    Container(Type type, int dim_idx, string name, Expr value)
        : type(type), dim_idx(dim_idx), name(std::move(name)), value(std::move(value)) {
    }
};

bool var_name_match(const string &v1, const string &v2) {
    return ((v1 == v2) ||
            Internal::ends_with(v1, "." + v2) ||
            Internal::ends_with(v2, "." + v1));
}

class ContainsImpureCall : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Call *op) override {
        if (!op->is_pure()) {
            result = true;
        } else {
            IRVisitor::visit(op);
        }
    }

public:
    bool result = false;
};

bool contains_impure_call(const Expr &expr) {
    ContainsImpureCall is_not_pure;
    expr.accept(&is_not_pure);
    return is_not_pure.result;
}

// A mutator that performs a substitute operation only on either the values or the
// arguments of Provide nodes.
class SubstituteIn : public IRGraphMutator {
    const string &name;
    const Expr &value;
    bool calls;
    bool provides;

    using IRMutator::visit;

    Stmt visit(const Provide *p) override {
        if (!provides) {
            return IRMutator::visit(p);
        }
        vector<Expr> args;
        bool changed = false;
        for (const Expr &i : p->args) {
            args.push_back(graph_substitute(name, value, i));
            changed = changed || !args.back().same_as(i);
        }
        if (changed) {
            return Provide::make(p->name, p->values, args, p->predicate);
        } else {
            return p;
        }
    }

    Expr visit(const Call *op) override {
        Expr result = IRMutator::visit(op);
        if (calls && op->call_type == Call::Halide) {
            result = graph_substitute(name, value, op);
        }
        return result;
    }

public:
    SubstituteIn(const string &name, const Expr &value, bool calls, bool provides)
        : name(name), value(value), calls(calls), provides(provides) {
    }
};

Stmt substitute_in(const string &name, const Expr &value, bool calls, bool provides, const Stmt &s) {
    return SubstituteIn(name, value, calls, provides).mutate(s);
}

class AddPredicates : public IRGraphMutator {
    const Expr &cond;
    const Function &func;
    ApplySplitResult::Type type;

    using IRMutator::visit;

    Stmt visit(const Provide *p) override {
        auto [args, changed_args] = mutate_with_changes(p->args);
        auto [values, changed_values] = mutate_with_changes(p->values);
        Expr predicate = mutate(p->predicate);
        if (type == ApplySplitResult::BlendProvides) {
            int idx = 0;
            for (Expr &v : values) {
                v = select(cond, v, Call::make(func, args, idx++));
            }
            return Provide::make(p->name, values, args, predicate);
        } else if (type == ApplySplitResult::PredicateProvides) {
            return Provide::make(p->name, values, args, predicate && cond);
        } else if (changed_args || changed_values || !predicate.same_as(p->predicate)) {
            return Provide::make(p->name, values, args, predicate);
        } else {
            return p;
        }
    }

    Expr visit(const Call *op) override {
        Expr result = IRMutator::visit(op);
        if (type == ApplySplitResult::PredicateCalls && op->call_type == Call::Halide) {
            result = Call::make(op->type, Call::if_then_else, {cond, result}, Call::PureIntrinsic);
        }
        return result;
    }

public:
    AddPredicates(const Expr &cond, const Function &func, ApplySplitResult::Type type)
        : cond(cond), func(func), type(type) {
    }
};

Stmt add_predicates(const Expr &cond, const Function &func, ApplySplitResult::Type type, const Stmt &s) {
    return AddPredicates(cond, func, type).mutate(s);
}

// Build a loop nest about a provide node using a schedule
Stmt build_loop_nest(
    const Stmt &body,
    const string &prefix,
    int start_fuse,
    const Function &func,
    const Definition &def,
    bool is_update) {
    const auto &dims = func.args();
    const auto &func_s = func.schedule();
    const auto &stage_s = def.schedule();
    const auto &predicates = def.split_predicate();

    // We'll build it from inside out, starting from the body,
    // then wrapping it in for loops.
    Stmt stmt = body;

    // A map of the dimensions for which we know the extent is a
    // multiple of some Expr. This can happen due to a bound, or
    // align_bounds directive, or if a dim comes from the inside
    // of a split.
    map<string, Expr> dim_extent_alignment;

    // First hunt through the bounds for them.
    for (const Bound &i : func_s.bounds()) {
        if (i.extent.defined()) {
            dim_extent_alignment[i.var] = i.extent;
        }
        if (i.modulus.defined()) {
            dim_extent_alignment[i.var] = i.modulus;
        }
    }
    // Then use any reduction domain.
    for (const ReductionVariable &i : stage_s.rvars()) {
        dim_extent_alignment[i.var] = i.extent;
    }

    vector<Split> splits = stage_s.splits();

    // Find all the predicated inner variables. We can't split these.
    set<string> predicated_vars;
    for (const Split &split : splits) {
        if (split.tail == TailStrategy::PredicateLoads || split.tail == TailStrategy::PredicateStores) {
            predicated_vars.insert(split.inner);
        }
    }

    // Define the function args in terms of the loop variables using the splits
    for (const Split &split : splits) {
        user_assert(predicated_vars.count(split.old_var) == 0)
            << "Cannot split a loop variable resulting from a split using PredicateLoads or PredicateStores.";

        vector<ApplySplitResult> splits_result = apply_split(split, is_update, prefix, dim_extent_alignment);

        // To ensure we substitute all indices used in call or provide,
        // we need to substitute all lets in, so we correctly guard x in
        // an example like let a = 2*x in a + f[a].
        stmt = substitute_in_all_lets(stmt);
        for (const auto &res : splits_result) {
            if (res.is_substitution()) {
                stmt = graph_substitute(res.name, res.value, stmt);
            } else if (res.is_substitution_in_calls()) {
                stmt = substitute_in(res.name, res.value, true, false, stmt);
            } else if (res.is_substitution_in_provides()) {
                stmt = substitute_in(res.name, res.value, false, true, stmt);
            } else if (res.is_blend_provides() ||
                       res.is_predicate_calls() ||
                       res.is_predicate_provides()) {
                stmt = add_predicates(res.value, func, res.type, stmt);
            } else if (res.is_let()) {
                stmt = LetStmt::make(res.name, res.value, stmt);
            } else {
                internal_assert(res.is_predicate());
                stmt = IfThenElse::make(res.value, stmt, Stmt());
            }
        }
        stmt = common_subexpression_elimination(stmt);
    }

    // Order the Ifs, Fors, and Lets for bounds inference
    // to generate tighter bounds and put the bound variables
    // in the right place.
    // This is not a generic loop invariant code motion step.
    // In particular there are dangling references to bound
    // variables that are not defined yet, so we can't rely
    // the loop invariant code motion pass.

    // All containing lets and fors. Outermost first.
    vector<Container> nest;
    nest.reserve(stage_s.dims().size());

    // Put the desired loop nest into the containers vector.
    for (int i = (int)stage_s.dims().size() - 1; i >= 0; i--) {
        const Dim &dim = stage_s.dims()[i];
        nest.emplace_back(Container::For, i, prefix + dim.var, Expr());
    }

    vector<Container> pred_container;
    // Strip off the lets/ifs into the containers vector.
    while (!stmt.same_as(body)) {
        const auto *let = stmt.as<LetStmt>();
        const auto *if_else = stmt.as<IfThenElse>();
        if (let) {
            nest.emplace_back(Container::Let, 0, let->name, let->value);
            stmt = let->body;
        } else if (if_else && !if_else->else_case.defined()) {
            pred_container.emplace_back(Container::If, 0, "", if_else->condition);
            stmt = if_else->then_case;
        } else {
            break;
        }
    }

    // Add appropriate predicates on the fused loop vars to ensure we don't
    // go out of bounds. Ignore the __outermost dims since it's going to be
    // removed later anyway. These have to be added as outermost as possible as
    // some let stmts (e.g. the rebase let stmt) might depend on this vars;
    // otherwise, this may mess up the bounds_touched computation.
    int n_predicates_inner = 0;
    for (int i = start_fuse; (i >= 0) && (i < (int)stage_s.dims().size() - 1); ++i) {
        string dim_var = prefix + stage_s.dims()[i].var;
        Expr var = Variable::make(Int(32), dim_var);
        Expr max = Variable::make(Int(32), dim_var + ".loop_max");
        Expr min = Variable::make(Int(32), dim_var + ".loop_min");
        // Use 'var', the variable which bounds we're constraining as the
        // container name, so that we can use it later to check if a LetStmt
        // value depends on 'var'.
        nest.emplace_back(Container::IfInner, 0, dim_var, likely(var >= min));
        nest.emplace_back(Container::IfInner, 0, dim_var, likely(var <= max));
        n_predicates_inner += 2;
    }

    // Put all the reduction domain predicates into the containers vector.
    for (Expr pred : predicates) {
        pred = qualify(prefix, pred);
        // Add a likely qualifier if there isn't already one
        if (Call::as_intrinsic(pred, {Call::likely, Call::likely_if_innermost})) {
            pred = likely(pred);
        }
        pred_container.emplace_back(Container::If, 0, "", pred);
    }
    int n_predicates = (int)(pred_container.size());

    nest.insert(nest.end(), pred_container.begin(), pred_container.end());

    // Resort the containers vector so that lets are as far outwards
    // as possible. Use reverse insertion sort. Start at the first letstmt.
    for (int i = (int)stage_s.dims().size(); i < (int)nest.size() - n_predicates_inner - n_predicates; i++) {
        // Only push up LetStmts.
        internal_assert(nest[i].value.defined());
        internal_assert(nest[i].type == Container::Let);
        if (!is_pure(nest[i].value)) {
            continue;
        }

        for (int j = i - 1; j >= 0; j--) {
            // Try to push it up by one.
            internal_assert(nest[j + 1].value.defined());
            if (!expr_uses_var(nest[j + 1].value, nest[j].name)) {
                std::swap(nest[j + 1], nest[j]);
            } else {
                break;
            }
        }
    }

    // Sort the predicate guards for the fused loops so they are as far outwards
    // as possible. IfInnner should not be reordered to outside of a for loop.
    for (int i = (int)nest.size() - n_predicates_inner - n_predicates;
         i < (int)nest.size() - n_predicates;
         i++) {
        // Only push up IfThenElse.
        internal_assert(nest[i].value.defined());
        internal_assert(nest[i].type == Container::IfInner);

        // Cannot lift out the predicate guard if it contains call to non-pure function
        if (contains_impure_call(nest[i].value)) {
            continue;
        }

        for (int j = i - 1; j >= 0; j--) {
            // Try to push it up by one.
            internal_assert(nest[j + 1].value.defined());

            if (!expr_uses_var(nest[j + 1].value, nest[j].name) &&
                (nest[j].type != Container::For)) {
                std::swap(nest[j + 1], nest[j]);
            } else {
                break;
            }
        }
    }

    // Sort the ifs so they are as far outwards as possible.
    // BoxesTouched trims the domain of a variable within a scope of if-then-else
    // based on the likely condition. However, it doesn't do it transitively; it
    // doesn't trim the domains of other variables that directly/indirectly
    // depend on the original variable in the likely condition outside the scope
    // of the if-then-else. That's why it's necessary to move the ifs as far
    // outwards as possible, so that those variables can have tighter bounds.
    for (int i = (int)nest.size() - n_predicates; i < (int)nest.size(); i++) {
        // Only push up IfThenElse.
        internal_assert(nest[i].value.defined());
        internal_assert(nest[i].type == Container::If);

        // Cannot lift out the 'if' if it contains call to non-pure function
        if (contains_impure_call(nest[i].value)) {
            continue;
        }

        for (int j = i - 1; j >= 0; j--) {
            // Try to push it up by one.
            internal_assert(nest[j + 1].value.defined());

            if (!expr_uses_var(nest[j + 1].value, nest[j].name)) {
                std::swap(nest[j + 1], nest[j]);
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
        } else if ((nest[i].type == Container::If) || (nest[i].type == Container::IfInner)) {
            internal_assert(nest[i].value.defined());
            stmt = IfThenElse::make(nest[i].value, stmt, Stmt());
        } else {
            internal_assert(nest[i].type == Container::For);
            const Dim &dim = stage_s.dims()[nest[i].dim_idx];
            Expr min = Variable::make(Int(32), nest[i].name + ".loop_min");
            Expr extent = Variable::make(Int(32), nest[i].name + ".loop_extent");
            stmt = For::make(nest[i].name, min, extent, dim.for_type, dim.partition_policy, dim.device_api, stmt);
        }
    }

    // Define the bounds on the split dimensions using the bounds
    // on the function args. If it is a purify, we should use the bounds
    // from the dims instead.
    for (size_t i = splits.size(); i > 0; i--) {
        const Split &split = splits[i - 1];

        vector<std::pair<string, Expr>> let_stmts = compute_loop_bounds_after_split(split, prefix);
        for (const auto &let_stmt : let_stmts) {
            stmt = LetStmt::make(let_stmt.first, let_stmt.second, stmt);
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
    for (const std::string &i : dims) {
        string var = prefix + i;
        Expr max = Variable::make(Int(32), var + ".max");
        Expr min = Variable::make(Int(32), var + ".min");  // Inject instance name here? (compute instance names during lowering)
        stmt = LetStmt::make(var + ".loop_extent",
                             (max + 1) - min,
                             stmt);
        stmt = LetStmt::make(var + ".loop_min", min, stmt);
        stmt = LetStmt::make(var + ".loop_max", max, stmt);
    }

    // Define the loop mins and extents for the reduction domain (if there is any)
    // in terms of the mins and maxs produced by bounds inference
    for (const ReductionVariable &rv : stage_s.rvars()) {
        string p = prefix + rv.var;
        Expr rmin = Variable::make(Int(32), p + ".min");
        Expr rmax = Variable::make(Int(32), p + ".max");
        stmt = LetStmt::make(p + ".loop_min", rmin, stmt);
        stmt = LetStmt::make(p + ".loop_max", rmax, stmt);
        stmt = LetStmt::make(p + ".loop_extent", rmax - rmin + 1, stmt);
    }

    return stmt;
}

// Build a loop nest about a provide node using a schedule
Stmt build_provide_loop_nest(const map<string, Function> &env,
                             const string &prefix,
                             const Function &func,
                             const Definition &def,
                             int start_fuse,
                             bool is_update) {

    internal_assert(!is_update == def.is_init());

    // Default stored values
    vector<Expr> site(def.args().size());
    vector<Expr> values(def.values().size());
    for (size_t i = 0; i < values.size(); i++) {
        Expr v = def.values()[i];
        v = qualify(prefix, v);
        values[i] = v;
        debug(3) << "Value " << i << " = " << v << "\n";
    }

    // Default stored locations
    for (size_t i = 0; i < def.args().size(); i++) {
        Expr s = def.args()[i];
        s = qualify(prefix, s);
        site[i] = s;
        debug(3) << "Site " << i << " = " << s << "\n";
    }

    // Make the (multi-dimensional multi-valued) store node.
    Stmt body = Provide::make(func.name(), values, site, const_true());
    if (def.schedule().atomic()) {  // Add atomic node.
        bool any_unordered_parallel = false;
        for (const auto &d : def.schedule().dims()) {
            any_unordered_parallel |= is_unordered_parallel(d.for_type);
        }
        if (any_unordered_parallel) {
            // If required, we will allocate a mutex buffer called func.name() + ".mutex"
            // The buffer is added in the AddAtomicMutex pass.
            body = Atomic::make(func.name(), func.name() + ".mutex", body);
        } else {
            // No mutex is required if there is no parallelism, and it
            // wouldn't work if all parallelism is synchronous
            // (e.g. vectorization). Vectorization and the like will
            // need to handle atomic nodes specially, by either
            // emitting VectorReduce ops or scalarizing.
            body = Atomic::make(func.name(), std::string{}, body);
        }
    }

    // Default schedule/values if there is no specialization
    Stmt stmt = build_loop_nest(body, prefix, start_fuse, func, def, is_update);
    stmt = inject_placeholder_prefetch(stmt, env, prefix, def.schedule().prefetches());

    // Make any specialized copies
    const vector<Specialization> &specializations = def.specializations();
    for (size_t i = specializations.size(); i > 0; i--) {
        const Specialization &s = specializations[i - 1];
        if (s.failure_message.empty()) {
            Stmt then_case = build_provide_loop_nest(env, prefix, func, s.definition, start_fuse, is_update);
            stmt = IfThenElse::make(s.condition, then_case, stmt);
        } else {
            internal_assert(equal(s.condition, const_true()));
            // specialize_fail() should only be possible on the final specialization
            internal_assert(i == specializations.size());
            Expr specialize_fail_error =
                Internal::Call::make(Int(32),
                                     "halide_error_specialize_fail",
                                     {StringImm::make(s.failure_message)},
                                     Internal::Call::Extern);
            // Since this is the final specialization, we can make
            // this the else clause
            stmt = AssertStmt::make(const_false(), specialize_fail_error);
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
Stmt build_extern_produce(const map<string, Function> &env, Function f, const Target &target) {
    // Call the external function

    // Build an argument list
    vector<Expr> extern_call_args;
    const vector<ExternFuncArgument> &args = f.extern_arguments();

    const string &extern_name = f.extern_function_name();

    // We need to generate crops of the input and output buffers if the
    // extern stage has some non-extern loops, aside from the outermost
    // placeholder.
    bool needs_crops = false;
    if (!f.definition().schedule().dims().empty()) {
        size_t extern_count = 0;
        for (const Dim &d : f.definition().schedule().dims()) {
            extern_count += d.for_type == ForType::Extern ? 1 : 0;
        }
        needs_crops = extern_count + 1 < f.definition().schedule().dims().size();
    }

    vector<pair<string, Expr>> lets;

    // Iterate through all of the input args to the extern
    // function building a suitable argument list for the
    // extern function call.
    vector<pair<Expr, int>> buffers_to_annotate;
    vector<Expr> buffers_contents_to_annotate;
    vector<pair<Expr, string>> buffers_to_check;
    vector<pair<Expr, Expr>> cropped_buffers;
    for (const ExternFuncArgument &arg : args) {
        if (arg.is_expr()) {
            extern_call_args.push_back(arg.expr);
        } else if (arg.is_func()) {
            Function input(arg.func);
            if (!needs_crops && input.schedule().store_level() == input.schedule().compute_level()) {
                for (int k = 0; k < input.outputs(); k++) {
                    string buf_name = input.name();
                    if (input.outputs() > 1) {
                        buf_name += "." + std::to_string(k);
                    }
                    buf_name += ".buffer";
                    Expr buffer = Variable::make(type_of<struct halide_buffer_t *>(), buf_name);
                    extern_call_args.push_back(buffer);
                    buffers_to_annotate.emplace_back(buffer, input.dimensions());
                    buffers_contents_to_annotate.push_back(buffer);
                }
            } else {
                // Make a local crop of just the region required,
                // in case the input was folded. We have no
                // protocol for passing folded buffers to extern
                // stages, so if the fold does indeed occur, we'll
                // assert later that the crop doesn't cross over a
                // fold.
                string stage_name = input.name() + ".s" + std::to_string(input.updates().size()) + ".";
                const vector<string> &input_args = input.args();
                for (int k = 0; k < input.outputs(); k++) {
                    string src_buf_name = input.name();
                    if (input.outputs() > 1) {
                        src_buf_name += "." + std::to_string(k);
                    }
                    src_buf_name += ".buffer";
                    Expr src_buffer = Variable::make(type_of<struct halide_buffer_t *>(), src_buf_name);

                    Expr alloca_size = Call::make(Int(32), Call::size_of_halide_buffer_t, {}, Call::Intrinsic);
                    Expr cropped_input = Call::make(type_of<struct halide_buffer_t *>(), Call::alloca,
                                                    {alloca_size}, Call::Intrinsic);

                    vector<Expr> args(5);
                    args[0] = cropped_input;
                    args[1] = Call::make(type_of<struct halide_dimension_t *>(), Call::alloca,
                                         {(int)sizeof(halide_dimension_t) * input.dimensions()}, Call::Intrinsic);
                    args[2] = src_buffer;

                    vector<Expr> mins, extents;
                    internal_assert(input.dimensions() == (int)input_args.size());
                    for (const string &arg : input_args) {
                        string var = stage_name + arg;
                        Expr min = Variable::make(Int(32), var + ".min");
                        Expr max = Variable::make(Int(32), var + ".max");
                        mins.push_back(min);
                        extents.push_back(max - min + 1);
                    }
                    args[3] = Call::make(type_of<const int *>(), Call::make_struct, mins, Call::Intrinsic);
                    args[4] = Call::make(type_of<const int *>(), Call::make_struct, extents, Call::Intrinsic);

                    cropped_input = Call::make(type_of<struct halide_buffer_t *>(), Call::buffer_crop,
                                               args, Call::Extern);

                    string buf_name = input.name() + "." + std::to_string(k) + ".tmp_buffer";
                    extern_call_args.push_back(Variable::make(type_of<struct halide_buffer_t *>(), buf_name));
                    buffers_to_annotate.emplace_back(extern_call_args.back(), input.dimensions());
                    buffers_contents_to_annotate.push_back(cropped_input);
                    cropped_buffers.emplace_back(extern_call_args.back(), src_buffer);
                    lets.emplace_back(buf_name, cropped_input);
                }
            }
        } else if (arg.is_buffer()) {
            Buffer<> b = arg.buffer;
            Parameter p(b.type(), true, b.dimensions(), b.name());
            p.set_buffer(b);
            Expr buf = Variable::make(type_of<struct halide_buffer_t *>(), b.name() + ".buffer", p);
            extern_call_args.push_back(buf);
            buffers_to_annotate.emplace_back(buf, b.dimensions());
            buffers_contents_to_annotate.push_back(buf);
        } else if (arg.is_image_param()) {
            Parameter p = arg.image_param;
            Expr buf = Variable::make(type_of<struct halide_buffer_t *>(), p.name() + ".buffer", p);
            extern_call_args.push_back(buf);
            // Do not annotate ImageParams: both the halide_buffer_t itself,
            // and the contents it points to, should be filled by the caller;
            // if we mark it here, we might mask a missed initialization.
            // buffers_to_annotate.push_back(buf);
            // buffers_contents_to_annotate.push_back(buf);
        } else {
            internal_error << "Bad ExternFuncArgument type\n";
        }
    }

    // Grab the halide_buffer_t's representing the output. If the
    // store level matches the compute level, then we can use the
    // ones already injected by allocation bounds inference. If
    // it's the output to the pipeline then it will similarly be
    // in the symbol table.
    if (!needs_crops && f.schedule().store_level() == f.schedule().compute_level()) {
        for (int j = 0; j < f.outputs(); j++) {
            string buf_name = f.name();
            if (f.outputs() > 1) {
                buf_name += "." + std::to_string(j);
            }
            buf_name += ".buffer";
            Expr buffer = Variable::make(type_of<struct halide_buffer_t *>(), buf_name);
            extern_call_args.push_back(buffer);
            // Since this is a temporary, internal-only buffer, make sure it's marked.
            // (but not the contents! callee is expected to fill that in.)
            buffers_to_annotate.emplace_back(buffer, f.dimensions());
            buffers_to_check.emplace_back(buffer, buf_name);
        }
    } else {
        // Store level doesn't match compute level. Make an output
        // buffer just for this subregion.
        string stage_name = f.name() + ".s0.";
        const vector<string> &f_args = f.args();
        for (int j = 0; j < f.outputs(); j++) {
            string src_buf_name = f.name();
            if (f.outputs() > 1) {
                src_buf_name += "." + std::to_string(j);
            }
            src_buf_name += ".buffer";
            Expr src_buffer = Variable::make(type_of<struct halide_buffer_t *>(), src_buf_name);

            Expr alloca_size = Call::make(Int(32), Call::size_of_halide_buffer_t, {}, Call::Intrinsic);
            Expr output_buffer_t = Call::make(type_of<struct halide_buffer_t *>(), Call::alloca,
                                              {alloca_size}, Call::Intrinsic);

            vector<Expr> args(5);
            args[0] = output_buffer_t;
            args[1] = Call::make(type_of<struct halide_dimension_t *>(), Call::alloca,
                                 {(int)sizeof(halide_dimension_t) * f.dimensions()}, Call::Intrinsic);
            args[2] = src_buffer;

            vector<Expr> mins, extents;
            internal_assert(f.dimensions() == (int)f_args.size());
            for (const string &arg : f_args) {
                string var = stage_name + arg;
                Expr min = Variable::make(Int(32), var + ".min");
                Expr max = Variable::make(Int(32), var + ".max");
                mins.push_back(min);
                extents.push_back(max - min + 1);
            }
            args[3] = Call::make(type_of<const int *>(), Call::make_struct, mins, Call::Intrinsic);
            args[4] = Call::make(type_of<const int *>(), Call::make_struct, extents, Call::Intrinsic);

            output_buffer_t = Call::make(type_of<struct halide_buffer_t *>(), Call::buffer_crop, args,
                                         Call::Extern);

            string buf_name = f.name() + "." + std::to_string(j) + ".tmp_buffer";
            extern_call_args.push_back(Variable::make(type_of<struct halide_buffer_t *>(), buf_name));
            // Since this is a temporary, internal-only buffer, make sure it's marked.
            // (but not the contents! callee is expected to fill that in.)
            buffers_to_annotate.emplace_back(extern_call_args.back(), f.dimensions());
            cropped_buffers.emplace_back(extern_call_args.back(), src_buffer);
            lets.emplace_back(buf_name, output_buffer_t);
            buffers_to_check.emplace_back(extern_call_args.back(), buf_name);
        }
    }

    Stmt pre_call, post_call;
    if (target.has_feature(Target::MSAN)) {
        // Mark the buffers as initialized before calling out.
        for (const auto &p : buffers_to_annotate) {
            Expr buffer = p.first;
            int dimensions = p.second;
            // Return type is really 'void', but no way to represent that in our IR.
            // Precedent (from halide_print, etc) is to use Int(32) and ignore the result.
            Expr sizeof_buffer_t = cast<uint64_t>(
                Call::make(Int(32), Call::size_of_halide_buffer_t, {}, Call::Intrinsic));
            Stmt mark_buffer =
                Evaluate::make(Call::make(Int(32), "halide_msan_annotate_memory_is_initialized",
                                          {buffer, sizeof_buffer_t}, Call::Extern));
            Expr shape = Call::make(type_of<halide_dimension_t *>(), Call::buffer_get_shape, {buffer},
                                    Call::Extern);
            Expr shape_size = Expr((uint64_t)(sizeof(halide_dimension_t) * dimensions));
            Stmt mark_shape =
                Evaluate::make(Call::make(Int(32), "halide_msan_annotate_memory_is_initialized",
                                          {shape, shape_size}, Call::Extern));

            mark_buffer = Block::make(mark_buffer, mark_shape);

            if (!is_no_op(pre_call)) {
                pre_call = Block::make(pre_call, mark_buffer);
            } else {
                pre_call = mark_buffer;
            }
        }
        for (const auto &buffer : buffers_contents_to_annotate) {
            // Return type is really 'void', but no way to represent that in our IR.
            // Precedent (from halide_print, etc) is to use Int(32) and ignore the result.
            Stmt mark_contents = Evaluate::make(
                Call::make(Int(32), "halide_msan_annotate_buffer_is_initialized", {buffer}, Call::Extern));
            if (!is_no_op(pre_call)) {
                pre_call = Block::make(pre_call, mark_contents);
            } else {
                pre_call = mark_contents;
            }
        }
        // Check the output buffer(s) from define_extern() calls to be sure they are fully initialized.
        for (const auto &p : buffers_to_check) {
            Expr buffer = p.first;
            string buf_name = p.second;
            Stmt check_contents = Evaluate::make(
                Call::make(Int(32), "halide_msan_check_buffer_is_initialized", {buffer, Expr(buf_name)}, Call::Extern));
            if (!is_no_op(post_call)) {
                post_call = Block::make(post_call, check_contents);
            } else {
                post_call = check_contents;
            }
        }
    }

    // Make the extern call
    Expr e = f.make_call_to_extern_definition(extern_call_args, target);

    // Check if it succeeded
    string result_name = unique_name('t');
    Expr result = Variable::make(Int(32), result_name);
    Expr error = Call::make(Int(32), "halide_error_extern_stage_failed",
                            {extern_name, result}, Call::Extern);
    Stmt check = AssertStmt::make(EQ::make(result, 0), error);

    if (!cropped_buffers.empty()) {
        // We need to check that all cropped buffers are non-null (since Call::buffer_crop can return nullptr)
        for (const auto &p : cropped_buffers) {
            Expr cropped = p.first;
            Expr cropped_u64 = reinterpret(UInt(64), cropped);
            Expr error = Call::make(Int(32), "halide_error_device_crop_failed", std::vector<Expr>(), Call::Extern);
            Stmt assertion = AssertStmt::make(cropped_u64 != 0, error);

            if (!is_no_op(pre_call)) {
                pre_call = Block::make(pre_call, assertion);
            } else {
                pre_call = assertion;
            }
        }

        // We need to clean up the temporary crops we made for the
        // outputs in case any of them have device allocations.
        vector<Expr> cleanup_args;

        // Make a struct with the buffers and their uncropped parents
        for (const auto &p : cropped_buffers) {
            // The cropped halide_buffer_t
            cleanup_args.push_back(p.first);
            // Its parent
            cleanup_args.push_back(p.second);
        }

        if (cropped_buffers.size() > 1) {
            // NULL-terminate it
            cleanup_args.push_back(make_zero(type_of<struct halide_buffer_t *>()));
        }

        Expr cleanup_struct = Call::make(Handle(),
                                         Call::make_struct,
                                         cleanup_args,
                                         Call::Intrinsic);

        // Insert cleanup before checking the result of the extern stage.
        string destructor_name = unique_name('d');
        const char *fn = (cropped_buffers.size() == 1 ? "_halide_buffer_retire_crop_after_extern_stage" : "_halide_buffer_retire_crops_after_extern_stage");
        Expr cleanup = Call::make(Int(32), fn, {cleanup_struct}, Call::Extern);
        check = Block::make(Evaluate::make(cleanup), check);
    }

    check = LetStmt::make(result_name, e, check);

    if (pre_call.defined()) {
        check = Block::make(pre_call, check);
    }

    for (const auto &let : lets) {
        check = LetStmt::make(let.first, let.second, check);
    }

    if (post_call.defined()) {
        check = Block::make(check, post_call);
    }

    Definition f_def_no_pred = f.definition().get_copy();
    f_def_no_pred.predicate() = const_true();
    return build_loop_nest(check, f.name() + ".s0.", -1, f, f_def_no_pred, false);
}

// A schedule may include explicit bounds on some dimension. This
// injects assertions that check that those bounds are sufficiently
// large to cover the inferred bounds required.
Stmt inject_explicit_bounds(Stmt body, Function func) {
    const FuncSchedule &s = func.schedule();
    for (size_t stage = 0; stage <= func.updates().size(); stage++) {
        for (auto b : s.bounds()) {
            string prefix = func.name() + ".s" + std::to_string(stage) + "." + b.var;
            string min_name = prefix + ".min_unbounded";
            string max_name = prefix + ".max_unbounded";
            Expr min_var = Variable::make(Int(32), min_name);
            Expr max_var = Variable::make(Int(32), max_name);
            if (!b.min.defined()) {
                b.min = min_var;
            }
            if (!b.extent.defined()) {
                // This is just a bounds alignment, which always expands the region computed.
                continue;
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
    const string &func;

    using IRVisitor::visit;

    void visit(const Call *op) override {
        IRVisitor::visit(op);
        if (op->name == func) {
            result = true;
        }
    }

    // A reference to the function's buffers counts as a use
    void visit(const Variable *op) override {
        if (op->type.is_handle() &&
            starts_with(op->name, func + ".") &&
            ends_with(op->name, ".buffer")) {
            result = true;
        }
    }

public:
    bool result = false;
    explicit IsUsedInStmt(const Function &f)
        : func(f.name()) {
    }
};

// Check if function 'f' is ever used in Stmt 's'.
bool function_is_used_in_stmt(const Function &f, const Stmt &s) {
    IsUsedInStmt is_called(f);
    s.accept(&is_called);
    return is_called.result;
}

class IsRealizedInStmt : public IRVisitor {
    const string &func;

    using IRVisitor::visit;

    void visit(const Realize *op) override {
        IRVisitor::visit(op);
        result = result || (op->name == func);
    }

public:
    bool result = false;

    explicit IsRealizedInStmt(const Function &f)
        : func(f.name()) {
    }
};

// Check if function 'f' is already realized in Stmt 's'.
bool function_is_already_realized_in_stmt(const Function &f, const Stmt &s) {
    IsRealizedInStmt is_realized(f);
    s.accept(&is_realized);
    return is_realized.result;
}

class InjectStmt : public IRMutator {
public:
    const Stmt &injected_stmt;
    bool found_level = false;
    const LoopLevel &level;

    InjectStmt(const Stmt &s, const LoopLevel &level)
        : injected_stmt(s), level(level) {
    }

private:
    using IRMutator::visit;

    Stmt visit(const For *for_loop) override {
        Stmt body = mutate(for_loop->body);

        if (level.match(for_loop->name)) {
            body = Block::make(body, injected_stmt);
            found_level = true;
        }

        if (body.same_as(for_loop->body)) {
            return for_loop;
        } else {
            return For::make(for_loop->name,
                             for_loop->min,
                             for_loop->extent,
                             for_loop->for_type,
                             for_loop->partition_policy,
                             for_loop->device_api,
                             body);
        }
    }
};

// Inject 'injected' into 'root' at 'level'.
Stmt inject_stmt(Stmt root, Stmt injected, const LoopLevel &level) {
    if (!root.defined()) {
        return injected;
    }
    if (!injected.defined()) {
        return root;
    }
    if (level.is_inlined() || level.is_root()) {
        return Block::make(root, injected);
    }
    InjectStmt injector(injected, level);
    root = injector.mutate(root);
    internal_assert(injector.found_level);
    return root;
}

// Collect all let stmts that define the loop min, max, and extent.
class CollectBounds : public IRVisitor {
public:
    template<typename T>
    static map<string, Expr> collect_bounds(const T &node) {
        CollectBounds bounds;
        node.accept(&bounds);
        return bounds.bounds;
    }

private:
    map<string, Expr> bounds;

    using IRVisitor::visit;

    void visit(const LetStmt *op) override {
        if (ends_with(op->name, ".loop_min") ||
            ends_with(op->name, ".loop_max") ||
            ends_with(op->name, ".loop_extent")) {
            bounds.emplace(op->name, Variable::make(Int(32), op->name));
        }
        IRVisitor::visit(op);
    }
};

class SubstituteFusedBounds : public IRMutator {
public:
    const map<string, Expr> &replacements;
    explicit SubstituteFusedBounds(const map<string, Expr> &r)
        : replacements(r) {
    }

private:
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        const auto *min_var = op->min.as<Variable>();
        const auto *extent_var = op->extent.as<Variable>();
        if (min_var && extent_var) {
            Expr min_val, extent_val;
            {
                const auto &it = replacements.find(min_var->name);
                if (it != replacements.end()) {
                    min_val = it->second;
                }
            }
            {
                const auto &it = replacements.find(extent_var->name);
                if (it != replacements.end()) {
                    extent_val = it->second;
                }
            }
            if (!min_val.defined() || !extent_val.defined()) {
                return IRMutator::visit(op);
            }

            Stmt body = mutate(op->body);

            size_t last_dot = op->name.rfind('.');
            internal_assert(last_dot != string::npos);
            string new_var = op->name.substr(0, last_dot) + ".fused." + op->name.substr(last_dot + 1);

            ForType for_type = op->for_type;
            DeviceAPI device_api = op->device_api;
            if (is_const_one(extent_val)) {
                // This is the child loop of a fused group. The real loop of the
                // fused group is the loop of the parent function of the fused
                // group. This child loop is just a scheduling point, and should
                // never be a device transition, so we rewrite it to be a simple
                // serial loop of extent 1."
                for_type = ForType::Serial;
                device_api = DeviceAPI::None;
            }

            Stmt stmt = For::make(new_var, Variable::make(Int(32), new_var + ".loop_min"),
                                  Variable::make(Int(32), new_var + ".loop_extent"),
                                  for_type, op->partition_policy, device_api, body);

            // Add let stmts defining the bound of the renamed for-loop.
            stmt = LetStmt::make(new_var + ".loop_min", min_val, stmt);
            stmt = LetStmt::make(new_var + ".loop_max", simplify(min_val + extent_val - 1), stmt);
            stmt = LetStmt::make(new_var + ".loop_extent", extent_val, stmt);
            // Replace any reference to the old loop name with the new one.
            stmt = substitute(op->name, Variable::make(Int(32), new_var), stmt);
            return stmt;
        } else {
            return IRMutator::visit(op);
        }
    }
};

// The bounds of every loop exist in 'replacements' should be replaced. The
// loop is also renamed by adding '.fused' in the original name before the
// variable name.
Stmt substitute_fused_bounds(Stmt s, const map<string, Expr> &replacements) {
    if (!s.defined() || replacements.empty()) {
        return s;
    } else {
        return SubstituteFusedBounds(replacements).mutate(s);
    }
}

// Shift the iteration domain of a loop nest by some factor.
class ShiftLoopNest : public IRMutator {
    const map<string, Expr> &shifts;  // Add the shift factor to the old var

    using IRMutator::visit;

    Stmt visit(const For *op) override {
        Stmt stmt = IRMutator::visit(op);
        const auto &iter = shifts.find(op->name);
        if (iter != shifts.end()) {
            debug(5) << "...Shifting for loop \"" << op->name << "\" by " << iter->second << "\n";
            op = stmt.as<For>();
            internal_assert(op);
            Expr adjusted = Variable::make(Int(32), op->name) + iter->second;
            Stmt body = substitute(op->name, adjusted, op->body);
            stmt = For::make(op->name, op->min, op->extent, op->for_type, op->partition_policy, op->device_api, body);
        }
        return stmt;
    }

public:
    explicit ShiftLoopNest(const map<string, Expr> &s)
        : shifts(s) {
    }

    template<typename T>
    static T apply_shift(const map<string, Expr> &shifts, const T &node) {
        if (shifts.empty()) {
            return node;
        }
        ShiftLoopNest visitor(shifts);
        return visitor.mutate(node);
    }
};

struct PlaceholderPrefetch {
    const string &name;
    const vector<Type> &types;
    const PrefetchDirective &prefetch;

    PlaceholderPrefetch(const string &name, const vector<Type> &types, const PrefetchDirective &prefetch)
        : name(name),
          types(types),
          prefetch(prefetch) {
    }
};

class InjectFunctionRealization : public IRMutator {
public:
    InjectFunctionRealization(const vector<Function> &funcs,
                              const vector<bool> &is_output_list,
                              const Target &target,
                              const map<string, Function> &env)
        : funcs(funcs),
          is_output_list(is_output_list),
          target(target),
          env(env),
          compute_level(funcs[0].schedule().compute_level()) {
    }

    bool found_compute_level() const {
        return _found_compute_level;
    }
    bool found_store_level() const {
        return _found_store_levels_for_funcs.size() == funcs.size();
    }
    bool found_hoist_storage_level() const {
        return _found_hoist_storage_levels_for_funcs.size() == funcs.size();
    }

protected:
    bool _found_compute_level{};
    std::set<string> _found_store_levels_for_funcs;
    std::set<string> _found_hoist_storage_levels_for_funcs;

    using IRMutator::visit;

    Stmt visit(const For *for_loop) override {
        debug(3) << "Injecting " << funcs << " entering for-loop over " << for_loop->name << "\n";
        Stmt body = for_loop->body;

        // Dig through any placeholder prefetches
        vector<PlaceholderPrefetch> placeholder_prefetches;
        while (const auto *p = body.as<Prefetch>()) {
            placeholder_prefetches.emplace_back(p->name, p->types, p->prefetch);
            body = p->body;
        }

        // Dig through any let/if statements
        vector<pair<string, Expr>> containers;
        while (true) {
            if (const LetStmt *l = body.as<LetStmt>()) {
                const Call *promise_clamped = Call::as_intrinsic(l->value, {Call::promise_clamped});
                if (!promise_clamped && !is_pure(l->value)) {
                    // The consumer of the Func we're injecting may be an
                    // extern stage, which shows up in the IR as a let
                    // stmt with a side-effecty RHS. We need to take care
                    // not to blow past it and risk injecting the producer
                    // *after* the consumer. In general it seems unwise to
                    // reorder the computation of a Func past something
                    // side-effecty, so we stop here.
                    //
                    // An exception is that it's good to walk inside a
                    // promise_clamped intrinsic due to a GuardWithIf
                    // split. It's safe and produces cleaner IR.
                    break;
                }
                containers.emplace_back(l->name, l->value);
                body = l->body;
            } else if (const IfThenElse *i = body.as<IfThenElse>()) {
                if (!is_pure(i->condition) || i->else_case.defined()) {
                    break;
                }
                containers.emplace_back(std::string{}, i->condition);
                body = i->then_case;
            } else {
                break;
            }
        }

        // Fused pairs (compute_with) cannot have extern definitions. Thus this condition is only true when funcs
        // contains a single function to be lowered. Can't schedule extern things inside a vector for loop.
        if (funcs[0].has_extern_definition() &&
            funcs[0].schedule().compute_level().is_inlined() &&
            for_loop->for_type == ForType::Vectorized &&
            !function_is_already_realized_in_stmt(funcs[0], for_loop) &&
            function_is_used_in_stmt(funcs[0], for_loop)) {

            // If we're trying to inline an extern function, schedule it here and bail out
            debug(2) << "Injecting realization of " << funcs[0].name() << " around node " << Stmt(for_loop) << "\n";

            Stmt stmt = build_realize(build_pipeline_group(for_loop), funcs[0], is_output_list[0]);
            _found_compute_level = true;
            _found_store_levels_for_funcs.insert(funcs[0].name());
            _found_hoist_storage_levels_for_funcs.insert(funcs[0].name());
            return stmt;
        }

        body = mutate(body);

        if (compute_level.match(for_loop->name)) {
            debug(3) << "Found compute level at " << for_loop->name << "\n";
            body = build_pipeline_group(body);
            _found_compute_level = true;
        }

        if (_found_compute_level) {
            for (size_t i = 0; i < funcs.size(); i++) {
                if (funcs[i].schedule().store_level().match(for_loop->name)) {
                    debug(3) << "Found store level for " << funcs[i].name() << " at " << for_loop->name << "\n";
                    body = build_realize_function_from_group(body, i);
                    _found_store_levels_for_funcs.insert(funcs[i].name());
                }
            }
            for (size_t i = 0; i < funcs.size(); i++) {
                if (funcs[i].schedule().hoist_storage_level().match(for_loop->name)) {
                    debug(3) << "Found hoist storage level for " << funcs[i].name() << " at " << for_loop->name << "\n";
                    if (funcs[i].schedule().hoist_storage_level() != funcs[i].schedule().store_level()) {
                        body = HoistedStorage::make(funcs[i].name(), body);
                    } else {
                    }
                    _found_hoist_storage_levels_for_funcs.insert(funcs[i].name());
                }
            }
        }

        // Reinstate the let/if statements
        for (size_t i = containers.size(); i > 0; i--) {
            auto p = containers[i - 1];
            if (p.first.empty()) {
                body = IfThenElse::make(p.second, body);
            } else {
                body = LetStmt::make(p.first, p.second, body);
            }
        }

        // Reinstate the placeholder prefetches
        for (size_t i = placeholder_prefetches.size(); i > 0; i--) {
            body = Prefetch::make(placeholder_prefetches[i - 1].name,
                                  placeholder_prefetches[i - 1].types,
                                  Region(),
                                  placeholder_prefetches[i - 1].prefetch,
                                  const_true(),
                                  body);
        }

        // Skips pointless allocation
        if (body.same_as(for_loop->body)) {
            return for_loop;
        } else {
            return For::make(for_loop->name,
                             for_loop->min,
                             for_loop->extent,
                             for_loop->for_type,
                             for_loop->partition_policy,
                             for_loop->device_api,
                             body);
        }
    }

    // If we're an inline update or extern, we may need to inject a realization around
    // the Provide node (or a Provide node surrounded by an Atomic).
    Stmt inline_to_provide(const std::string &provide_name, Stmt provide_op) {
        // none of the functions in a fused group can be inlined, so this will only
        // happen when we're lowering a single func.
        if (provide_name != funcs[0].name() &&
            !funcs[0].is_pure() &&
            funcs[0].schedule().compute_level().is_inlined() &&
            function_is_used_in_stmt(funcs[0], provide_op)) {

            // Prefix all calls to func in op
            Stmt stmt = build_realize(build_pipeline_group(provide_op), funcs[0], is_output_list[0]);
            _found_compute_level = true;
            _found_store_levels_for_funcs.insert(funcs[0].name());
            _found_hoist_storage_levels_for_funcs.insert(funcs[0].name());
            return stmt;
        }

        return provide_op;
    }

    Stmt visit(const Provide *op) override {
        return inline_to_provide(op->name, op);
    }

    Stmt visit(const Atomic *op) override {
        return inline_to_provide(op->producer_name, op);
    }

private:
    const vector<Function> &funcs;
    const vector<bool> &is_output_list;
    const Target &target;
    const map<string, Function> &env;
    const LoopLevel &compute_level;

    Stmt build_realize(Stmt s, const Function &func, bool is_output) {
        if (func.has_extern_definition()) {
            // Add an annotation to let bounds inference know that
            // this will write to the entire bounds required.
            vector<Expr> args;
            args.emplace_back(Variable::make(Handle(), func.name()));
            for (int i = 0; i < func.dimensions(); i++) {
                string prefix = func.name() + ".s0." + func.args()[i];
                string min_name = prefix + ".min";
                string max_name = prefix + ".max";

                args.emplace_back(Variable::make(Int(32), min_name));
                args.emplace_back(Variable::make(Int(32), max_name));
            }
            Expr decl = Call::make(Int(32), Call::declare_box_touched, args, Call::Intrinsic);
            s = Block::make(Evaluate::make(decl), s);
        }

        if (!is_output) {
            Region bounds;
            const string &name = func.name();
            const vector<string> &func_args = func.args();
            for (int i = 0; i < func.dimensions(); i++) {
                const string &arg = func_args[i];
                Expr min = Variable::make(Int(32), name + "." + arg + ".min_realized");
                Expr extent = Variable::make(Int(32), name + "." + arg + ".extent_realized");
                bounds.emplace_back(min, extent);
            }

            s = Realize::make(name, func.output_types(), func.schedule().memory_type(), bounds, const_true(), s);
        }

        // This is also the point at which we inject explicit bounds
        // for this realization.
        if (target.has_feature(Target::NoAsserts)) {
            return s;
        } else {
            return inject_explicit_bounds(s, func);
        }
    }

    Stmt build_realize_function_from_group(Stmt s, int func_index) {
        if (function_is_already_realized_in_stmt(funcs[func_index], s)) {
            return s;
        }
        if (function_is_used_in_stmt(funcs[func_index], s) || is_output_list[func_index]) {
            s = build_realize(s, funcs[func_index], is_output_list[func_index]);
        }

        return s;
    }

    // Compute the shift factor required to align iteration of
    // a function stage with its fused parent loop nest.
    void compute_shift_factor(const Function &f, const string &prefix, const Definition &def,
                              map<string, Expr> &bounds, map<string, Expr> &shifts) {
        if (!def.defined()) {
            return;
        }

        const vector<Dim> &dims = def.schedule().dims();  // From inner to outer
        const LoopLevel &fuse_level = def.schedule().fuse_level().level;
        const map<string, LoopAlignStrategy> &align_strategy = def.schedule().fuse_level().align;

        if (fuse_level.is_inlined() || fuse_level.is_root()) {
            return;
        }

        int start_fuse;
        {
            const auto &iter = std::find_if(dims.begin(), dims.end(),
                                            [&fuse_level](const Dim &d) {
                                                return var_name_match(d.var, fuse_level.var().name());
                                            });
            internal_assert(iter != dims.end());
            start_fuse = (int)(iter - dims.begin());
        }

        int fused_vars_num = dims.size() - start_fuse - 1;

        const auto &env_iter = env.find(fuse_level.func());
        internal_assert(env_iter != env.end());
        const auto &parent_func = env_iter->second;

        const auto &parent_def = (fuse_level.stage_index() == 0) ? parent_func.definition() : parent_func.update(fuse_level.stage_index() - 1);
        const vector<Dim> &parent_dims = parent_def.schedule().dims();

        for (int i = start_fuse; i < (int)dims.size() - 1; ++i) {
            const string &var = dims[i].var;
            Expr shift_val;
            auto iter = align_strategy.begin();
            for (; iter != align_strategy.end(); ++iter) {
                if (var_name_match(var, iter->first)) {
                    break;
                }
            }

            if ((iter == align_strategy.end()) ||
                (iter->second == LoopAlignStrategy::NoAlign) ||
                (iter->second == LoopAlignStrategy::Auto)) {
                continue;
            }

            string parent_prefix = fuse_level.func() + ".s" + std::to_string(fuse_level.stage_index()) + ".";
            int parent_var_index = (i - start_fuse) + (int)parent_dims.size() - 1 - fused_vars_num;
            internal_assert(parent_var_index >= 0);
            string parent_var = parent_dims[parent_var_index].var;

            auto it_min = bounds.find(prefix + var + ".loop_min");
            auto it_max = bounds.find(prefix + var + ".loop_max");
            internal_assert((it_min != bounds.end()) && (it_max != bounds.end()));

            if (iter->second == LoopAlignStrategy::AlignStart) {
                auto parent_min = bounds.find(parent_prefix + parent_var + ".loop_min");
                internal_assert(parent_min != bounds.end());
                shift_val = parent_min->second - it_min->second;
            } else {
                auto parent_max = bounds.find(parent_prefix + parent_var + ".loop_max");
                internal_assert(parent_max != bounds.end());
                shift_val = parent_max->second - it_max->second;
            }

            internal_assert(shift_val.defined());
            shifts.emplace(prefix + var, simplify(-shift_val));
            it_min->second = simplify(shift_val + it_min->second);
            it_max->second = simplify(shift_val + it_max->second);
        }
    }

    Stmt build_produce_definition(const Function &f, const string &prefix, const Definition &def, bool is_update,
                                  map<string, Expr> &replacements, vector<pair<string, Expr>> &add_lets) {
        const vector<Dim> &dims = def.schedule().dims();  // From inner to outer
        const LoopLevel &fuse_level = def.schedule().fuse_level().level;

        size_t start_fuse = dims.size();
        if (!fuse_level.is_inlined() && !fuse_level.is_root()) {
            const auto &iter = std::find_if(dims.begin(), dims.end(),
                                            [&fuse_level](const Dim &d) {
                                                return var_name_match(d.var, fuse_level.var().name());
                                            });
            internal_assert(iter != dims.end());
            start_fuse = (size_t)(iter - dims.begin());
        }

        // The bounds of the child fused loops should be replaced to refer to the
        // parent fused loop. Here, we are only collecting the ones we should
        // replace. The actual replacement is done later.
        for (const FusedPair &pair : def.schedule().fused_pairs()) {
            const auto &f2_it = env.find(pair.func_2);
            internal_assert(f2_it != env.end());
            const vector<Dim> &dims_2 =
                (pair.stage_2 == 0) ? f2_it->second.definition().schedule().dims() : f2_it->second.update((int)(pair.stage_2 - 1)).schedule().dims();

            const auto &iter = std::find_if(dims.begin(), dims.end(),
                                            [&pair](const Dim &d) { return var_name_match(d.var, pair.var_name); });
            internal_assert(iter != dims.end());
            start_fuse = std::min(start_fuse, (size_t)(iter - dims.begin()));
            // Should ignore the __outermost dummy dimension.
            for (size_t i = (size_t)(iter - dims.begin()); i < dims.size() - 1; ++i) {
                string var_orig = pair.func_1 + ".s" + std::to_string(pair.stage_1) + "." + dims[i].var;
                Expr val = Variable::make(Int(32), var_orig);

                int dim2_idx = (int)(dims_2.size() - (dims.size() - i));
                internal_assert(dim2_idx < (int)dims_2.size());
                string var = pair.func_2 + ".s" + std::to_string(pair.stage_2) + "." + dims_2[dim2_idx].var;

                replacements.emplace(var + ".loop_extent", make_const(Int(32), 1));
                replacements.emplace(var + ".loop_min", val);
                replacements.emplace(var + ".loop_max", val);
            }
        }

        Stmt produce = build_provide_loop_nest(env, prefix, f, def, (int)(start_fuse), is_update);

        // Strip off the containing lets. The bounds of the parent fused loop
        // (i.e. the union bounds) might refer to them, so we need to move them
        // to the topmost position.
        while (const auto *let = produce.as<LetStmt>()) {
            add_lets.emplace_back(let->name, let->value);
            produce = let->body;
        }
        return produce;
    }

    void collect_all_dependence_helper(const string &prefix, const Definition &def, const FusedPair &p,
                                       vector<FusedPair> &dependence, set<string> &visited) {
        visited.insert(prefix);
        dependence.push_back(p);
        for (const FusedPair &pair : def.schedule().fused_pairs()) {
            const auto &iter = env.find(pair.func_2);
            internal_assert(iter != env.end());
            const Function &f = iter->second;
            string prefix_2 = pair.func_2 + ".s" + std::to_string(pair.stage_2) + "." + pair.var_name;
            if (visited.find(prefix_2) == visited.end()) {
                const Definition &def_2 = (pair.stage_2 == 0) ? f.definition() : f.update((int)(pair.stage_2 - 1));
                collect_all_dependence_helper(prefix_2, def_2, pair, dependence, visited);
            }
        }
    }

    // Collect all fused pairs that directly/indirectly related to 'def'
    vector<FusedPair> collect_all_dependence(const Definition &def) {
        set<string> visited;
        vector<FusedPair> dependence;

        for (const FusedPair &pair : def.schedule().fused_pairs()) {
            const auto &iter = env.find(pair.func_2);
            internal_assert(iter != env.end());
            const Function &f = iter->second;
            string prefix = pair.func_2 + ".s" + std::to_string(pair.stage_2) + "." + pair.var_name;
            if (visited.find(prefix) == visited.end()) {
                const Definition &def_2 = (pair.stage_2 == 0) ? f.definition() : f.update((int)(pair.stage_2 - 1));
                collect_all_dependence_helper(prefix, def_2, pair, dependence, visited);
            }
        }
        return dependence;
    }

    // Replace the bounds of the parent fused loop (i.e. the first one to be
    // realized in the group) with union of the bounds of the fused group.
    Stmt replace_parent_bound_with_union_bound(const Function &f, Stmt produce, const map<string, Expr> &bounds) {
        string prefix = f.name() + ".s0";
        const Definition &def = f.definition();

        if (!def.defined()) {
            return produce;
        }

        const vector<Dim> &dims = def.schedule().dims();  // From inner to outer

        map<string, Expr> replacements;

        vector<FusedPair> dependence = collect_all_dependence(def);

        // Compute the union of the bounds of the fused loops.
        for (const FusedPair &pair : dependence) {
            const auto &f2_it = env.find(pair.func_2);
            internal_assert(f2_it != env.end());
            const vector<Dim> &dims_2 =
                (pair.stage_2 == 0) ? f2_it->second.definition().schedule().dims() :
                                      f2_it->second.update((int)(pair.stage_2 - 1)).schedule().dims();

            const auto &iter = std::find_if(dims.begin(), dims.end(),
                                            [&pair](const Dim &d) { return var_name_match(d.var, pair.var_name); });
            internal_assert(iter != dims.end());

            // Should ignore the __outermost dummy dimension.
            for (size_t i = (size_t)(iter - dims.begin()); i < dims.size() - 1; ++i) {
                // The child's dim might have slightly different name from
                // the parent, e.g. y.yi and yi.
                int dim2_idx = (int)(dims_2.size() - (dims.size() - i));
                internal_assert(dim2_idx < (int)dims_2.size());

                string var_2 = pair.func_2 + ".s" + std::to_string(pair.stage_2) +
                               "." + dims_2[dim2_idx].var;
                internal_assert(bounds.count(var_2 + ".loop_min"));
                internal_assert(bounds.count(var_2 + ".loop_max"));
                internal_assert(bounds.count(var_2 + ".loop_extent"));
                Expr min_2 = bounds.find(var_2 + ".loop_min")->second;
                Expr max_2 = bounds.find(var_2 + ".loop_max")->second;
                Expr extent_2 = bounds.find(var_2 + ".loop_extent")->second;

                string var_1 = prefix + "." + dims[i].var;
                internal_assert(bounds.count(var_1 + ".loop_min"));
                internal_assert(bounds.count(var_1 + ".loop_max"));
                internal_assert(bounds.count(var_1 + ".loop_extent"));

                Expr min_1, max_1;
                const auto &it = replacements.find(var_1 + ".loop_min");
                if (it == replacements.end()) {
                    min_1 = bounds.find(var_1 + ".loop_min")->second;
                    max_1 = bounds.find(var_1 + ".loop_max")->second;
                } else {
                    min_1 = replacements.find(var_1 + ".loop_min")->second;
                    max_1 = replacements.find(var_1 + ".loop_max")->second;
                }

                // Extent is computed from min/max, so we don't find() it earlier.
                replacements[var_1 + ".loop_min"] = simplify(min(min_1, min_2));
                replacements[var_1 + ".loop_max"] = simplify(max(max_1, max_2));
                replacements[var_1 + ".loop_extent"] =
                    simplify((replacements[var_1 + ".loop_max"] + 1) -
                             replacements[var_1 + ".loop_min"]);
            }
        }

        // Now, replace the bounds of the parent fused loops with the union bounds.
        produce = substitute_fused_bounds(produce, replacements);
        return produce;
    }

    Stmt build_pipeline_group(Stmt consumer) {
        size_t num_skipped = 0;
        for (size_t i = 0; i < funcs.size(); ++i) {
            bool should_skip = function_is_already_realized_in_stmt(funcs[i], consumer) ||
                               !(function_is_used_in_stmt(funcs[i], consumer) || is_output_list[i]);
            if (should_skip) {
                num_skipped += 1;
            }
        }

        if (num_skipped == funcs.size()) {
            // All producers are skipped.
            return consumer;
        }

        user_assert(num_skipped == 0) << "Fused groups must either be entirely used or unused\n";

        // Order of the stages for building produce definitions.
        vector<pair<Function, int>> stage_order;
        // Inverse map from function name to the index.
        map<string, int> func_name_to_index;
        // Contains a number of dependencies which need to go first for a given stage of the function.
        vector<vector<int>> stage_dependencies(funcs.size());

        // Holds the index of the function stage.
        struct FuncStageIndex {
            int func_index;
            int stage_index;
        };

        // Adjacency list of dependencies. The structure is [func_index, stage_index, vector of the edges],
        // where edge is the index of the other function stage.
        vector<vector<vector<FuncStageIndex>>> adj_list(funcs.size());

        // Initialize data structures.
        for (size_t i = 0; i < funcs.size(); i++) {
            stage_dependencies[i].resize(1 + funcs[i].updates().size(), 0);
            adj_list[i].resize(1 + funcs[i].updates().size());
            func_name_to_index[funcs[i].name()] = i;
        }

        // Figure out dependencies between the stages.
        for (size_t i = 0; i < funcs.size(); i++) {
            auto prev_level = funcs[i].definition().schedule().fuse_level().level;
            {
                const auto &level = funcs[i].definition().schedule().fuse_level().level;
                if (!level.is_root() && !level.is_inlined()) {
                    stage_dependencies[i][0]++;
                    adj_list[func_name_to_index[level.func()]][level.stage_index()].push_back({(int)i, 0});
                }
            }
            for (size_t j = 0; j < funcs[i].updates().size(); ++j) {
                const auto &level = funcs[i].updates()[j].schedule().fuse_level().level;
                if (!level.is_root() && !level.is_inlined()) {
                    stage_dependencies[i][j + 1]++;
                    adj_list[func_name_to_index[level.func()]][level.stage_index()].push_back({(int)i, (int)j + 1});

                    // Let say that we have a stage f.update(p), which is scheduled to be computed_with
                    // another stage g.update(q) (like f.update(p).compute_with(g.update(q), var)).
                    // This means that the loop for f.update(p) will be injected into the loop for g.update(q).
                    // Given that, for this to be correct, all stages of f up until (p - 1) must come
                    // before g.update(q).
                    // However, there is a special case here when two or more consecutive stages are computed
                    // with the same stage. In this case, we won't be adding back edge to avoid creating cyclic
                    // dependency.
                    if (!(prev_level.func() == level.func() && prev_level.stage_index() == level.stage_index())) {
                        for (size_t k = 0; k < j + 1; k++) {
                            stage_dependencies[func_name_to_index[level.func()]][level.stage_index()]++;
                            adj_list[i][k].push_back({func_name_to_index[level.func()], level.stage_index()});
                        }
                    }
                    prev_level = level;
                }
            }
        }

        size_t complete_count = 0;
        vector<size_t> stage_index(funcs.size());
        // This basically computes topological order, but exploits the fact that stages of the function
        // form linear order. Basically, we have a set of indices that point to the current stages
        // for each of the functions and should be considered as a next stage in the general order. They
        // are added to the order, only if all of their dependencies have been added already.
        while (complete_count < funcs.size()) {
            bool progress_made = false;
            for (size_t i = 0; i < funcs.size(); i++) {
                // We already added all stages of this function, so proceed to the next function.
                if (stage_index[i] == stage_dependencies[i].size()) {
                    continue;
                }
                // Proceed as far as we can, so stages of the same function are bundled together.
                while (stage_index[i] < stage_dependencies[i].size()) {
                    if (stage_dependencies[i][stage_index[i]] > 0) {
                        break;
                    }
                    // Now that we are going to add a stage to the order, go over dependent nodes
                    // and decrease their dependency count.
                    for (auto &edge : adj_list[i][stage_index[i]]) {
                        internal_assert(stage_dependencies[edge.func_index][edge.stage_index] > 0);
                        stage_dependencies[edge.func_index][edge.stage_index]--;
                    }
                    stage_order.emplace_back(funcs[i], stage_index[i]);
                    stage_index[i]++;
                    progress_made = true;
                }
                if (stage_index[i] == stage_dependencies[i].size()) {
                    complete_count++;
                }
            }
            // Make sure that we made some progress, otherwise there is a cyclic dependency.
            if (!progress_made) {
                std::stringstream ss;
                ss << "There is a cycle inside of the fused group: \n";
                for (size_t i = 0; i < funcs.size(); i++) {
                    if (stage_index[i] == stage_dependencies[i].size()) {
                        continue;
                    }
                    ss << funcs[i].name() << ".s" << stage_index[i] << "has " << stage_dependencies[i][stage_index[i]]
                       << "unsatisfied dependencies; \n";
                }
                user_assert(progress_made) << ss.str();
            }
        }

        // Build the loops.
        Stmt producer;
        map<string, Expr> replacements;
        vector<pair<string, Expr>> add_lets;

        for (const auto &func_stage : stage_order) {
            const auto &f = func_stage.first;

            if (f.has_extern_definition() && (func_stage.second == 0)) {
                const Stmt &produceDef = Internal::build_extern_produce(env, f, target);
                producer = inject_stmt(producer, produceDef, LoopLevel::inlined().lock());
                continue;
            }

            string def_prefix = f.name() + ".s" + std::to_string(func_stage.second) + ".";
            const auto &def = (func_stage.second == 0) ? f.definition() : f.updates()[func_stage.second - 1];

            const Stmt &produceDef = build_produce_definition(f, def_prefix, def, func_stage.second > 0,
                                                              replacements, add_lets);
            producer = inject_stmt(producer, produceDef, def.schedule().fuse_level().level);
        }

        internal_assert(producer.defined());

        // Rewrap the loop in the containing lets.
        for (size_t i = add_lets.size(); i > 0; --i) {
            const auto &b = add_lets[i - 1];
            producer = LetStmt::make(b.first, b.second, producer);
        }

        // The original bounds of the loop nests (without any loop-fusion)
        auto bounds = CollectBounds::collect_bounds(producer);

        // Compute the shift factors based on the alignment strategies
        // starting from the the parent (root loop) to the children. The root
        // loop bounds should remain unchanged.
        map<string, Expr> shifts;
        for (auto i = funcs.size(); i-- > 0;) {
            const auto &func = funcs[i];
            compute_shift_factor(func, func.name() + ".s0.", func.definition(), bounds, shifts);
            for (size_t j = 0; j < func.updates().size(); ++j) {
                string prefix = func.name() + ".s" + std::to_string(j + 1) + ".";
                compute_shift_factor(func, prefix, func.updates()[j], bounds, shifts);
            }
        }
        // Shift the loops.
        producer = ShiftLoopNest::apply_shift(shifts, producer);

        // Replace all the child fused loops with the appropriate bounds
        // (i.e. the min/max should refer to the parent loop vars and the
        // extent should be one).
        producer = substitute_fused_bounds(producer, replacements);

        // Replace the bounds of parent fused loop with union of bounds of
        // the fused loops.
        producer = replace_parent_bound_with_union_bound(funcs.back(), producer, bounds);

        // Add the producer nodes.
        for (const auto &i : funcs) {
            producer = ProducerConsumer::make_produce(i.name(), producer);
        }

        // Add the consumer nodes.
        for (size_t i = 0; i < funcs.size(); i++) {
            if (!is_output_list[i]) {
                consumer = ProducerConsumer::make_consume(funcs[i].name(), consumer);
            }
        }

        if (is_no_op(consumer)) {
            // For the very first output to be scheduled, the consumer
            // Stmt can be a no-op. No point in preserving it.
            return producer;
        } else {
            return Block::make(producer, consumer);
        }
    }
};

class ComputeLegalSchedules : public IRVisitor {
public:
    struct Site {
        bool is_parallel, is_gpu_block;
        LoopLevel loop_level;
    };
    vector<Site> sites_allowed;
    bool found = false;

    ComputeLegalSchedules(Function f, const map<string, Function> &env)
        : func(std::move(f)), env(env) {
    }

private:
    using IRVisitor::visit;

    vector<Site> sites;
    Function func;

    const map<string, Function> &env;

    void visit(const For *f) override {
        size_t first_dot = f->name.find('.');
        size_t last_dot = f->name.rfind('.');
        internal_assert(first_dot != string::npos && last_dot != string::npos);
        string func = f->name.substr(0, first_dot);
        string var = f->name.substr(last_dot + 1);
        LoopLevel loop_level;
        if (func.empty()) {
            internal_assert(!var.empty());
            loop_level = LoopLevel::root();
        } else {
            auto it = env.find(func);
            internal_assert(it != env.end()) << "Unable to find Function " << func << " in env (Var = " << var << ")\n";
            loop_level = LoopLevel(it->second, Var(var));
        }
        // Since we are now in the lowering phase, we expect all LoopLevels to be locked;
        // thus any new ones we synthesize we must explicitly lock.
        loop_level.lock();
        const bool is_gpu_block = (f->for_type == ForType::GPUBlock);
        sites.push_back({f->is_parallel(), is_gpu_block, loop_level});

        f->min.accept(this);
        f->extent.accept(this);
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

    void visit(const Call *c) override {
        IRVisitor::visit(c);

        if (c->name == func.name()) {
            register_use();
        }
    }

    void visit(const Variable *v) override {
        if (v->type.is_handle() &&
            starts_with(v->name, func.name() + ".") &&
            ends_with(v->name, ".buffer")) {
            register_use();
        }
    }
};

string schedule_to_source(const Function &f, const LoopLevel &store_at, const LoopLevel &compute_at) {
    std::ostringstream ss;
    ss << f.name();
    if (compute_at.is_inlined()) {
        ss << ".compute_inline()";
    } else {
        if (!store_at.match(compute_at)) {
            if (store_at.is_root()) {
                ss << ".store_root()";
            } else {
                string store_var_name = store_at.var().name();
                if (store_var_name == Var::outermost().name()) {
                    store_var_name = "Var::outermost()";
                }
                ss << ".store_at(" << store_at.func() << ", " << store_var_name << ")";
            }
        }
        if (compute_at.is_root()) {
            ss << ".compute_root()";
        } else {
            string compute_var_name = compute_at.var().name();
            if (compute_var_name == Var::outermost().name()) {
                compute_var_name = "Var::outermost()";
            }
            ss << ".compute_at(" << compute_at.func() << ", " << compute_var_name << ")";
        }
    }
    ss << ";";
    return ss.str();
}

class StmtUsesFunc : public IRVisitor {
    using IRVisitor::visit;
    const string &func;
    void visit(const Call *op) override {
        if (op->name == func) {
            result = true;
        }
        IRVisitor::visit(op);
    }
    void visit(const Variable *op) override {
        if (op->type.is_handle() &&
            starts_with(op->name, func + ".") &&
            ends_with(op->name, ".buffer")) {
            result = true;
        }
        IRVisitor::visit(op);
    }

public:
    bool result = false;
    explicit StmtUsesFunc(const string &f)
        : func(f) {
    }
};

class PrintUsesOfFunc : public IRVisitor {
    using IRVisitor::visit;

    int indent = 1;
    string func, caller;
    bool last_print_was_ellipsis = false;
    std::ostream &stream;

    Indentation get_indent() const {
        return Indentation{indent};
    }

    void visit(const For *op) override {
        if (ends_with(op->name, Var::outermost().name()) ||
            ends_with(op->name, LoopLevel::root().lock().to_string())) {
            IRVisitor::visit(op);
        } else {

            int old_indent = indent;

            StmtUsesFunc uses(func);
            op->body.accept(&uses);
            if (!uses.result) {
                if (!last_print_was_ellipsis) {
                    stream << get_indent() << "...\n";
                    last_print_was_ellipsis = true;
                }
            } else {
                stream << get_indent() << "for " << op->name << ":\n";
                last_print_was_ellipsis = false;
                indent++;
            }

            IRVisitor::visit(op);
            indent = old_indent;
        }
    }

    void visit(const ProducerConsumer *op) override {
        if (op->is_producer) {
            string old_caller = caller;
            caller = op->name;
            op->body.accept(this);
            caller = old_caller;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Call *op) override {
        if (op->name == func) {
            stream << get_indent() << caller << " uses " << func << "\n";
            last_print_was_ellipsis = false;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Variable *op) override {
        if (op->type.is_handle() &&
            starts_with(op->name, func + ".") &&
            ends_with(op->name, ".buffer")) {
            stream << get_indent() << caller << " uses " << func << "\n";
            last_print_was_ellipsis = false;
        } else {
            IRVisitor::visit(op);
        }
    }

public:
    PrintUsesOfFunc(string f, std::ostream &s)
        : func(std::move(f)), stream(s) {
    }
};

// Check a schedule is legal, throwing an error if it is not. Returns
// whether or not a realization of the Func should be injected. Unused
// intermediate Funcs that somehow made it into the Func DAG can be
// discarded.
bool validate_schedule(Function f, const Stmt &s, const Target &target, bool is_output, const map<string, Function> &env) {

    // If f is extern, check that none of its inputs are scheduled inline.
    if (f.has_extern_definition()) {
        for (const ExternFuncArgument &arg : f.extern_arguments()) {
            if (arg.is_func()) {
                Function g(arg.func);
                if (!g.is_wrapper() &&
                    g.schedule().compute_level().is_inlined()) {
                    user_error
                        << "Func " << g.name() << " cannot be scheduled to be computed inline, "
                        << "because it is used in the externally-computed function " << f.name() << "\n";
                }
            }
        }

        // Check that extern stages do not have any non-extern loops
        // inside any extern loops, and all loop types are supported
        // for extern stages.
        const vector<Dim> &dims = f.definition().schedule().dims();
        bool is_extern = !dims.empty() ? dims.front().for_type == ForType::Extern : false;
        for (const Dim &i : dims) {
            switch (i.for_type) {
            case ForType::Extern:
                if (!is_extern) {
                    user_error
                        << "Externally defined Func " << f.name()
                        << " cannot have extern loop " << i.var
                        << " outside a non-extern loop.\n";
                }
                break;
            case ForType::Serial:
            case ForType::Parallel:
            case ForType::Unrolled:
                is_extern = false;
                break;
            default:
                user_error
                    << "Externally defined Func " << f.name()
                    << " cannot have loop type " << i.for_type << " (" << i.var << ")\n";
            }
        }
    }

    // Emit a warning if only some of the steps have been scheduled.
    bool any_scheduled = f.has_pure_definition() && f.definition().schedule().touched();
    for (const Definition &r : f.updates()) {
        any_scheduled = any_scheduled || r.schedule().touched();
    }
    if (any_scheduled) {
        for (size_t i = 0; i < f.updates().size(); i++) {
            const Definition &r = f.update((int)i);
            if (!r.schedule().touched()) {
                user_warning
                    << "Update definition " << i
                    << " of function " << f.name()
                    << " has not been scheduled, even though some other"
                    << " definitions have been. You may have forgotten to"
                    << " schedule it. If this was intentional, call "
                    << f.name() << ".update(" << i << ").unscheduled() to suppress"
                    << " this warning.\n";
            }
        }
    }

    // If the func is scheduled on the gpu, check that the relevant
    // api is enabled in the target.
    vector<Definition> definitions;
    if (f.has_pure_definition()) {
        definitions.push_back(f.definition());
    }
    for (const Definition &def : f.updates()) {
        definitions.push_back(def);
    }

    for (size_t i = 0; i < definitions.size(); i++) {
        for (const Specialization &s : definitions[i].specializations()) {
            definitions.push_back(s.definition);
        }
    }

    int racy_shift_inwards_count = 0;
    int allow_race_conditions_count = 0;
    for (const Definition &def : definitions) {
        const StageSchedule &s = def.schedule();
        set<string> parallel_vars;
        for (const Dim &d : s.dims()) {
            // We don't care about GPU parallelism here
            if (d.for_type == ForType::Parallel) {
                parallel_vars.insert(d.var);
            }
            if (!target.supports_device_api(d.device_api)) {
                user_error << "Schedule for Func " << f.name()
                           << " requires " << d.device_api
                           << " but no compatible target feature is enabled in target "
                           << target.to_string() << "\n";
            }
        }
        if (s.allow_race_conditions()) {
            allow_race_conditions_count++;
        }

        // For purposes of race-detection-warning, any split that
        // is the child of a parallel var is also 'parallel'.
        //
        // However, there are four types of Split, and the concept of a child var varies across them:
        // - For a vanilla split, inner and outer are the children and old_var is the parent.
        // - For rename and purify, the outer is the child and the inner is meaningless.
        // - For fuse, old_var is the child and inner/outer are the parents.
        //
        // (@abadams comments: "I acknowledge that this is gross and should be refactored.")

        // (Note that the splits are ordered, so a single reverse-pass catches all these cases.)
        for (auto split = s.splits().rbegin(); split != s.splits().rend(); split++) {
            if (split->is_split() && (parallel_vars.count(split->outer) || parallel_vars.count(split->inner))) {
                parallel_vars.insert(split->old_var);
            } else if (split->is_fuse() && parallel_vars.count(split->old_var)) {
                parallel_vars.insert(split->inner);
                parallel_vars.insert(split->outer);
            } else if ((split->is_rename() || split->is_purify()) && parallel_vars.count(split->outer)) {
                parallel_vars.insert(split->old_var);
            }
        }

        for (const auto &split : s.splits()) {
            // ShiftInwards used inside a parallel split can produce racy (though benignly so) code
            // that TSAN will complain about; issue a warning so that the user doesn't assume
            // the warning is legitimate.
            if (split.tail == TailStrategy::ShiftInwards && parallel_vars.count(split.outer)) {
                racy_shift_inwards_count++;
            }
        }
    }

    if (target.has_feature(Target::TSAN)) {
        if (allow_race_conditions_count > 0) {
            user_warning
                << "Schedule for Func '" << f.name()
                << "'' has one or more uses of allow_race_conditions() in its schedule;\n"
                << "this may report benign data races when run with ThreadSanitizer.\n\n";
        }
        if (racy_shift_inwards_count > 0) {
            user_warning
                << "Schedule for Func '" << f.name()
                << "'' has " << racy_shift_inwards_count << " split(s) using TailStrategy::ShiftInwards inside a parallel loop;\n"
                << "this may report benign data races when run with ThreadSanitizer.\n"
                << "(Note that ShiftInwards splits may be implicitly created by\n"
                << "other scheduling operations, e.g. parallel() and vectorize()).\n\n";
        }
    }

    LoopLevel store_at = f.schedule().store_level();
    LoopLevel compute_at = f.schedule().compute_level();
    LoopLevel hoist_storage_at = f.schedule().hoist_storage_level();

    // Outputs must be compute_root and store_root. They're really
    // store_in_user_code, but store_root is close enough.
    if (is_output) {
        if (store_at.is_root() && compute_at.is_root()) {
            return true;
        } else {
            user_error << "Func " << f.name() << " is an output, so must"
                       << " be scheduled compute_root (which is the default).\n";
        }
    }

    // Otherwise inspect the uses to see what's ok.
    ComputeLegalSchedules legal(f, env);
    s.accept(&legal);

    if (!is_output && !legal.found) {
        // It's not an output, and it's not called anywhere. Skip it.
        return false;
    }

    if (compute_at.is_inlined()) {
        if (store_at.is_root()) {
            user_error << "Func \"" << f.name() << "\" is scheduled store_root(), but is inlined. Funcs that use store_root must also call compute_root or compute_at.\n";
        } else if (!store_at.is_inlined()) {
            user_error << "Func \"" << f.name() << "\" is scheduled store_at(), but is inlined. Funcs that use store_at must also call compute_at.\n";
        }

        if (hoist_storage_at.is_root()) {
            user_error << "Func \"" << f.name() << "\" is scheduled hoist_storage_root(), but is inlined. Funcs that use hoist_storage_root must also call compute_root or compute_at.\n";
        } else if (!hoist_storage_at.is_inlined()) {
            user_error << "Func \"" << f.name() << "\" is scheduled hoist_storage(), but is inlined. Funcs that use hoist_storage_root must also call compute_at.\n";
        }
    }
    // Check if the schedule of the inlined function is legal. Since only
    // pure function can be inlined, we only need to call the validator on
    // pure function. An inlined Halide Func with multiple stages technically
    // will get lowered into compute_at innermost and thus can be treated
    // similarly as a non-inlined Func.
    if (store_at.is_inlined() && compute_at.is_inlined() && hoist_storage_at.is_inlined()) {
        if (f.is_pure()) {
            validate_schedule_inlined_function(f);
        }
        return true;
    }

    if (f.schedule().ring_buffer().defined() && store_at == hoist_storage_at) {
        user_error << "Func \"" << f.name() << "\" is scheduled with ring_buffer(), but has matching store_at and hoist_storage levels. Add an explicit hoist_storage directive to the schedule to fix the issue.\n";
    }

    vector<ComputeLegalSchedules::Site> &sites = legal.sites_allowed;
    int store_idx = -1, compute_idx = -1, hoist_storage_idx = -1;
    for (size_t i = 0; i < sites.size(); i++) {
        if (sites[i].loop_level.match(hoist_storage_at)) {
            hoist_storage_idx = i;
        }
        if (sites[i].loop_level.match(store_at) && hoist_storage_idx >= 0) {
            store_idx = i;
        }
        if (sites[i].loop_level.match(compute_at) && store_idx >= 0 && hoist_storage_idx >= 0) {
            compute_idx = i;
        }
    }

    std::ostringstream err;

    const auto all_ok = [&]() {
        return store_idx >= 0 && compute_idx >= 0 && hoist_storage_idx >= 0;
    };

    // Check there isn't a parallel loop between the compute_at and the store_at
    if (all_ok()) {
        for (int i = store_idx + 1; i <= compute_idx; i++) {
            if (sites[i].is_parallel) {
                err << "Func \"" << f.name()
                    << "\" is stored outside the parallel/vectorized/gpu_block loop over "
                    << sites[i].loop_level.to_string()
                    << " but computed within it. This is a potential race condition.\n";
                store_idx = compute_idx = hoist_storage_idx = -1;
            }
        }
    }

    // Check there isn't a parallel loop between the compute_at and the hoist_storage_at
    if (all_ok()) {
        for (int i = hoist_storage_idx + 1; i <= compute_idx; i++) {
            if (sites[i].is_parallel) {
                err << "Func \"" << f.name()
                    << "\" storage is hoisted outside the parallel/vectorized/gpu_block loop over "
                    << sites[i].loop_level.to_string()
                    << " but computed within it. This is a potential race condition.\n";
                store_idx = compute_idx = hoist_storage_idx = -1;
            }
        }
    }

    if (!all_ok()) {
        err << "Func \"" << f.name() << "\" is computed at the following invalid location:\n"
            << "  " << schedule_to_source(f, store_at, compute_at) << "\n"
            << "Legal locations for this function are:\n";
        for (const auto &site : sites) {
            err << "  " << schedule_to_source(f, site.loop_level, site.loop_level) << "\n";
        }
        err << "\"" << f.name() << "\" is used in the following places:\n";
        PrintUsesOfFunc printer(f.name(), err);
        s.accept(&printer);

        user_error << err.str();
    }

    return true;
}

void validate_fused_group_schedule_helper(const string &fn,
                                          size_t stage_index,
                                          const Definition &def_1,
                                          const map<string, Function> &env) {
    internal_assert(def_1.defined());
    for (const auto &p : def_1.schedule().fused_pairs()) {
        internal_assert((fn == p.func_1) && (stage_index == p.stage_1));

        const auto &iter1 = env.find(p.func_1);
        const auto &iter2 = env.find(p.func_2);
        internal_assert((iter1 != env.end()) && (iter2 != env.end()));

        const Function &func_1 = iter1->second;
        const Function &func_2 = iter2->second;
        const Definition &def_2 = (p.stage_2 == 0) ? func_2.definition() : func_2.update((int)(p.stage_2 - 1));
        internal_assert(def_2.defined());

        // f2.compute_with(f1, var) is allowed only if f2 has no specializations.
        user_assert(func_2.definition().specializations().empty())
            << "Func " << func_2.name() << " is scheduled to be computed with "
            << func_1.name() << ", so it must not have any specializations.\n";

        // Verify that the functions being computed with are not scheduled inline.
        user_assert(!func_1.schedule().compute_level().is_inlined())
            << "Invalid compute_with: " << p.func_1 << ".s" << p.stage_1
            << " is scheduled inline.\n";
        user_assert(!func_2.schedule().compute_level().is_inlined())
            << "Invalid compute_with: " << p.func_2 << ".s" << p.stage_2
            << " is scheduled inline.\n";

        // Verify that the functions being computed with does not have extern definitions.
        user_assert(!func_1.has_extern_definition())
            << "Invalid compute_with: " << p.func_1 << ".s" << p.stage_1
            << " has extern definition.\n";
        user_assert(!func_2.has_extern_definition())
            << "Invalid compute_with: " << p.func_2 << ".s" << p.stage_2
            << " has extern definition.\n";

        // Verify that they are computed at the same loop level.
        user_assert(func_1.schedule().compute_level() == func_2.schedule().compute_level())
            << "Invalid compute_with: the compute levels of " << p.func_1 << ".s" << p.stage_1
            << " (computed at " << func_1.schedule().compute_level().to_string()
            << ") and " << p.func_2 << ".s" << p.stage_2 << " ("
            << func_2.schedule().compute_level().to_string() << ") do not match.\n";

        const vector<Dim> &dims_1 = def_1.schedule().dims();
        const vector<Dim> &dims_2 = def_2.schedule().dims();

        // Assert that the variable specified in compute_with is in the dim list.
        const auto &iter_1 =
            std::find_if(dims_1.begin(), dims_1.end(),
                         [&p](const Dim &d) {
                             return var_name_match(d.var, p.var_name);
                         });
        user_assert(iter_1 != dims_1.end())
            << "Invalid compute_with: cannot find " << p.var_name << " in "
            << p.func_1 << ".s" << p.stage_1 << "\n";

        const auto &iter_2 =
            std::find_if(dims_2.begin(), dims_2.end(),
                         [&p](const Dim &d) {
                             return var_name_match(d.var, p.var_name);
                         });
        user_assert(iter_2 != dims_2.end())
            << "Invalid compute_with: cannot find " << p.var_name << " in "
            << p.func_2 << ".s" << p.stage_2 << "\n";

        // Verify that their dimensions up to "var_name" are the same.
        size_t start_fuse_1 = (size_t)(iter_1 - dims_1.begin());
        size_t start_fuse_2 = (size_t)(iter_2 - dims_2.begin());

        int n_fused = (int)(dims_1.size() - start_fuse_1 - 1);  // Ignore __outermost
        user_assert(n_fused == (int)(dims_2.size() - start_fuse_2 - 1))
            << "Invalid compute_with: # of fused dims of " << p.func_1 << ".s"
            << p.stage_1 << " and " << p.func_2 << ".s" << p.stage_2 << " do not match.\n";

        for (int i = 0; i < n_fused; ++i) {
            const Dim &d1 = dims_1[start_fuse_1 + i];
            const Dim &d2 = dims_2[start_fuse_2 + i];
            user_assert(var_name_match(d1.var, d2.var)) << "Invalid compute_with: names of dim "
                                                        << i << " of " << p.func_1 << ".s"
                                                        << p.stage_1 << "(" << d1.var << ") and " << p.func_2
                                                        << ".s" << p.stage_2 << "(" << d2.var << ") do not match.\n";
            user_assert(d1.for_type == d2.for_type) << "Invalid compute_with: for types of dim "
                                                    << i << " of " << p.func_1 << ".s" << p.stage_1 << "("
                                                    << d1.var << " is " << d1.for_type << ") and " << p.func_2
                                                    << ".s" << p.stage_2 << "(" << d2.var << " is " << d2.for_type
                                                    << ") do not match.\n";
            user_assert(d1.device_api == d2.device_api) << "Invalid compute_with: device APIs of dim "
                                                        << i << " of " << p.func_1 << ".s" << p.stage_1 << "("
                                                        << d1.var << " is " << d1.device_api << ") and " << p.func_2
                                                        << ".s" << p.stage_2 << "(" << d2.var << " is " << d2.device_api
                                                        << ") do not match.\n";
            user_assert(d1.dim_type == d2.dim_type) << "Invalid compute_with: types of dim "
                                                    << i << " of " << p.func_1 << ".s" << p.stage_1 << "("
                                                    << d1.var << " is " << d1.dim_type << ") and " << p.func_2
                                                    << ".s" << p.stage_2 << "(" << d2.var << " is " << d2.dim_type
                                                    << ") do not match.\n";
        }
    }
}

void validate_fused_groups_schedule(const vector<vector<string>> &fused_groups, const map<string, Function> &env) {
    for (const vector<string> &group : fused_groups) {
        for (const auto &fn : group) {
            const auto &iter = env.find(fn);
            internal_assert(iter != env.end());
            if (iter->second.has_extern_definition()) {
                continue;
            }

            validate_fused_group_schedule_helper(
                iter->first, 0, iter->second.definition(), env);
            for (const auto &s : iter->second.definition().specializations()) {
                validate_fused_group_schedule_helper(
                    iter->first, 0, s.definition, env);
            }
            for (size_t i = 0; i < iter->second.updates().size(); ++i) {
                const auto &update_stage = iter->second.updates()[i];
                validate_fused_group_schedule_helper(
                    iter->first, i + 1, update_stage, env);
                for (const auto &s : update_stage.specializations()) {
                    validate_fused_group_schedule_helper(
                        iter->first, i + 1, s.definition, env);
                }
            }
        }
    }
}

class RemoveLoopsOverOutermost : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        if (ends_with(op->name, ".__outermost") &&
            is_const_one(simplify(op->extent)) &&
            op->device_api == DeviceAPI::None) {
            return mutate(substitute(op->name, op->min, op->body));
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const LetStmt *op) override {
        if (ends_with(op->name, ".__outermost.loop_extent") ||
            ends_with(op->name, ".__outermost.loop_min") ||
            ends_with(op->name, ".__outermost.loop_max")) {
            return mutate(substitute(op->name, simplify(op->value), op->body));
        } else {
            return IRMutator::visit(op);
        }
    }
};

bool group_should_be_inlined(const vector<Function> &funcs) {
    return (funcs.size() == 1 &&
            (funcs[0].has_extern_definition() || funcs[0].definition().schedule().fused_pairs().empty()) &&
            funcs[0].can_be_inlined() &&
            funcs[0].schedule().compute_level().is_inlined());
}

}  // namespace

std::ostream &operator<<(std::ostream &out, const std::vector<Function> &v) {
    out << "{ ";
    for (size_t i = 0; i < v.size(); ++i) {
        out << v[i].name();
        if (i != v.size() - 1) {
            out << ", ";
        }
    }
    out << " }";
    return out;
}

Stmt schedule_functions(const vector<Function> &outputs,
                        const vector<vector<string>> &fused_groups,
                        const map<string, Function> &env,
                        const Target &target,
                        bool &any_memoized) {
    string root_var = LoopLevel::root().lock().to_string();
    Stmt s = For::make(root_var, 0, 1, ForType::Serial, Partition::Never, DeviceAPI::Host, Evaluate::make(0));

    any_memoized = false;

    validate_fused_groups_schedule(fused_groups, env);

    for (size_t i = fused_groups.size(); i > 0; --i) {
        const vector<string> &group = fused_groups[i - 1];
        vector<Function> funcs;
        vector<bool> is_output_list;

        for (const string &name : group) {
            Function f = env.find(name)->second;

            bool is_output = false;
            for (const Function &o : outputs) {
                is_output = is_output | o.same_as(f);
            }

            // The way in which the function was referred to in the
            // function DAG might not actually result in a use in the
            // code. This can happen if you inline a Tuple function,
            // ignoring one of the Tuple elements, and that Tuple
            // element is the sole call to a function with an update
            // definition.
            if (validate_schedule(f, s, target, is_output, env)) {
                any_memoized = any_memoized || f.schedule().memoized();
                funcs.push_back(f);
                is_output_list.push_back(is_output);
            }
        }

        if (funcs.empty()) {
            continue;
        }

        if (group_should_be_inlined(funcs)) {
            debug(1) << "Inlining " << funcs[0].name() << "\n";
            s = inline_function(s, funcs[0]);
        } else {
            debug(1) << "Injecting realization of " << funcs << "\n";
            InjectFunctionRealization injector(funcs, is_output_list, target, env);
            s = injector.mutate(s);
            internal_assert(injector.found_store_level() && injector.found_compute_level() && injector.found_hoist_storage_level());
        }

        debug(2) << s << "\n";
    }

    // We can remove the loop over root now
    const For *root_loop = s.as<For>();
    internal_assert(root_loop);
    s = root_loop->body;

    // We can also remove all the loops over __outermost now.
    s = RemoveLoopsOverOutermost().mutate(s);

    return s;
}

}  // namespace Internal
}  // namespace Halide
