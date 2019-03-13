#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>

#include "Lower.h"

#include "AddImageChecks.h"
#include "AddParameterChecks.h"
#include "AllocationBoundsInference.h"
#include "AsyncProducers.h"
#include "BoundSmallAllocations.h"
#include "Bounds.h"
#include "BoundsInference.h"
#include "CSE.h"
#include "CanonicalizeGPUVars.h"
#include "Debug.h"
#include "DebugArguments.h"
#include "DebugToFile.h"
#include "Deinterleave.h"
#include "EarlyFree.h"
#include "FindCalls.h"
#include "Func.h"
#include "Function.h"
#include "FuseGPUThreadLoops.h"
#include "FuzzFloatStores.h"
#include "HexagonOffload.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "InferArguments.h"
#include "InjectHostDevBufferCopies.h"
#include "InjectOpenGLIntrinsics.h"
#include "Inline.h"
#include "LICM.h"
#include "LoopCarry.h"
#include "LowerWarpShuffles.h"
#include "Memoization.h"
#include "PartitionLoops.h"
#include "PurifyIndexMath.h"
#include "Prefetch.h"
#include "Profiling.h"
#include "Qualify.h"
#include "RealizationOrder.h"
#include "RemoveDeadAllocations.h"
#include "RemoveExternLoops.h"
#include "RemoveTrivialForLoops.h"
#include "RemoveUndef.h"
#include "ScheduleFunctions.h"
#include "SelectGPUAPI.h"
#include "Simplify.h"
#include "SimplifySpecializations.h"
#include "SkipStages.h"
#include "SlidingWindow.h"
#include "SplitTuples.h"
#include "StorageFlattening.h"
#include "StorageFolding.h"
#include "StrictifyFloat.h"
#include "Substitute.h"
#include "Tracing.h"
#include "TrimNoOps.h"
#include "UnifyDuplicateLets.h"
#include "UniquifyVariableNames.h"
#include "UnpackBuffers.h"
#include "UnsafePromises.h"
#include "UnrollLoops.h"
#include "VaryingAttributes.h"
#include "VectorizeLoops.h"
#include "WrapCalls.h"
#include "WrapExternStages.h"

// TODO: Only here for store_with
#include "ExprUsesVar.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::map;
using std::ostringstream;
using std::set;
using std::string;
using std::vector;
using std::pair;

// TODO: move this

// Return the symbolic times and sites for all uses of a buffer.
struct Use {
    vector<Expr> time, site;
    Expr predicate;

    Use(const vector<Expr> &t, const vector<Expr> &s, const Expr &p) : time(t), site(s), predicate(p) {
        // Make any variables unique to this use
        map<string, Expr> renaming;
        for (auto &e : time) {
            if (const Variable *v = e.as<Variable>()) {
                Expr new_var = Variable::make(Int(32), unique_name(v->name));
                renaming[v->name] = new_var;
                e = new_var;
            }
        }
        for (auto &e : site) {
            e = substitute(renaming, e);
        }
        predicate = substitute(renaming, predicate);
    }
    Use() = default;

    Expr happens_before(const Use &other, size_t first_idx = 0) const {
        // Lexicographic order starting at the given index
        if (first_idx < other.time.size() && first_idx >= time.size()) return const_true();
        if (first_idx >= other.time.size() && first_idx <= time.size()) return const_false();

        Expr tail = happens_before(other, first_idx + 1);

        // We only care about the ordering of the tail if the times match, so do a substitution
        tail = substitute(time[first_idx], other.time[first_idx], tail);

        return (time[first_idx] < other.time[first_idx]) || (time[first_idx] == other.time[first_idx] && tail);
    }

