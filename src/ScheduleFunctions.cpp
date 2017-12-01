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
#include "Func.h"
#include "ApplySplit.h"
#include "IREquality.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::vector;
using std::pair;
using std::set;

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
Stmt build_provide_loop_nest_helper(string func_name,
                                    string prefix,
                                    const vector<string> &dims,
                                    const vector<Expr> &site,
                                    const vector<Expr> &values,
                                    const vector<Expr> &predicates,
                                    const FuncSchedule &func_s,
                                    const StageSchedule &stage_s,
                                    bool is_update) {


    // We'll build it from inside out, starting from a store node,
    // then wrapping it in for loops.

    // Make the (multi-dimensional multi-valued) store node.
    Stmt stmt = Provide::make(func_name, values, site);

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

    // Define the function args in terms of the loop variables using the splits
    for (const Split &split : splits) {
        vector<ApplySplitResult> splits_result = apply_split(split, is_update, prefix, dim_extent_alignment);

        for (const auto &res : splits_result) {
            if (res.is_substitution()) {
                stmt = substitute(res.name, res.value, stmt);
            } else if (res.is_let()) {
                stmt = LetStmt::make(res.name, res.value, stmt);
            } else {
                internal_assert(res.is_predicate());
                stmt = IfThenElse::make(res.value, stmt, Stmt());
            }
        }
    }

    // All containing lets and fors. Outermost first.
    vector<Container> nest;

    // Put the desired loop nest into the containers vector.
    for (int i = (int)stage_s.dims().size() - 1; i >= 0; i--) {
        const Dim &dim = stage_s.dims()[i];
        Container c = {Container::For, i, prefix + dim.var, Expr()};
        nest.push_back(c);
    }

    vector<Container> pred_container;
    // Strip off the lets/ifs into the containers vector.
    while (true) {
        const LetStmt *let = stmt.as<LetStmt>();
        const IfThenElse *if_else = stmt.as<IfThenElse>();
        if (let) {
            Container c = {Container::Let, 0, let->name, let->value};
            nest.push_back(c);
            stmt = let->body;
        } else if (if_else && !if_else->else_case.defined()) {
            Container c = {Container::If, 0, "", if_else->condition};
            pred_container.push_back(c);
            stmt = if_else->then_case;
        } else {
            break;
        }
    }

    // Put all the reduction domain predicates into the containers vector.
    for (Expr pred : predicates) {
        pred = qualify(prefix, pred);
        Container c = {Container::If, 0, "", likely(pred)};
        pred_container.push_back(c);
    }
    int n_predicates = pred_container.size();

    nest.insert(nest.end(), pred_container.begin(), pred_container.end());

    // Resort the containers vector so that lets are as far outwards
    // as possible. Use reverse insertion sort. Start at the first letstmt.
    for (int i = (int)stage_s.dims().size(); i < (int)nest.size() - n_predicates; i++) {
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

    // Sort the ifs so they are as far outwards as possible.
    // BoxesTouched trims the domain of a variable within a scope of if-then-else
    // based on the likely condition. However, it doesn't do it transitively; it
    // doesn't trim the domains of other variables that directly/indirectly
    // depend on the original variable in the likely condition outside the scope
    // of the if-then-else. That's why it's necessary to move the ifs as far
    // outwards as possible, so that those variables can have tighter bounds.
    for (int i = (int)nest.size() - n_predicates; i < (int)nest.size(); i++) {
        // Only push up LetStmts.
        internal_assert(nest[i].value.defined());
        internal_assert(nest[i].type == Container::If);

        // Cannot lift out the 'if' if it contains call to non-pure function
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
            stmt = IfThenElse::make(nest[i].value, stmt, Stmt());
        } else {
            internal_assert(nest[i].type == Container::For);
            const Dim &dim = stage_s.dims()[nest[i].dim_idx];
            Expr min = Variable::make(Int(32), nest[i].name + ".loop_min");
            Expr extent = Variable::make(Int(32), nest[i].name + ".loop_extent");
            stmt = For::make(nest[i].name, min, extent, dim.for_type, dim.device_api, stmt);
        }
    }

    // Define the bounds on the split dimensions using the bounds
    // on the function args. If it is a purify, we should use the bounds
    // from the dims instead.
    for (size_t i = splits.size(); i > 0; i--) {
        const Split &split = splits[i-1];

        vector<std::pair<string, Expr>> let_stmts = compute_loop_bounds_after_split(split, prefix);
        for (size_t j = 0; j < let_stmts.size(); j++) {
            stmt = LetStmt::make(let_stmts[j].first, let_stmts[j].second, stmt);
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
        Expr min = Variable::make(Int(32), var + ".min"); // Inject instance name here? (compute instance names during lowering)
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
Stmt build_provide_loop_nest(string func_name,
                             string prefix,
                             const vector<string> &dims,
                             const FuncSchedule &f_sched,
                             const Definition &def,
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

    // Default schedule/values if there is no specialization
    Stmt stmt = build_provide_loop_nest_helper(
        func_name, prefix, dims, site, values, def.split_predicate(), f_sched, def.schedule(), is_update);

    // Make any specialized copies
    const vector<Specialization> &specializations = def.specializations();
    for (size_t i = specializations.size(); i > 0; i--) {
        const Specialization &s = specializations[i-1];
        Expr c = s.condition;
        const Definition &s_def = s.definition;
        Stmt then_case;
        if (s.failure_message.empty()) {
            then_case = build_provide_loop_nest(func_name, prefix, dims, f_sched, s_def, is_update);
        } else {
            internal_assert(equal(c, const_true()));
            // specialize_fail() should only be possible on the final specialization
            internal_assert(i == specializations.size());
            Expr specialize_fail_error =
                Internal::Call::make(Int(32),
                                     "halide_error_specialize_fail",
                                     {StringImm::make(s.failure_message)},
                                     Internal::Call::Extern);
            then_case = AssertStmt::make(const_false(), specialize_fail_error);
        }
        stmt = IfThenElse::make(c, then_case, stmt);
    }

    return stmt;
}

// Turn a function into a loop nest that computes it. It will
// refer to external vars of the form function_name.arg_name.min
// and function_name.arg_name.extent to define the bounds over
// which it should be realized. It will compute at least those
// bounds (depending on splits, it may compute more). This loop
// won't do any allocation.
Stmt build_produce(Function f, const Target &target) {

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
        vector<pair<Expr, int>> buffers_to_annotate;
        vector<Expr> buffers_contents_to_annotate;
        for (const ExternFuncArgument &arg : args) {
            if (arg.is_expr()) {
                extern_call_args.push_back(arg.expr);
            } else if (arg.is_func()) {
                Function input(arg.func);
                if (input.schedule().store_level() == input.schedule().compute_level()) {
                    for (int k = 0; k < input.outputs(); k++) {
                        string buf_name = input.name();
                        if (input.outputs() > 1) {
                            buf_name += "." + std::to_string(k);
                        }
                        buf_name += ".buffer";
                        Expr buffer = Variable::make(type_of<struct halide_buffer_t *>(), buf_name);
                        extern_call_args.push_back(buffer);
                        buffers_to_annotate.push_back({buffer, input.dimensions()});
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
                    const vector<string> input_args = input.args();
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
                        internal_assert(input.dimensions() == (int)input.args().size());
                        for (const string arg : input.args()) {
                            string var = stage_name + arg;
                            Expr min = Variable::make(Int(32), var + ".min");
                            Expr max = Variable::make(Int(32), var + ".max");
                            mins.push_back(min);
                            extents.push_back(max - min + 1);
                        }
                        args[3] = Call::make(Handle(), Call::make_struct, mins, Call::Intrinsic);
                        args[4] = Call::make(Handle(), Call::make_struct, extents, Call::Intrinsic);

                        cropped_input = Call::make(type_of<struct halide_buffer_t *>(), Call::buffer_crop,
                                                   args, Call::Extern);

                        string buf_name = input.name() + "." + std::to_string(k) + ".tmp_buffer";
                        extern_call_args.push_back(Variable::make(type_of<struct halide_buffer_t *>(), buf_name));
                        buffers_to_annotate.push_back({extern_call_args.back(), input.dimensions()});
                        buffers_contents_to_annotate.push_back(cropped_input);
                        lets.push_back({ buf_name, cropped_input });
                    }
                }
            } else if (arg.is_buffer()) {
                Buffer<> b = arg.buffer;
                Parameter p(b.type(), true, b.dimensions(), b.name());
                p.set_buffer(b);
                Expr buf = Variable::make(type_of<struct halide_buffer_t *>(), b.name() + ".buffer", p);
                extern_call_args.push_back(buf);
                buffers_to_annotate.push_back({buf, b.dimensions()});
                buffers_contents_to_annotate.push_back(buf);
            } else if (arg.is_image_param()) {
                Parameter p = arg.image_param;
                Expr buf = Variable::make(type_of<struct halide_buffer_t *>(), p.name() + ".buffer", p);
                extern_call_args.push_back(buf);
                // Do not annotate ImageParams: both the buffer_t itself,
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
        vector<pair<Expr, Expr>> cropped_buffers;
        if (f.schedule().store_level() == f.schedule().compute_level()) {
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
                buffers_to_annotate.push_back({buffer, f.dimensions()});
            }
        } else {
            // Store level doesn't match compute level. Make an output
            // buffer just for this subregion.
            string stage_name = f.name() + ".s0.";
            const vector<string> f_args = f.args();
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
                internal_assert(f.dimensions() == (int)f.args().size());
                for (const string arg : f.args()) {
                    string var = stage_name + arg;
                    Expr min = Variable::make(Int(32), var + ".min");
                    Expr max = Variable::make(Int(32), var + ".max");
                    mins.push_back(min);
                    extents.push_back(max - min + 1);
                }
                args[3] = Call::make(Handle(), Call::make_struct, mins, Call::Intrinsic);
                args[4] = Call::make(Handle(), Call::make_struct, extents, Call::Intrinsic);

                output_buffer_t = Call::make(type_of<struct halide_buffer_t *>(), Call::buffer_crop, args, Call::Extern);

                string buf_name = f.name() + "." + std::to_string(j) + ".tmp_buffer";
                extern_call_args.push_back(Variable::make(type_of<struct halide_buffer_t *>(), buf_name));
                // Since this is a temporary, internal-only buffer, make sure it's marked.
                // (but not the contents! callee is expected to fill that in.)
                buffers_to_annotate.push_back({extern_call_args.back(), f.dimensions()});
                cropped_buffers.push_back({extern_call_args.back(), src_buffer});
                lets.push_back({ buf_name, output_buffer_t });
            }
        }

        Stmt annotate;
        if (target.has_feature(Target::MSAN)) {
            // Mark the buffers as initialized before calling out.
            for (const auto &p: buffers_to_annotate) {
                Expr buffer = p.first;
                int dimensions = p.second;
                // Return type is really 'void', but no way to represent that in our IR.
                // Precedent (from halide_print, etc) is to use Int(32) and ignore the result.
                Expr sizeof_buffer_t = cast<uint64_t>(Call::make(Int(32), Call::size_of_halide_buffer_t, {}, Call::Intrinsic));
                Stmt mark_buffer =
                    Evaluate::make(Call::make(Int(32), "halide_msan_annotate_memory_is_initialized",
                                              {buffer, sizeof_buffer_t}, Call::Extern));
                Expr shape = Call::make(type_of<halide_dimension_t *>(), Call::buffer_get_shape, {buffer}, Call::Extern);
                Expr shape_size = Expr((uint64_t)(sizeof(halide_dimension_t) * dimensions));
                Stmt mark_shape =
                    Evaluate::make(Call::make(Int(32), "halide_msan_annotate_memory_is_initialized",
                                              {shape, shape_size}, Call::Extern));
                mark_buffer = Block::make(mark_buffer, mark_shape);
                if (annotate.defined()) {
                    annotate = Block::make(annotate, mark_buffer);
                } else {
                    annotate = mark_buffer;
                }
            }
            for (const auto &buffer: buffers_contents_to_annotate) {
                // Return type is really 'void', but no way to represent that in our IR.
                // Precedent (from halide_print, etc) is to use Int(32) and ignore the result.
                Stmt mark_contents = Evaluate::make(Call::make(Int(32), "halide_msan_annotate_buffer_is_initialized", {buffer}, Call::Extern));
                if (annotate.defined()) {
                    annotate = Block::make(annotate, mark_contents);
                } else {
                    annotate = mark_contents;
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
        check = LetStmt::make(result_name, e, check);

        if (!cropped_buffers.empty()) {
            // We need to clean up the temporary crops we made for the
            // outputs in case any of them have device allocations.
            vector<Expr> cleanup_args;

            // Make a struct with the buffers and their uncropped parents
            for (auto p : cropped_buffers) {
                // The cropped buffer_t
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

            // We have to anticipate failures in the extern stage, so
            // call this by registering a destructor before the extern
            // call and triggering it afterwards using an Allocate node.
            string destructor_name = unique_name('d');
            const char *fn = (cropped_buffers.size() == 1 ?
                              "_halide_buffer_retire_crop_after_extern_stage" :
                              "_halide_buffer_retire_crops_after_extern_stage");
            check = Allocate::make(destructor_name, Handle(), {},
                                   const_true(), check, cleanup_struct, fn);
        }

        for (size_t i = 0; i < lets.size(); i++) {
            check = LetStmt::make(lets[i].first, lets[i].second, check);
        }

        if (annotate.defined()) {
            check = Block::make(annotate, check);
        }

        // Add the dummy outermost loop.
        string outermost = f.name() + ".s0." + Var::outermost().name();
        check = For::make(outermost, 0, 1, ForType::Serial, DeviceAPI::None, check);

        return check;
    } else {

        string prefix = f.name() + ".s0.";
        vector<string> dims = f.args();
        return build_provide_loop_nest(f.name(), prefix, dims, f.schedule(), f.definition(), false);
    }
}

// Build the loop nests that update a function (assuming it's a reduction).
vector<Stmt> build_update(Function f) {

    vector<Stmt> updates;

    for (size_t i = 0; i < f.updates().size(); i++) {
        const Definition &def = f.update(i);

        string prefix = f.name() + ".s" + std::to_string(i+1) + ".";

        vector<string> dims = f.args();
        Stmt loop = build_provide_loop_nest(f.name(), prefix, dims, f.schedule(), def, true);
        updates.push_back(loop);
    }

    return updates;
}

pair<Stmt, Stmt> build_production(Function func, const Target &target) {
    Stmt produce = build_produce(func, target);
    vector<Stmt> updates = build_update(func);

    // Combine the update steps
    Stmt merged_updates = Block::make(updates);
    return { produce, merged_updates };
}

// A schedule may include explicit bounds on some dimension. This
// injects assertions that check that those bounds are sufficiently
// large to cover the inferred bounds required.
Stmt inject_explicit_bounds(Stmt body, Function func) {
    const FuncSchedule &s = func.schedule();
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
class InjectRealization : public IRMutator2 {
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

    Stmt build_pipeline(Stmt consumer) {
        pair<Stmt, Stmt> realization = build_production(func, target);

        Stmt producer;
        if (realization.first.defined() && realization.second.defined()) {
            producer = Block::make(realization.first, realization.second);
        } else if (realization.first.defined()) {
            producer = realization.first;
        } else {
            internal_assert(realization.second.defined());
            producer = realization.second;
        }
        producer = ProducerConsumer::make_produce(func.name(), producer);

        // Outputs don't have consume nodes
        if (!is_output) {
            consumer = ProducerConsumer::make_consume(func.name(), consumer);
        }

        if (is_no_op(consumer)) {
            // For the very first output to be scheduled, the consumer
            // Stmt will be a no-op. No point in preserving it.
            return producer;
        } else {
            return Block::make(producer, consumer);
        }
    }

    Stmt build_realize(Stmt s) {
        if (!is_output) {
            Region bounds;
            string name = func.name();
            const vector<string> func_args = func.args();
            for (int i = 0; i < func.dimensions(); i++) {
                const string &arg = func_args[i];
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

    using IRMutator2::visit;

    Stmt visit(const ProducerConsumer *op) override {
        if (op->is_producer) {
            string old = producing;
            producing = op->name;
            Stmt body = mutate(op->body);
            producing = old;

            if (body.same_as(op->body)) {
                return op;
            } else {
                return ProducerConsumer::make(op->name, op->is_producer, body);
            }
        } else {
            return IRMutator2::visit(op);
        }
    }

    Stmt visit(const For *for_loop) override {
        debug(3) << "InjectRealization of " << func.name() << " entering for loop over " << for_loop->name << "\n";
        const LoopLevel &compute_level = func.schedule().compute_level();
        const LoopLevel &store_level = func.schedule().store_level();

        Stmt body = for_loop->body;

        // Dig through any let statements
        vector<pair<string, Expr>> lets;
        while (const LetStmt *l = body.as<LetStmt>()) {
            if (!is_pure(l->value)) {
                // The consumer of the Func we're injecting may be an
                // extern stage, which shows up in the IR as a let
                // stmt with a side-effecty RHS. We need to take care
                // not to blow past it and risk injecting the producer
                // *after* the consumer. In general it seems unwise to
                // reorder the computation of a Func past something
                // side-effecty, so we stop here.
                break;
            }
            lets.push_back({ l->name, l->value });
            body = l->body;
        }

        // Can't schedule extern things inside a vector for loop
        if (func.has_extern_definition() &&
            func.schedule().compute_level().is_inlined() &&
            for_loop->for_type == ForType::Vectorized &&
            function_is_used_in_stmt(func, for_loop)) {

            // If we're trying to inline an extern function, schedule it here and bail out
            debug(2) << "Injecting realization of " << func.name() << " around node " << Stmt(for_loop) << "\n";
            Stmt stmt = build_realize(build_pipeline(for_loop));
            found_store_level = found_compute_level = true;
            return stmt;
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
            return for_loop;
        } else {
            return For::make(for_loop->name,
                             for_loop->min,
                             for_loop->extent,
                             for_loop->for_type,
                             for_loop->device_api,
                             body);
        }
    }

    // If we're an inline update or extern, we may need to inject a realization here
    Stmt visit(const Provide *op) override {
        if (op->name != func.name() &&
            !func.is_pure() &&
            func.schedule().compute_level().is_inlined() &&
            function_is_used_in_stmt(func, op)) {

            // Prefix all calls to func in op
            Stmt stmt = build_realize(build_pipeline(op));
            found_store_level = found_compute_level = true;
            return stmt;
        } else {
            return op;
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
    bool found;

    ComputeLegalSchedules(Function f, const map<string, Function> &env) : found(false), func(f), env(env) {}

private:
    using IRVisitor::visit;

    vector<Site> sites;
    Function func;

    const map<string, Function> &env;

    void visit(const For *f) {
        f->min.accept(this);
        f->extent.accept(this);
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
        Site s = {f->is_parallel() ||
                  f->for_type == ForType::Vectorized,
                  loop_level};
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
            ends_with(op->name, LoopLevel::root().lock().to_string())) {
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
        if (op->is_producer) {
            string old_caller = caller;
            caller = op->name;
            op->body.accept(this);
            caller = old_caller;
        } else {
            IRVisitor::visit(op);
        }
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

// Check a schedule is legal, throwing an error if it is not. Returns
// whether or not a realization of the Func should be injected. Unused
// intermediate Funcs that somehow made it into the Func DAG can be
// discarded.
bool validate_schedule(Function f, Stmt s, const Target &target, bool is_output, const map<string, Function> &env) {

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
    }

    // Emit a warning if only some of the steps have been scheduled.
    bool any_scheduled = f.has_pure_definition() && f.definition().schedule().touched();
    for (const Definition &r : f.updates()) {
        any_scheduled = any_scheduled || r.schedule().touched();
    }
    if (any_scheduled) {
        for (size_t i = 0; i < f.updates().size(); i++) {
            const Definition &r = f.update(i);
            if (!r.schedule().touched()) {
                user_warning << "Warning: Update step " << i
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

    for (const Definition &def : definitions) {
        const StageSchedule &s = def.schedule();
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

    // Check if the schedule of the inlined function is legal. Since only
    // pure function can be inlined, we only need to call the validator on
    // pure function. An inlined Halide Func with multiple stages technically
    // will get lowered into compute_at innermost and thus can be treated
    // similarly as a non-inlined Func.
    if (store_at.is_inlined() && compute_at.is_inlined()) {
        if (f.is_pure()) {
            validate_schedule_inlined_function(f);
        }
        return true;
    }

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
                    << sites[i].loop_level.to_string()
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

    return true;
}

class RemoveLoopsOverOutermost : public IRMutator2 {
    using IRMutator2::visit;

    Stmt visit(const For *op) override {
        if (ends_with(op->name, ".__outermost") &&
            is_one(simplify(op->extent)) &&
            op->device_api == DeviceAPI::None) {
            return mutate(substitute(op->name, op->min, op->body));
        } else {
            return IRMutator2::visit(op);
        }
    }

    Stmt visit(const LetStmt *op) override {
        if (ends_with(op->name, ".__outermost.loop_extent") ||
            ends_with(op->name, ".__outermost.loop_min") ||
            ends_with(op->name, ".__outermost.loop_max")) {
            return mutate(substitute(op->name, simplify(op->value), op->body));
        } else {
            return IRMutator2::visit(op);
        }
    }
};

Stmt schedule_functions(const vector<Function> &outputs,
                        const vector<string> &order,
                        const map<string, Function> &env,
                        const Target &target,
                        bool &any_memoized) {

    string root_var = LoopLevel::root().lock().to_string();
    Stmt s = For::make(root_var, 0, 1, ForType::Serial, DeviceAPI::Host, Evaluate::make(0));

    any_memoized = false;

    for (size_t i = order.size(); i > 0; i--) {
        Function f = env.find(order[i-1])->second;

        bool is_output = false;
        for (Function o : outputs) {
            is_output |= o.same_as(f);
        }

        bool necessary = validate_schedule(f, s, target, is_output, env);

        if (!necessary) {
            // The way in which the function was referred to in the
            // function DAG must not actually result in a use in the
            // code. This can happen if you inline a Tuple function,
            // ignoring one of the Tuple elements, and that Tuple
            // element is the sole call to a function with an update
            // definition.
            continue;
        }

        if (f.can_be_inlined() &&
            f.schedule().compute_level().is_inlined()) {
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
