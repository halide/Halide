#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

#include "Lower.h"

#include "AddAtomicMutex.h"
#include "AddImageChecks.h"
#include "AddParameterChecks.h"
#include "AddSplitFactorChecks.h"
#include "AllocationBoundsInference.h"
#include "AsyncProducers.h"
#include "BoundConstantExtentLoops.h"
#include "BoundSmallAllocations.h"
#include "Bounds.h"
#include "BoundsInference.h"
#include "CSE.h"
#include "CanonicalizeGPUVars.h"
#include "ClampUnsafeAccesses.h"
#include "CompilerLogger.h"
#include "CompilerProfiling.h"
#include "Debug.h"
#include "DebugArguments.h"
#include "DebugToFile.h"
#include "Deinterleave.h"
#include "EarlyFree.h"
#include "ExtractTileOperations.h"
#include "FindCalls.h"
#include "FindIntrinsics.h"
#include "FlattenNestedRamps.h"
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
#include "Inline.h"
#include "LICM.h"
#include "LoopCarry.h"
#include "LowerParallelTasks.h"
#include "LowerWarpShuffles.h"
#include "Memoization.h"
#include "OffloadGPULoops.h"
#include "PartitionLoops.h"
#include "Prefetch.h"
#include "Profiling.h"
#include "PurifyIndexMath.h"
#include "Qualify.h"
#include "RealizationOrder.h"
#include "RebaseLoopsToZero.h"
#include "RemoveDeadAllocations.h"
#include "RemoveExternLoops.h"
#include "RemoveUndef.h"
#include "ScheduleFunctions.h"
#include "SelectGPUAPI.h"
#include "Simplify.h"
#include "SimplifyCorrelatedDifferences.h"
#include "SimplifySpecializations.h"
#include "SkipStages.h"
#include "SlidingWindow.h"
#include "SplitTuples.h"
#include "StageStridedLoads.h"
#include "StorageFlattening.h"
#include "StorageFolding.h"
#include "StrictifyFloat.h"
#include "StripAsserts.h"
#include "Substitute.h"
#include "TargetQueryOps.h"
#include "Tracing.h"
#include "TrimNoOps.h"
#include "UnifyDuplicateLets.h"
#include "UniquifyVariableNames.h"
#include "UnpackBuffers.h"
#include "UnrollLoops.h"
#include "UnsafePromises.h"
#include "VectorizeLoops.h"
#include "WrapCalls.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;

namespace {

class LoweringLogger {
    Stmt last_written;
    std::chrono::time_point<std::chrono::high_resolution_clock> last_time;
    const char *last_msg;

    std::vector<std::pair<double, std::string>> timings;
    bool time_lowering_passes = false;

public:
    LoweringLogger() {
        static bool should_time = !get_env_variable("HL_TIME_LOWERING_PASSES").empty();
        time_lowering_passes = should_time;
    }

    void begin(const char *msg) {
        debug(1) << "Lowering pass: " << msg << "...\n";
        Profiling::generic_zone_begin(msg);
        last_time = std::chrono::high_resolution_clock::now();
        last_msg = msg;
    }

    void begin(const char *msg, int data) {
        debug(1) << "Lowering pass: " << msg << " " << data << "...\n";
        Profiling::generic_zone_begin(msg, data);
        last_time = std::chrono::high_resolution_clock::now();
        last_msg = msg;
    }

    void end() {
        Profiling::generic_zone_end(last_msg);
        auto t = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = t - last_time;
        timings.emplace_back(diff.count() * 1000, last_msg);
    }

    void end(const Stmt &s) {
        Profiling::generic_zone_end(last_msg);
        auto t = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = t - last_time;
        timings.emplace_back(diff.count() * 1000, last_msg);
        if (!s.same_as(last_written)) {
            debug(2) << "Lowering after " << last_msg << "\n"
                     << s << "\n";
            last_written = s;
            last_time = t;
        } else {
            debug(2) << "Lowering after " << last_msg << " (unchanged)\n\n";
            last_time = t;
        }
    }

