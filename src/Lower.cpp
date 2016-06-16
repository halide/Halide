#include <iostream>
#include <set>
#include <sstream>
#include <algorithm>

#include "Lower.h"

#include "AddImageChecks.h"
#include "AddParameterChecks.h"
#include "AllocationBoundsInference.h"
#include "Bounds.h"
#include "BoundsInference.h"
#include "CSE.h"
#include "Debug.h"
#include "DebugToFile.h"
#include "DeepCopy.h"
#include "Deinterleave.h"
#include "EarlyFree.h"
#include "FindCalls.h"
#include "Function.h"
#include "FuseGPUThreadLoops.h"
#include "HexagonOffload.h"
#include "InjectHostDevBufferCopies.h"
#include "InjectImageIntrinsics.h"
#include "InjectOpenGLIntrinsics.h"
#include "Inline.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "LoopCarry.h"
#include "Memoization.h"
#include "PartitionLoops.h"
#include "Profiling.h"
#include "Qualify.h"
#include "RealizationOrder.h"
#include "RemoveDeadAllocations.h"
#include "RemoveTrivialForLoops.h"
#include "RemoveUndef.h"
#include "ScheduleFunctions.h"
#include "SelectGPUAPI.h"
#include "SkipStages.h"
#include "SlidingWindow.h"
#include "Simplify.h"
#include "SimplifySpecializations.h"
#include "StorageFlattening.h"
#include "StorageFolding.h"
#include "Substitute.h"
#include "Tracing.h"
#include "TrimNoOps.h"
#include "UnifyDuplicateLets.h"
#include "UniquifyVariableNames.h"
#include "UnrollLoops.h"
#include "VaryingAttributes.h"
#include "VectorizeLoops.h"
#include "WrapCalls.h"

namespace Halide {
namespace Internal {

using std::set;
using std::ostringstream;
using std::string;
using std::vector;
using std::map;

Stmt lower(vector<Function> outputs, const string &pipeline_name, const Target &t, const vector<IRMutator *> &custom_passes) {

    // Compute an environment
    map<string, Function> env;
    for (Function f : outputs) {
        map<string, Function> more_funcs = find_transitive_calls(f);
        env.insert(more_funcs.begin(), more_funcs.end());
    }

    // Create a deep-copy of the entire graph of Funcs.
    std::tie(outputs, env) = deep_copy(outputs, env);

    // Substitute in wrapper Funcs
    env = wrap_func_calls(env);

    // Compute a realization order
    vector<string> order = realization_order(outputs, env);

    // Try to simplify the RHS/LHS of a function definition by propagating its
    // specializations' conditions
    simplify_specializations(env);

    bool any_memoized = false;

    debug(1) << "Creating initial loop nests...\n";
    Stmt s = schedule_functions(outputs, order, env, t, any_memoized);
    debug(2) << "Lowering after creating initial loop nests:\n" << s << '\n';

    if (any_memoized) {
        debug(1) << "Injecting memoization...\n";
        s = inject_memoization(s, env, pipeline_name, outputs);
        debug(2) << "Lowering after injecting memoization:\n" << s << '\n';
    } else {
        debug(1) << "Skipping injecting memoization...\n";
    }

    debug(1) << "Injecting tracing...\n";
    s = inject_tracing(s, pipeline_name, env, outputs);
    debug(2) << "Lowering after injecting tracing:\n" << s << '\n';

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
    s = add_image_checks(s, outputs, t, order, env, func_bounds);
    debug(2) << "Lowering after injecting image checks:\n" << s << '\n';

    // This pass injects nested definitions of variable names, so we
    // can't simplify statements from here until we fix them up. (We
    // can still simplify Exprs).
    debug(1) << "Performing computation bounds inference...\n";
    s = bounds_inference(s, outputs, order, env, func_bounds);
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
    s = storage_folding(s, env);
    debug(2) << "Lowering after storage folding:\n" << s << '\n';

    debug(1) << "Injecting debug_to_file calls...\n";
    s = debug_to_file(s, outputs, env);
    debug(2) << "Lowering after injecting debug_to_file calls:\n" << s << '\n';

    debug(1) << "Simplifying...\n"; // without removing dead lets, because storage flattening needs the strides
    s = simplify(s, false);
    debug(2) << "Lowering after first simplification:\n" << s << "\n\n";

    debug(1) << "Dynamically skipping stages...\n";
    s = skip_stages(s, order);
    debug(2) << "Lowering after dynamically skipping stages:\n" << s << "\n\n";

    if (t.has_feature(Target::OpenGL) || t.has_feature(Target::Renderscript)) {
        debug(1) << "Injecting image intrinsics...\n";
        s = inject_image_intrinsics(s, env);
        debug(2) << "Lowering after image intrinsics:\n" << s << "\n\n";
    }

    debug(1) << "Performing storage flattening...\n";
    s = storage_flattening(s, outputs, env, t);
    debug(2) << "Lowering after storage flattening:\n" << s << "\n\n";

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
        t.has_feature(Target::Renderscript) ||
        (t.arch != Target::Hexagon && (t.features_any_of({Target::HVX_64, Target::HVX_128})))) {
        debug(1) << "Selecting a GPU API for GPU loops...\n";
        s = select_gpu_api(s, t);
        debug(2) << "Lowering after selecting a GPU API:\n" << s << "\n\n";

        debug(1) << "Injecting host <-> dev buffer copies...\n";
        s = inject_host_dev_buffer_copies(s, t);
        debug(2) << "Lowering after injecting host <-> dev buffer copies:\n" << s << "\n\n";
    }

    if (t.has_feature(Target::OpenGL)) {
        debug(1) << "Injecting OpenGL texture intrinsics...\n";
        s = inject_opengl_intrinsics(s);
        debug(2) << "Lowering after OpenGL intrinsics:\n" << s << "\n\n";
    }

    if (t.has_gpu_feature() ||
        t.has_feature(Target::OpenGLCompute) ||
        t.has_feature(Target::Renderscript)) {
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

    debug(1) << "Trimming loops to the region over which they do something...\n";
    s = trim_no_ops(s);
    debug(2) << "Lowering after loop trimming:\n" << s << "\n\n";

    debug(1) << "Injecting early frees...\n";
    s = inject_early_frees(s);
    debug(2) << "Lowering after injecting early frees:\n" << s << "\n\n";

    if (t.has_feature(Target::Profile)) {
        debug(1) << "Injecting profiling...\n";
        s = inject_profiling(s, pipeline_name);
        debug(2) << "Lowering after injecting profiling:\n" << s << '\n';
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

    s = remove_dead_allocations(s);
    s = remove_trivial_for_loops(s);
    s = simplify(s);
    debug(1) << "Lowering after final simplification:\n" << s << "\n\n";

    debug(1) << "Splitting off Hexagon offload...\n";
    s = inject_hexagon_rpc(s, t);
    debug(2) << "Lowering after splitting off Hexagon offload:\n" << s << '\n';

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