    Expr safely_before(const Use &other) const {
        // We want to return 'same_site(other) => happens_before(other)'

        // We could just construct and return that, encoding A => B as
        // !A || B, but that's asking the simplifier to do a lot of
        // substitutions that we could just do directly.

        Expr before = simplify(happens_before(other));

        debug(0) << "Before: " << before << "\n";

        Expr same_site = const_true();
        for (size_t i = 0; i < site.size(); i++) {
            Expr c = simplify(site[i] == other.site[i]);
            if (const EQ *eq = c.as<EQ>()) {
                // Perform a substitution
                before = substitute(eq->a, eq->b, before);
                same_site = substitute(eq->a, eq->b, same_site);
            }
            same_site = c && same_site;
        }
        debug(0) << "LHS: " << simplify(same_site) << "\n";
        debug(0) << "RHS: " << simplify(before) << "\n";

        return !(same_site && predicate && other.predicate) || before;
    }
};
std::vector<Use> get_times_of_all_uses(const Stmt &s, string buf) {
    class PolyhedralClock : public IRVisitor {
        using IRVisitor::visit;

        vector<Expr> clock;
        vector<Expr> predicate;
        const string &buf;

        void visit(const Block *op) override {
            int i = 0;
            clock.push_back(i);
            Stmt rest;
            do {
                op->first.accept(this);
                rest = op->rest;
                clock.back() = ++i;
            } while ((op = rest.as<Block>()));
            rest.accept(this);
            clock.pop_back();
        }

        void visit(const For *op) override {
            Expr loop_var = Variable::make(Int(32), op->name);
            predicate.push_back(loop_var >= op->min && loop_var < op->min + op->extent);
            if (op->is_parallel()) {
                // No useful ordering, so add nothing to the clock
                op->body.accept(this);
            } else {
                clock.push_back(Variable::make(Int(32), op->name));
                op->body.accept(this);
                clock.pop_back();
            }
            predicate.pop_back();
        }

        void found_use(const vector<Expr> &site) {
            Expr p = const_true();
            for (const auto &e : predicate) {
                p = p && e;
            }
            uses.emplace_back(clock, site, p);
            for (auto it = lets.rbegin(); it != lets.rend(); it++) {
                for (auto &e : uses.back().site) {
                    if (expr_uses_var(e, it->first)) {
                        e = Let::make(it->first, it->second, e);
                    }
                }
                if (expr_uses_var(p, it->first)) {
                    uses.back().predicate = Let::make(it->first, it->second, uses.back().predicate);
                }
            }
        }

        vector<pair<string, Expr>> lets;

        void visit(const Let *op) override {
            op->value.accept(this);
            lets.emplace_back(op->name, op->value);
            op->body.accept(this);
            lets.pop_back();
        }

        void visit(const LetStmt *op) override {
            op->value.accept(this);
            lets.emplace_back(op->name, op->value);
            op->body.accept(this);
            lets.pop_back();
        }

        void visit(const Provide *op) override {
            // The RHS is evaluated before the store happens
            clock.push_back(0);
            IRVisitor::visit(op);
            clock.back() = 1;
            if (op->name == buf) {
                found_use(op->args);
            }
            clock.pop_back();
        }

        void visit(const Call *op) override {
            IRVisitor::visit(op);
            if (op->name == buf) {
                found_use(op->args);
            }
        }
    public:
        vector<Use> uses;
        PolyhedralClock(std::string &b) : buf(b) {}
    } clock(buf);

    s.accept(&clock);

    for (const auto &u : clock.uses) {
        debug(0) << "Use of " << buf << ":\n";
        debug(0) << "Clock: ";
        for (const auto &e : u.time) {
            debug(0) << e << " ";
        }
        debug(0) << "\n";
        debug(0) << "Site: ";
        for (const auto &e : u.site) {
            debug(0) << e << " ";
        }
        debug(0) << "\n";
    }

    return clock.uses;
}