    ~LoweringLogger() {
        if (time_lowering_passes) {
            double total = 0.0;
            debug(0) << "Lowering pass runtimes:\n";
            std::sort(timings.begin(), timings.end());
            for (const auto &p : timings) {
                total += p.first;
                debug(0) << std::setw(10) << std::fixed << std::setprecision(3) << p.first << " ms : "
                         << p.second << "\n";
            }
            debug(0) << std::setw(10) << std::fixed << std::setprecision(3) << total << " ms in total\n";
        }
    }
};

void lower_impl(const vector<Function> &output_funcs,
                const string &pipeline_name,
                const Target &t,
                const vector<Argument> &args,
                const LinkageType linkage_type,
                const vector<Stmt> &requirements,
                bool trace_pipeline,
                const vector<IRMutator *> &custom_passes,
                Module &result_module) {
    ZoneScoped;
    auto time_start = std::chrono::high_resolution_clock::now();

    size_t initial_lowered_function_count = result_module.functions().size();

    // Create a deep-copy of the entire graph of Funcs.
    auto [outputs, env] = deep_copy(output_funcs, build_environment(output_funcs));

    lower_target_query_ops(env, t);

    bool any_strict_float = strictify_float(env, t);
    result_module.set_any_strict_float(any_strict_float);

    // Finalize all the LoopLevels
    for (auto &iter : env) {
        iter.second.lock_loop_levels();
    }

    // Substitute in wrapper Funcs
    env = wrap_func_calls(env);

    // Compute a realization order and determine group of functions which loops
    // are to be fused together
    auto [order, fused_groups] = realization_order(outputs, env);

    // Try to simplify the RHS/LHS of a function definition by propagating its
    // specializations' conditions
    simplify_specializations(env);

    LoweringLogger log;

    log.begin("Creating initial loop nests");
    bool any_memoized = false;
    Stmt s = schedule_functions(outputs, fused_groups, env, t, any_memoized);
    log.end(s);

    if (any_memoized) {
        log.begin("Injecting memoization");
        s = inject_memoization(s, env, pipeline_name, outputs);
        log.end(s);
    } else {
        debug(1) << "Skipping injecting memoization...\n";
    }
    log.begin("Injecting tracing");
    s = inject_tracing(s, pipeline_name, trace_pipeline, env, outputs, t);
    log.end(s);

    log.begin("Adding checks for parameters");
    s = add_parameter_checks(requirements, s, t);
    log.end(s);

    // Compute the maximum and minimum possible value of each
    // function. Used in later bounds inference passes.
    log.begin("Computing bounds of each function's value");
    FuncValueBounds func_bounds = compute_function_value_bounds(order, env);
    log.end();

    // Clamp unsafe instances where a Func f accesses a Func g using
    // an index which depends on a third Func h.
    log.begin("Clamping unsafe data-dependent accesses");
    s = clamp_unsafe_accesses(s, env, func_bounds);
    log.end(s);

    // This pass injects nested definitions of variable names, so we
    // can't simplify statements from here until we fix them up. (We
    // can still simplify Exprs).
    log.begin("Bounds inference");
    s = bounds_inference(s, outputs, order, fused_groups, env, func_bounds, t);
    log.end(s);

    log.begin("Asserting that all split factors are positive");
    s = add_split_factor_checks(s, env);
    log.end(s);

    log.begin("Removing extern loops");
    s = remove_extern_loops(s);
    log.end(s);

    log.begin("Sliding window optimization");
    s = sliding_window(s, env);
    log.end(s);

    // This uniquifies the variable names, so we're good to simplify
    // after this point. This lets later passes assume syntactic
    // equivalence means semantic equivalence.
    log.begin("Uniquifying variable names");
    s = uniquify_variable_names(s);
    log.end(s);

    log.begin("Simplifying");
    s = simplify(s);
    log.end(s);

    log.begin("Simplifying correlated differences");
    s = simplify_correlated_differences(s);
    log.end(s);

    log.begin("Allocation bounds inference");
    s = allocation_bounds_inference(s, env, func_bounds);
    log.end(s);

    bool will_inject_host_copies =
        (t.has_gpu_feature() ||
         t.has_feature(Target::HexagonDma) ||
         (t.arch != Target::Hexagon && (t.has_feature(Target::HVX))));

    log.begin("Adding checks for images");
    s = add_image_checks(s, outputs, t, order, env, func_bounds, will_inject_host_copies);
    log.end(s);

    log.begin("Removing code that depends on undef values");
    s = remove_undef(s);
    log.end(s);

    log.begin("Performing storage folding optimization");
    s = storage_folding(s, env);
    log.end(s);

    log.begin("Injecting debug_to_file calls");
    s = debug_to_file(s, outputs, env);
    log.end(s);

    log.begin("Injecting prefetches");
    s = inject_prefetch(s, env);
    log.end(s);

    log.begin("Discarding safe promises");
    s = lower_safe_promises(s);
    log.end(s);

    log.begin("Dynamically skipping stages");
    s = skip_stages(s, outputs, fused_groups, env);
    log.end(s);

    log.begin("Forking asynchronous producers");
    s = fork_async_producers(s, env);
    log.end(s);

    log.begin("Destructuring tuple-valued realizations");
    s = split_tuples(s, env);
    log.end(s);

    if (t.has_gpu_feature()) {
        log.begin("Canonicalizing GPU var names");
        s = canonicalize_gpu_vars(s);
        log.end(s);
    }

    log.begin("Bounding small realizations");
    s = simplify_correlated_differences(s);
    s = bound_small_allocations(s);
    log.end(s);

    log.begin("Performing storage flattening");
    s = storage_flattening(s, outputs, env, t);
    log.end(s);

    log.begin("Adding atomic mutex allocation");
    s = add_atomic_mutex(s, outputs);
    log.end(s);

    log.begin("Unpacking buffer arguments");
    s = unpack_buffers(s);
    log.end(s);

    if (any_memoized) {
        log.begin("Rewriting memoized allocations");
        s = rewrite_memoized_allocations(s, env);
        log.end(s);
    } else {
        debug(1) << "Skipping rewriting memoized allocations\n";
    }

    if (will_inject_host_copies) {
        log.begin("Selecting a GPU API for GPU loops");
        s = select_gpu_api(s, t);
        log.end(s);

        log.begin("Injecting host <-> dev buffer copies");
        s = inject_host_dev_buffer_copies(s, t);
        log.end(s);

        log.begin("Selecting a GPU API for extern stages");
        s = select_gpu_api(s, t);
        log.end(s);
    } else {
        log.begin("Injecting host-dirty marking");
        s = inject_host_dev_buffer_copies(s, t);
        log.end(s);
    }

    log.begin("Simplifying");
    s = simplify(s);
    s = unify_duplicate_lets(s);
    log.end(s);

    log.begin("Reduce prefetch dimension");
    s = reduce_prefetch_dimension(s, t);
    log.end(s);

    log.begin("Simplifying correlated differences");
    s = simplify_correlated_differences(s);
    log.end(s);

    log.begin("Bounding constant extent loops");
    s = bound_constant_extent_loops(s);
    log.end(s);

    log.begin("Unrolling");
    s = unroll_loops(s);
    log.end(s);

    log.begin("Vectorizing");
    s = vectorize_loops(s, env);
    s = simplify(s);
    log.end(s);

    if (t.has_gpu_feature() ||
        t.has_feature(Target::Vulkan)) {
        log.begin("Injecting per-block gpu synchronization");
        s = fuse_gpu_thread_loops(s);
        log.end(s);
    }

    log.begin("Detecting vector interleavings");
    s = rewrite_interleavings(s);
    s = simplify(s);
    log.end(s);

    log.begin("Partitioning loops to simplify boundary conditions");
    s = partition_loops(s);
    s = simplify(s);
    log.end(s);

    log.begin("Staging strided loads");
    s = stage_strided_loads(s);
    log.end(s);

    log.begin("Trimming loops to the region over which they do something");
    s = trim_no_ops(s);
    log.end(s);

    log.begin("Rebasing loops to zero");
    s = rebase_loops_to_zero(s);
    log.end(s);

    log.begin("Hoisting loop invariant if statements");
    s = hoist_loop_invariant_if_statements(s);
    log.end(s);

    log.begin("Injecting early frees");
    s = inject_early_frees(s);
    log.end(s);

    if (t.has_feature(Target::FuzzFloatStores)) {
        log.begin("Fuzzing floating point stores");
        s = fuzz_float_stores(s);
        log.end(s);
    }

    log.begin("Simplifying correlated differences");
    s = simplify_correlated_differences(s);
    log.end(s);

    log.begin("Bounding small allocations");
    s = bound_small_allocations(s);
    log.end(s);

    if (t.has_feature(Target::Profile) || t.has_feature(Target::ProfileByTimer)) {
        log.begin("Injecting profiling");
        s = inject_profiling(s, pipeline_name, env);
        log.end(s);
    }

    if (t.has_feature(Target::CUDA)) {
        log.begin("Injecting warp shuffles");
        s = lower_warp_shuffles(s, t);
        log.end(s);
    }

    log.begin("Simplifying");
    s = common_subexpression_elimination(s);
    log.end();

    log.begin("Lowering unsafe promises");
    s = lower_unsafe_promises(s, t);
    log.end(s);

    if (t.has_feature(Target::AVX512_SapphireRapids)) {
        log.begin("Extracting tile operations");
        s = extract_tile_operations(s);
        log.end(s);
    }

    log.begin("Flattening nested ramps");
    s = flatten_nested_ramps(s);
    log.end(s);

    log.begin("Removing dead allocations and moving loop invariant code");
    s = remove_dead_allocations(s);
    s = simplify(s);
    s = hoist_loop_invariant_values(s);
    s = hoist_loop_invariant_if_statements(s);
    log.end(s);

    log.begin("Finding intrinsics");
    // Must be run after the last simplification, because it turns
    // divisions into shifts, which the simplifier reverses.
    s = find_intrinsics(s);
    log.end(s);

    log.begin("Hoisting prefetches");
    s = hoist_prefetches(s);
    log.end(s);

    if (t.has_feature(Target::NoAsserts)) {
        log.begin("Stripping asserts");
        s = strip_asserts(s);
        log.end(s);
    }

    debug(1) << "Lowering after final simplification:\n"
             << s << "\n\n";

    if (!custom_passes.empty()) {
        for (size_t i = 0; i < custom_passes.size(); i++) {
            log.begin("Custom lowering pass", i);
            s = custom_passes[i]->operator()(s);
            log.end(s);
        }
    }

    // Make a copy of the Stmt code, before we lower anything to less human-readable code.
    result_module.set_conceptual_code_stmt(s);

    if (t.arch != Target::Hexagon && t.has_feature(Target::HVX)) {
        log.begin("Splitting off Hexagon offload");
        s = inject_hexagon_rpc(s, t, result_module);
        log.end(s);
    } else {
        debug(1) << "Skipping Hexagon offload...\n";
    }

    if (t.has_gpu_feature()) {
        log.begin("Offloading GPU loops");
        s = inject_gpu_offload(s, t, any_strict_float);
        log.end(s);
    } else {
        debug(1) << "Skipping GPU offload...\n";
    }

    // TODO: This needs to happen before lowering parallel tasks, because global
    // images used inside parallel loops are rewritten from loads from images to
    // loads from closure parameters. Closure parameters are missing the Buffer<>
    // object, which needs to be found by infer_arguments here. Running
    // infer_arguments prior to lower_parallel_tasks is a hacky solution to this
    // problem. It would be better if closures could directly reference globals
    // so they don't add overhead to the closure.
    vector<InferredArgument> inferred_args = infer_arguments(s, outputs);

    std::vector<LoweredFunc> closure_implementations;
    log.begin("Lowering Parallel Tasks");
    s = lower_parallel_tasks(s, closure_implementations, pipeline_name, t);
    // Process any LoweredFunctions added by other passes. In practice, this
    // will likely not work well enough due to ordering issues with
    // closure generating passes and instead all such passes will need to
    // be done at once.
    for (size_t i = initial_lowered_function_count; i < result_module.functions().size(); i++) {
        // Note that lower_parallel_tasks() appends to the end of closure_implementations
        result_module.functions()[i].body =
            lower_parallel_tasks(result_module.functions()[i].body, closure_implementations,
                                 result_module.functions()[i].name, t);
    }
    for (auto &lowered_func : closure_implementations) {
        result_module.append(lowered_func);
    }
    log.end(s);

    vector<Argument> public_args = args;
    for (const auto &out : outputs) {
        for (const Parameter &buf : out.output_buffers()) {
            public_args.emplace_back(buf.name(),
                                     Argument::OutputBuffer,
                                     buf.type(), buf.dimensions(), buf.get_argument_estimates());
        }
    }

    for (const InferredArgument &arg : inferred_args) {
        if (arg.param.defined() && arg.param.name() == "__user_context") {
            // The user context is always in the inferred args, but is
            // not required to be in the args list.
            continue;
        }

        internal_assert(arg.arg.is_input()) << "Expected only input Arguments here";

        bool found = false;
        for (const Argument &a : args) {
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
            for (const auto &arg : args) {
                err << arg.name << " ";
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
    s = mutate_with(s, [&](auto *self, const Call *c) {
        Expr expr = self->visit_base(c);
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
    });

    LoweredFunc main_func(pipeline_name, public_args, s, linkage_type);

    // If we're in debug mode, add code that prints the args.
    if (t.has_feature(Target::Debug)) {
        debug_arguments(&main_func, t);
    }

    result_module.append(main_func);

    auto *logger = get_compiler_logger();
    if (logger) {
        auto time_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = time_end - time_start;
        logger->record_compilation_time(CompilerLogger::Phase::HalideLowering, diff.count());
    }
}

}  // namespace

Module lower(const vector<Function> &output_funcs,
             const string &pipeline_name,
             const Target &t,
             const vector<Argument> &args,
             const LinkageType linkage_type,
             const vector<Stmt> &requirements,
             bool trace_pipeline,
             const vector<IRMutator *> &custom_passes) {
    Module result_module{strip_namespaces(pipeline_name), t};
    run_with_large_stack([&]() {
        lower_impl(output_funcs, pipeline_name, t, args, linkage_type, requirements, trace_pipeline, custom_passes, result_module);
    });
    return result_module;
}

Stmt lower_main_stmt(const std::vector<Function> &output_funcs,
                     const std::string &pipeline_name,
                     const Target &t,
                     const std::vector<Stmt> &requirements,
                     bool trace_pipeline,
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

    Module module = lower(output_funcs, pipeline_name, t, args, LinkageType::External, requirements, trace_pipeline, custom_passes);

    return module.functions().front().body;
}

}  // namespace Internal
}  // namespace Halide