Stmt lower_store_with(const Stmt &s, const map<string, Function> &env) {
    // First check legality on a simplified version of the stmt
    Stmt simpler = simplify(uniquify_variable_names(s));
    debug(0) << "Checking legality of store_with on: " << simpler << "\n";
    for (const auto &p : env) {
        const std::string &stored_with = p.second.schedule().store_with();
        if (!stored_with.empty()) {
            auto uses_of_1 = get_times_of_all_uses(simpler, p.first);
            auto uses_of_2 = get_times_of_all_uses(simpler, stored_with);

            // Check all uses of 1 are before all uses of 2, or vice-versa
            Expr check1 = const_true(), check2 = const_true();
            for (const auto &u1 : uses_of_1) {
                for (const auto &u2 : uses_of_2) {
                    debug(0) << "u1 before u2: " << u1.safely_before(u2) << "\n";
                    debug(0) << "u2 before u1: " << u2.safely_before(u1) << "\n";

                    check1 = check1 && u1.safely_before(u2);
                    check2 = check2 && u2.safely_before(u1);
                }
            }

            debug(0) << "Check1: " << check1 << '\n';
            debug(0) << "Check2: " << check2 << '\n';

            Expr check = simplify(common_subexpression_elimination(check1 || check2));
            user_assert(can_prove(check))
                << "Could not prove it's safe to store " << p.first
                << " in the same buffer as " << stored_with << '\n'
                << "Condition: " << check << "\n";
        }
    }

    class LowerStoreWith : public IRMutator {
        using IRMutator::visit;

        Stmt visit(const Realize *op) override {
            auto it = env.find(op->name);
            if (it != env.end() &&
                !it->second.schedule().store_with().empty()) {
                return mutate(op->body);
            } else {
                return IRMutator::visit(op);
            }
        }

        Stmt visit(const Provide *op) override {
            auto it = env.find(op->name);
            internal_assert(it != env.end());

            string stored_with = it->second.schedule().store_with();
            // Assumes no transitive buggery

            if (it->second.schedule().store_with().empty()) {
                return IRMutator::visit(op);
            }

            // TODO: assert stored_with in scope

            Stmt p = IRMutator::visit(op);
            op = p.as<Provide>();
            return Provide::make(stored_with, op->values, op->args);
        }

        Expr visit(const Call *op) override {
            if (op->call_type != Call::Halide) {
                return IRMutator::visit(op);
            }

            auto it = env.find(op->name);
            internal_assert(it != env.end());

            string stored_with = it->second.schedule().store_with();
            // Assumes no transitive buggery

            // TODO: assert stored_with in scope

            if (it->second.schedule().store_with().empty()) {
                return IRMutator::visit(op);
            }

            Expr c = IRMutator::visit(op);
            op = c.as<Call>();
            it = env.find(stored_with);
            internal_assert(it != env.end());
            return Call::make(it->second, op->args, op->value_index);
        }

        const map<string, Function> &env;
    public:
        LowerStoreWith(const map<string, Function> &env) : env(env) {}
    } m(env);
    return m.mutate(s);
}

Module lower(const vector<Function> &output_funcs,
             const string &pipeline_name,
             const Target &t,
             const vector<Argument> &args,
             const LinkageType linkage_type,
             const vector<Stmt> &requirements,
             const vector<IRMutator *> &custom_passes) {
    std::vector<std::string> namespaces;
    std::string simple_pipeline_name = extract_namespaces(pipeline_name, namespaces);

    Module result_module(simple_pipeline_name, t);

    // Compute an environment
    map<string, Function> env;
    for (Function f : output_funcs) {
        populate_environment(f, env);
    }

    // Create a deep-copy of the entire graph of Funcs.
    vector<Function> outputs;
    std::tie(outputs, env) = deep_copy(output_funcs, env);

    bool any_strict_float = strictify_float(env, t);
    result_module.set_any_strict_float(any_strict_float);

    // Output functions should all be computed and stored at root.
    for (Function f: outputs) {
        Func(f).compute_root().store_root();
    }

    // Finalize all the LoopLevels
    for (auto &iter : env) {
        iter.second.lock_loop_levels();
    }

    // Substitute in wrapper Funcs
    env = wrap_func_calls(env);

    // Compute a realization order and determine group of functions which loops
    // are to be fused together
    vector<string> order;
    vector<vector<string>> fused_groups;
    std::tie(order, fused_groups) = realization_order(outputs, env);

    // Try to simplify the RHS/LHS of a function definition by propagating its
    // specializations' conditions
    simplify_specializations(env);

    debug(1) << "Creating initial loop nests...\n";
    bool any_memoized = false;
    Stmt s = schedule_functions(outputs, fused_groups, env, t, any_memoized);
    debug(2) << "Lowering after creating initial loop nests:\n" << s << '\n';

    if (any_memoized) {
        debug(1) << "Injecting memoization...\n";
        s = inject_memoization(s, env, pipeline_name, outputs);
        debug(2) << "Lowering after injecting memoization:\n" << s << '\n';
    } else {
        debug(1) << "Skipping injecting memoization...\n";
    }

    debug(1) << "Injecting tracing...\n";
    s = inject_tracing(s, pipeline_name, env, outputs, t);
    debug(2) << "Lowering after injecting tracing:\n" << s << '\n';

    debug(1) << "Adding checks for parameters\n";
    s = add_parameter_checks(requirements, s, t);
    debug(2) << "Lowering after injecting parameter checks:\n" << s << '\n';

    // Compute the maximum and minimum possible value of each
    // function. Used in later bounds inference passes.
    debug(1) << "Computing bounds of each function's value\n";
    FuncValueBounds func_bounds = compute_function_value_bounds(order, env);

    // The checks will be in terms of the symbols defined by bounds
    // inference.
    debug(1) << "Adding checks for images\n";
    s = add_image_checks(s, outputs, t, order, env, func_bounds);
    debug(2) << "Lowering after injecting image checks:\n" << s << '\n';

    // This pass injects nested definitions of variable names, so we
    // can't simplify statements from here until we fix them up. (We
    // can still simplify Exprs).
    debug(1) << "Performing computation bounds inference...\n";
    s = bounds_inference(s, outputs, order, fused_groups, env, func_bounds, t);
    debug(2) << "Lowering after computation bounds inference:\n" << s << '\n';

    debug(1) << "Removing extern loops...\n";
    s = remove_extern_loops(s);
    debug(2) << "Lowering after removing extern loops:\n" << s << '\n';

    debug(1) << "Performing sliding window optimization...\n";
    s = sliding_window(s, env);
    debug(2) << "Lowering after sliding window:\n" << s << '\n';

    debug(1) << "Merging buffers using store_with directives...\n";
    s = lower_store_with(s, env);
    debug(2) << "Lowering after merging buffers:\n" << s << '\n';

    debug(1) << "Performing allocation bounds inference...\n";
    // TODO: respect store_with
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

    debug(1) << "Simplifying...\n";
    s = simplify(s, false); // Storage folding needs .loop_max symbols
    debug(2) << "Lowering after first simplification:\n" << s << "\n\n";

    debug(1) << "Performing storage folding optimization...\n";
    s = storage_folding(s, env);
    debug(2) << "Lowering after storage folding:\n" << s << '\n';

    debug(1) << "Injecting debug_to_file calls...\n";
    s = debug_to_file(s, outputs, env);
    debug(2) << "Lowering after injecting debug_to_file calls:\n" << s << '\n';

    debug(1) << "Injecting prefetches...\n";
    s = inject_prefetch(s, env);
    debug(2) << "Lowering after injecting prefetches:\n" << s << "\n\n";

    debug(1) << "Dynamically skipping stages...\n";
    s = skip_stages(s, order);
    debug(2) << "Lowering after dynamically skipping stages:\n" << s << "\n\n";

    debug(1) << "Forking asynchronous producers...\n";
    s = fork_async_producers(s, env);
    debug(2) << "Lowering after forking asynchronous producers:\n" << s << '\n';

    debug(1) << "Destructuring tuple-valued realizations...\n";
    s = split_tuples(s, env);
    debug(2) << "Lowering after destructuring tuple-valued realizations:\n" << s << "\n\n";

    // OpenGL relies on GPU var canonicalization occurring before
    // storage flattening.
    if (t.has_gpu_feature() ||
        t.has_feature(Target::OpenGLCompute) ||
        t.has_feature(Target::OpenGL)) {
        debug(1) << "Canonicalizing GPU var names...\n";
        s = canonicalize_gpu_vars(s);
        debug(2) << "Lowering after canonicalizing GPU var names:\n"
                 << s << '\n';
    }

    debug(1) << "Performing storage flattening...\n";
    s = storage_flattening(s, outputs, env, t);
    debug(2) << "Lowering after storage flattening:\n" << s << "\n\n";

    debug(1) << "Unpacking buffer arguments...\n";
    s = unpack_buffers(s);
    debug(2) << "Lowering after unpacking buffer arguments...\n" << s << "\n\n";

    if (any_memoized) {
        debug(1) << "Rewriting memoized allocations...\n";
        s = rewrite_memoized_allocations(s, env);
        debug(2) << "Lowering after rewriting memoized allocations:\n" << s << "\n\n";
    } else {
        debug(1) << "Skipping rewriting memoized allocations...\n";
    }

    if (t.has_gpu_feature() ||
        t.has_feature(Target::OpenGLCompute) ||
        t.has_feature(Target::OpenGL) ||
        t.has_feature(Target::HexagonDma) ||
        (t.arch != Target::Hexagon && (t.features_any_of({Target::HVX_64, Target::HVX_128})))) {
        debug(1) << "Selecting a GPU API for GPU loops...\n";
        s = select_gpu_api(s, t);
        debug(2) << "Lowering after selecting a GPU API:\n" << s << "\n\n";

        debug(1) << "Injecting host <-> dev buffer copies...\n";
        s = inject_host_dev_buffer_copies(s, t);
        debug(2) << "Lowering after injecting host <-> dev buffer copies:\n" << s << "\n\n";

        debug(1) << "Selecting a GPU API for extern stages...\n";
        s = select_gpu_api(s, t);
        debug(2) << "Lowering after selecting a GPU API for extern stages:\n" << s << "\n\n";
    }

    if (t.has_feature(Target::OpenGL)) {
        debug(1) << "Injecting OpenGL texture intrinsics...\n";
        s = inject_opengl_intrinsics(s);
        debug(2) << "Lowering after OpenGL intrinsics:\n" << s << "\n\n";
    }

    debug(1) << "Simplifying...\n";
    s = simplify(s);
    s = unify_duplicate_lets(s);
    s = remove_trivial_for_loops(s);
    debug(2) << "Lowering after second simplifcation:\n" << s << "\n\n";

    debug(1) << "Reduce prefetch dimension...\n";
    s = reduce_prefetch_dimension(s, t);
    debug(2) << "Lowering after reduce prefetch dimension:\n" << s << "\n";

    debug(1) << "Unrolling...\n";
    s = unroll_loops(s);
    s = simplify(s);
    debug(2) << "Lowering after unrolling:\n" << s << "\n\n";

    debug(1) << "Vectorizing...\n";
    s = vectorize_loops(s, t);
    s = simplify(s);
    debug(2) << "Lowering after vectorizing:\n" << s << "\n\n";

    if (t.has_gpu_feature() ||
        t.has_feature(Target::OpenGLCompute)) {
        debug(1) << "Injecting per-block gpu synchronization...\n";
        s = fuse_gpu_thread_loops(s);
        debug(2) << "Lowering after injecting per-block gpu synchronization:\n" << s << "\n\n";
    }

    debug(1) << "Detecting vector interleavings...\n";
    s = rewrite_interleavings(s);
    s = simplify(s);
    debug(2) << "Lowering after rewriting vector interleavings:\n" << s << "\n\n";

    debug(1) << "Partitioning loops to simplify boundary conditions...\n";
    s = partition_loops(s);
    s = simplify(s);
    debug(2) << "Lowering after partitioning loops:\n" << s << "\n\n";

    debug(1) << "Trimming loops to the region over which they do something...\n";
    s = trim_no_ops(s);
    debug(2) << "Lowering after loop trimming:\n" << s << "\n\n";

    debug(1) << "Injecting early frees...\n";
    s = inject_early_frees(s);
    debug(2) << "Lowering after injecting early frees:\n" << s << "\n\n";

    if (t.has_feature(Target::FuzzFloatStores)) {
        debug(1) << "Fuzzing floating point stores...\n";
        s = fuzz_float_stores(s);
        debug(2) << "Lowering after fuzzing floating point stores:\n" << s << "\n\n";
    }

    debug(1) << "Bounding small allocations...\n";
    s = bound_small_allocations(s);
    debug(2) << "Lowering after bounding small allocations:\n" << s << "\n\n";

    if (t.has_feature(Target::Profile)) {
        debug(1) << "Injecting profiling...\n";
        s = inject_profiling(s, pipeline_name);
        debug(2) << "Lowering after injecting profiling:\n" << s << "\n\n";
    }

    if (t.has_feature(Target::CUDA)) {
        debug(1) << "Injecting warp shuffles...\n";
        s = lower_warp_shuffles(s);
        debug(2) << "Lowering after injecting warp shuffles:\n" << s << "\n\n";
    }

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

    debug(1) << "Lowering unsafe promises...\n";
    s = lower_unsafe_promises(s, t);
    debug(2) << "Lowering after lowering unsafe promises:\n" << s << "\n\n";

    s = remove_dead_allocations(s);
    s = remove_trivial_for_loops(s);
    s = simplify(s);
    s = loop_invariant_code_motion(s);
    debug(1) << "Lowering after final simplification:\n" << s << "\n\n";

    if (t.arch != Target::Hexagon && (t.features_any_of({Target::HVX_64, Target::HVX_128}))) {
        debug(1) << "Splitting off Hexagon offload...\n";
        s = inject_hexagon_rpc(s, t, result_module);
        debug(2) << "Lowering after splitting off Hexagon offload:\n" << s << '\n';
    } else {
        debug(1) << "Skipping Hexagon offload...\n";
    }

    if (!custom_passes.empty()) {
        for (size_t i = 0; i < custom_passes.size(); i++) {
            debug(1) << "Running custom lowering pass " << i << "...\n";
            s = custom_passes[i]->mutate(s);
            debug(1) << "Lowering after custom pass " << i << ":\n" << s << "\n\n";
        }
    }

    vector<Argument> public_args = args;
    for (const auto &out : outputs) {
        for (Parameter buf : out.output_buffers()) {
            public_args.push_back(Argument(buf.name(),
                                           Argument::OutputBuffer,
                                           buf.type(), buf.dimensions(), buf.get_argument_estimates()));
        }
    }

    vector<InferredArgument> inferred_args = infer_arguments(s, outputs);
    for (const InferredArgument &arg : inferred_args) {
        if (arg.param.defined() && arg.param.name() == "__user_context") {
            // The user context is always in the inferred args, but is
            // not required to be in the args list.
            continue;
        }

        internal_assert(arg.arg.is_input()) << "Expected only input Arguments here";

        bool found = false;
        for (Argument a : args) {
            found |= (a.name == arg.arg.name);
        }

        if (arg.buffer.defined() && !found) {
            // It's a raw Buffer used that isn't in the args
            // list. Embed it in the output instead.
            debug(1) << "Embedding image " << arg.buffer.name() << "\n";
            result_module.append(arg.buffer);
        } else if (!found) {
            std::ostringstream err;
            err << "Generated code refers to ";
            if (arg.arg.is_buffer()) {
                err << "image ";
            }
            err << "parameter " << arg.arg.name
                << ", which was not found in the argument list.\n";

            err << "\nArgument list specified: ";
            for (size_t i = 0; i < args.size(); i++) {
                err << args[i].name << " ";
            }
            err << "\n\nParameters referenced in generated code: ";
            for (const InferredArgument &ia : inferred_args) {
                if (ia.arg.name != "__user_context") {
                    err << ia.arg.name << " ";
                }
            }
            err << "\n\n";
            user_error << err.str();
        }
    }

    // We're about to drop the environment and outputs vector, which
    // contain the only strong refs to Functions that may still be
    // pointed to by the IR. So make those refs strong.
    class StrengthenRefs : public IRMutator {
        using IRMutator::visit;
        Expr visit(const Call *c) override {
            Expr expr = IRMutator::visit(c);
            c = expr.as<Call>();
            internal_assert(c);
            if (c->func.defined()) {
                FunctionPtr ptr = c->func;
                ptr.strengthen();
                expr = Call::make(c->type, c->name, c->args, c->call_type,
                                  ptr, c->value_index,
                                  c->image, c->param);
            }
            return expr;
        }
    };
    s = StrengthenRefs().mutate(s);

    LoweredFunc main_func(pipeline_name, public_args, s, linkage_type);

    // If we're in debug mode, add code that prints the args.
    if (t.has_feature(Target::Debug)) {
        debug_arguments(&main_func);
    }

    result_module.append(main_func);

    // Append a wrapper for this pipeline that accepts old buffer_ts
    // and upgrades them. It will use the same name, so it will
    // require C++ linkage. We don't need it when jitting.
    if (!t.has_feature(Target::JIT)) {
        add_legacy_wrapper(result_module, main_func);
    }

    return result_module;
}

Stmt lower_main_stmt(const std::vector<Function> &output_funcs,
                     const std::string &pipeline_name,
                     const Target &t,
                     const std::vector<Stmt> &requirements,
                     const std::vector<IRMutator *> &custom_passes) {
    // We really ought to start applying for appellation d'origine contrôlée
    // status on types representing arguments in the Halide compiler.
    vector<InferredArgument> inferred_args = infer_arguments(Stmt(), output_funcs);
    vector<Argument> args;
    for (const auto &ia : inferred_args) {
        if (!ia.arg.name.empty() && ia.arg.is_input()) {
            args.push_back(ia.arg);
        }
    }

    Module module = lower(output_funcs, pipeline_name, t, args, LinkageType::External, requirements, custom_passes);

    return module.functions().front().body;
}

}  // namespace Internal
}  // namespace Halide
