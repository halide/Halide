#include "Errors.h"
#include "Halide.h"
#include "HalidePlugin.h"
#include "ParamParser.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

namespace {

struct GradientAutoschedulerParams {
    /** Maximum level of parallelism available. */
    int parallelism = 16;
};

std::map<std::string, Box> inference_bounds(const std::vector<Function> &functions,
                                            const std::vector<Box> &output_bounds) {
    std::vector<Func> funcs;
    funcs.reserve(functions.size());
    for (const auto &f : functions) {
        funcs.emplace_back(f);
    }
    return inference_bounds(funcs, output_bounds);
}

template<typename T>
std::vector<int> sort_indices(const std::vector<T> &v) {
    std::vector<int> idx(v.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(),
              [&v](int i1, int i2) { return v[i1] < v[i2]; });
    return idx;
}

std::vector<int> get_int_bounds(const Box &bounds) {
    std::vector<int> int_bounds;
    int_bounds.reserve(bounds.size());
    for (int i = 0; i < (int)bounds.size(); i++) {
        Interval interval = bounds[i];
        Expr extent = simplify(interval.max - interval.min + 1);
        extent = simplify(substitute_var_estimates(extent));
        const int64_t *extent_int = as_const_int(extent);
        user_assert(extent_int != nullptr)
            << "extent:" << extent << " is not constant.\n";
        int_bounds.push_back(*extent_int);
    }
    return int_bounds;
}

std::vector<int> get_rvar_bounds(const std::vector<ReductionVariable> &rvars) {
    std::vector<int> rvar_bounds;
    rvar_bounds.reserve(rvars.size());
    for (const auto &rvar : rvars) {
        Expr extent = simplify(substitute_var_estimates(rvar.extent));
        const int64_t *extent_int = as_const_int(extent);
        user_assert(extent_int != nullptr)
            << "extent:" << extent << " is not constant.\n";
        rvar_bounds.push_back(*extent_int);
    }
    return rvar_bounds;
}

void reorder_storage(Func func,
                     const std::vector<Var> &all_vars,
                     std::ostringstream &schedule_source) {
    func.reorder_storage(all_vars);
    schedule_source << "    .reorder_storage(";
    for (int i = 0; i < (int)all_vars.size(); i++) {
        schedule_source << all_vars[i].name();
        if (i != (int)all_vars.size() - 1) {
            schedule_source << ",";
        }
    }
    schedule_source << ")\n";
}

void reorder_storage(const Stage &stage,
                     const std::vector<Var> &all_vars,
                     std::ostringstream &schedule_source) {
    internal_error << "Can't reorder storage of a stage.";
}

int natural_vector_size(const Target &target, const Type &t) {
    const int data_size = t.bytes();
    if (target.os == Target::OSUnknown || target.arch == Target::ArchUnknown || target.bits != 0) {
        return 32 / data_size;
    } else {
        return target.natural_vector_size(t);
    }
}

template<typename FuncOrStage>
void parallelize_vars_and_rvars_gpu(
    const GradientAutoschedulerParams &params,
    FuncOrStage func_or_stage,
    bool is_pure_def,
    const std::vector<Var> &vars,
    const std::vector<int> &var_bounds,
    const std::vector<RVar> &rvars,
    const std::vector<int> &rvar_bounds,
    TailStrategy tail,
    std::ostringstream &schedule_source) {
    // Find the first variable that has bounds larger or equal than 64,
    // this is our GPU thread.
    // We use 64 since it's twice the warp size, so this launches enough
    // GPU threads for a block to be work efficient.
    constexpr int warp_size = 32;
    constexpr int split_size = 2 * warp_size;
    std::vector<Var> gpu_blocks;
    std::string gpu_threads;
    int gpu_thread_dim = -1;
    for (int i = 0; i < (int)vars.size(); i++) {
        if (gpu_threads.empty() && var_bounds[i] >= split_size) {
            gpu_thread_dim = i;
            Var outer, inner;
            func_or_stage.split(vars[i],
                                outer,
                                inner,
                                split_size,
                                tail);
            schedule_source << "    .split("
                            << vars[i].name() << ","
                            << outer.name() << ","
                            << inner.name() << ","
                            << split_size << ","
                            << tail << ")\n";
            gpu_blocks.push_back(outer);
            gpu_threads = inner.name();
        } else {
            gpu_blocks.push_back(vars[i]);
        }
    }

    std::vector<RVar> serial_rvars;
    std::vector<RVar> r_gpu_blocks;
    std::string r_gpu_threads;
    if (gpu_threads.empty()) {
        // If we can't find any GPU threads, parallelize RVars to find more parallelism
        for (int i = 0; i < (int)rvars.size(); i++) {
            if (!r_gpu_threads.empty() && rvar_bounds[i] > split_size) {
                RVar outer, inner;
                func_or_stage.split(rvars[i],
                                    outer,
                                    inner,
                                    split_size,
                                    tail);
                schedule_source << "    .split("
                                << rvars[i].name() << ","
                                << outer.name() << ","
                                << inner.name() << ","
                                << split_size << ","
                                << tail << ")\n";
                r_gpu_blocks.push_back(outer);
                r_gpu_threads = inner.name();
            } else {
                r_gpu_blocks.push_back(rvars[i]);
            }
        }
    } else {
        serial_rvars = rvars;
    }

    if (gpu_threads.empty() && r_gpu_threads.empty()) {
        // If we didn't assign any GPU threads in the previous
        // process, use the largest loop as the GPU thread.
        int loop_size = 0;
        int largest_loop_id = -1;
        for (int i = 0; i < (int)vars.size(); i++) {
            if (var_bounds[i] > loop_size) {
                loop_size = var_bounds[i];
                largest_loop_id = i;
            }
        }
        for (int i = 0; i < (int)rvars.size(); i++) {
            if (rvar_bounds[i] > loop_size) {
                loop_size = rvar_bounds[i];
                largest_loop_id = i + (int)vars.size();
            }
        }
        if (largest_loop_id != -1) {
            if (largest_loop_id < (int)vars.size()) {
                // The largest loop is a pure variable
                const Var &v = vars[largest_loop_id];
                Var inner;
                func_or_stage.split(v,
                                    v,
                                    inner,
                                    warp_size,
                                    TailStrategy::GuardWithIf);
                schedule_source << "    .split("
                                << v.name() << ","
                                << v.name() << ","
                                << inner.name() << ","
                                << warp_size << ","
                                << TailStrategy::GuardWithIf << ")\n";
                gpu_threads = inner.name();
            } else {
                // The largest loop is a reduction variable
                const RVar &v = rvars[largest_loop_id - (int)vars.size()];
                RVar inner;
                func_or_stage.split(v,
                                    v,
                                    inner,
                                    warp_size,
                                    TailStrategy::GuardWithIf);
                schedule_source << "    .split("
                                << v.name() << ","
                                << v.name() << ","
                                << inner.name() << ","
                                << warp_size << ","
                                << TailStrategy::GuardWithIf << ")\n";
                r_gpu_threads = inner.name();
            }
        }
    }
    // Fuse all gpu blocks into a single variable
    std::string fused_var;
    if (!gpu_blocks.empty()) {
        fused_var = gpu_blocks[0].name();
        // inner to outer
        for (int i = 1; i < (int)gpu_blocks.size(); i++) {
            func_or_stage.fuse(Var(fused_var), gpu_blocks[i], Var(fused_var));
            schedule_source << "    .fuse("
                            << fused_var << ","
                            << gpu_blocks[i].name() << ","
                            << fused_var << ")\n";
        }
    }
    std::string fused_rvar;
    if (!r_gpu_blocks.empty()) {
        fused_rvar = r_gpu_blocks[0].name();
        // inner to outer
        for (int i = 1; i < (int)r_gpu_blocks.size(); i++) {
            func_or_stage.fuse(RVar(fused_rvar), r_gpu_blocks[i], RVar(fused_rvar));
            schedule_source << "    .fuse("
                            << fused_rvar << ","
                            << r_gpu_blocks[i].name() << ","
                            << fused_rvar << ")\n";
        }
    }
    // CUDA places rather strict restriction on the second dimension of the GPU blocks (usually 65536),
    // so we want to split it if it is too large
    int rdomain_size = 1;
    for (int b : rvar_bounds) {
        rdomain_size *= b;
    }
    std::string fused_rvar2;
    // CUDA supports up to 65536 blocks in the second and third dimensions
    constexpr int cuda_gpu_block_split = 65536;
    if (rdomain_size >= cuda_gpu_block_split) {
        RVar r;
        fused_rvar2 = r.name();
        func_or_stage.split(RVar(fused_rvar), RVar(fused_rvar), RVar(fused_rvar2), int(std::sqrt(double(rdomain_size))));
    }

    // Reorder: the order is rvars -> gpu_threads -> gpu_blocks
    std::vector<VarOrRVar> all_vars;
    all_vars.reserve(serial_rvars.size() + 4);
    for (const RVar &v : serial_rvars) {
        all_vars.emplace_back(v);
    }
    if (!r_gpu_threads.empty()) {
        all_vars.emplace_back(RVar(r_gpu_threads));
    }
    if (!gpu_threads.empty()) {
        all_vars.emplace_back(Var(gpu_threads));
    }
    if (!fused_var.empty()) {
        all_vars.emplace_back(Var(fused_var));
    }
    if (!fused_rvar.empty()) {
        all_vars.emplace_back(RVar(fused_rvar));
    }
    if (!fused_rvar2.empty()) {
        all_vars.emplace_back(RVar(fused_rvar2));
    }
    // Only reorder if there's more than one variables.
    if (all_vars.size() > 1) {
        func_or_stage.reorder(all_vars);
        schedule_source << "    .reorder(";
        for (int i = 0; i < (int)all_vars.size(); i++) {
            schedule_source << all_vars[i].name();
            if (i != (int)all_vars.size() - 1) {
                schedule_source << ",";
            }
        }
        schedule_source << ")\n";
        if (is_pure_def) {
            if (gpu_thread_dim > 0) {
                std::vector<Var> reordered_vars = vars;
                std::swap(reordered_vars[0], reordered_vars[gpu_thread_dim]);
                reorder_storage(func_or_stage, reordered_vars, schedule_source);
            }
        }
    }

    if (!gpu_blocks.empty() || !r_gpu_blocks.empty()) {
        // Assign outer loops to GPU blocks
        if (!fused_var.empty()) {
            func_or_stage.gpu_blocks(Var(fused_var));
            schedule_source << "    .gpu_blocks(" << fused_var << ")\n";
        }
        if (!fused_rvar.empty()) {
            func_or_stage.atomic()
                .gpu_blocks(RVar(fused_rvar));
            schedule_source << "    .atomic()\n";
            schedule_source << "    .gpu_blocks(" << fused_rvar << ")\n";
        }
        if (!fused_rvar2.empty()) {
            internal_assert(!fused_rvar.empty());
            func_or_stage.gpu_blocks(RVar(fused_rvar2));
            schedule_source << "    .gpu_blocks(" << fused_rvar2 << ")\n";
        }
        // Assign inner loops to GPU threads
        if (!gpu_threads.empty()) {
            func_or_stage.gpu_threads(Var(gpu_threads));
            schedule_source << "    .gpu_threads(" << gpu_threads << ")\n";
        }
        if (!r_gpu_threads.empty()) {
            func_or_stage.gpu_threads(RVar(r_gpu_threads));
            schedule_source << "    .r_gpu_threads(" << r_gpu_threads << ")\n";
        }
    } else {
        // Not enough parallelism, use a single GPU thread
        func_or_stage.gpu_single_thread();
        schedule_source << "    .gpu_single_thread()\n";
    }
}

template<typename FuncOrStage>
void parallelize_vars_and_rvars_cpu(
    const GradientAutoschedulerParams &params,
    FuncOrStage func_or_stage,
    int natural_vector_size,
    bool is_pure_def,
    const std::vector<Var> &vars,
    const std::vector<int> &var_bounds,
    const std::vector<RVar> &rvars,
    const std::vector<int> &rvar_bounds,
    TailStrategy tail,
    std::ostringstream &schedule_source) {
    // Find the first variable that has bounds larger or equal than natural_vector_size,
    // this is our vectorized dimension
    const int split_size = natural_vector_size;
    std::vector<Var> parallel_vars;
    std::string vectorized_var;
    int num_threads_var = 1;
    int vectorized_dim = -1;
    for (int i = 0; i < (int)vars.size(); i++) {
        if (vectorized_var.empty() && var_bounds[i] >= split_size) {
            vectorized_dim = i;
            Var outer, inner;
            func_or_stage.split(vars[i],
                                outer,
                                inner,
                                split_size,
                                tail);
            schedule_source << "    .split("
                            << vars[i].name() << ","
                            << outer.name() << ","
                            << inner.name() << ","
                            << split_size << ","
                            << tail << ")\n";
            parallel_vars.push_back(outer);
            vectorized_var = inner.name();
            int b = var_bounds[i] / split_size;
            if (var_bounds[i] % split_size == 0) {
                b++;
            }
            num_threads_var *= b;
        } else {
            parallel_vars.push_back(vars[i]);
            num_threads_var *= var_bounds[i];
        }
    }

    // If there's not enough parallelism, find in rvars.
    // Two cases: 1) not enough threads 2) no vectorized dimension
    std::vector<RVar> serial_rvars;
    std::vector<RVar> parallel_rvars;
    std::string vectorized_rvar;
    int num_threads_rvar = 1;
    for (int i = 0; i < (int)rvars.size(); i++) {
        if (vectorized_var.empty() && vectorized_rvar.empty() &&
            rvar_bounds[i] >= split_size) {
            RVar outer, inner;
            func_or_stage.split(rvars[i],
                                outer,
                                inner,
                                split_size,
                                tail);
            schedule_source << "    .split("
                            << rvars[i].name() << ","
                            << outer.name() << ","
                            << inner.name() << ","
                            << split_size << ","
                            << tail << ")\n";
            int b = rvar_bounds[i] / split_size;
            if (rvar_bounds[i] % split_size == 0) {
                b++;
            }
            if (num_threads_var * num_threads_rvar < params.parallelism) {
                parallel_rvars.push_back(outer);
                num_threads_rvar *= b;
            } else {
                serial_rvars.push_back(outer);
            }
            vectorized_rvar = inner.name();
        } else {
            if (num_threads_var * num_threads_rvar < params.parallelism) {
                num_threads_rvar *= rvar_bounds[i];
                parallel_rvars.push_back(rvars[i]);
            } else {
                serial_rvars.push_back(rvars[i]);
            }
        }
    }

    // Fuse all parallel vars into a single variable for parallelism
    std::string fused_var;
    if (!parallel_vars.empty()) {
        fused_var = parallel_vars[0].name();
        // inner to outer
        for (int i = 1; i < (int)parallel_vars.size(); i++) {
            func_or_stage.fuse(Var(fused_var), parallel_vars[i], Var(fused_var));
            schedule_source << "    .fuse("
                            << fused_var << ","
                            << parallel_vars[i].name() << ","
                            << fused_var << ")\n";
        }
    }

    // Fuse all parallel rvars into a single variable for parallelism
    std::string fused_rvar;
    if (!parallel_rvars.empty()) {
        fused_rvar = parallel_rvars[0].name();
        // inner to outer
        for (int i = 1; i < (int)parallel_rvars.size(); i++) {
            func_or_stage.fuse(RVar(fused_rvar), parallel_rvars[i], RVar(fused_rvar));
            schedule_source << "    .fuse("
                            << fused_rvar << ","
                            << parallel_rvars[i].name() << ","
                            << fused_rvar << ")\n";
        }
    }

    // Reorder: the order is serial_rvars -> vectorized_rvar/vectorized_var ->
    //                       fused_rvars -> fused_vars
    std::vector<VarOrRVar> all_vars;
    all_vars.reserve(serial_rvars.size() + 4);
    for (const RVar &v : serial_rvars) {
        all_vars.emplace_back(v);
    }
    if (!vectorized_rvar.empty()) {
        all_vars.emplace_back(RVar(vectorized_rvar));
    }
    if (!vectorized_var.empty()) {
        all_vars.emplace_back(Var(vectorized_var));
    }
    if (!fused_rvar.empty()) {
        all_vars.emplace_back(RVar(fused_rvar));
    }
    if (!fused_var.empty()) {
        all_vars.emplace_back(Var(fused_var));
    }
    // Only reorder if there's more than one variables.
    if (all_vars.size() > 1) {
        func_or_stage.reorder(all_vars);
        schedule_source << "    .reorder(";
        for (int i = 0; i < (int)all_vars.size(); i++) {
            schedule_source << all_vars[i].name();
            if (i != (int)all_vars.size() - 1) {
                schedule_source << ",";
            }
        }
        schedule_source << ")\n";
        if (is_pure_def) {
            if (vectorized_dim > 0) {
                std::vector<Var> reordered_vars = vars;
                std::swap(reordered_vars[0], reordered_vars[vectorized_dim]);
                reorder_storage(func_or_stage, reordered_vars, schedule_source);
            }
        }
    }

    if (!fused_var.empty()) {
        // Parallelize vars
        if (num_threads_var > params.parallelism * 8) {
            func_or_stage.parallel(Var(fused_var),
                                   num_threads_var / (params.parallelism * 8),
                                   tail);
            schedule_source << "    .parallel("
                            << fused_var << ","
                            << num_threads_var / (params.parallelism * 8) << ","
                            << tail << ")\n";
        } else {
            func_or_stage.parallel(Var(fused_var));
            schedule_source << "    .parallel(" << fused_var << ")\n";
        }
    }
    if (!fused_rvar.empty()) {
        // Parallelize rvars
        if (num_threads_rvar > params.parallelism * 8) {
            func_or_stage.atomic()
                .parallel(RVar(fused_rvar),
                          num_threads_rvar / (params.parallelism * 8),
                          tail);
            schedule_source << "    .atomic()\n";
            schedule_source << "    .parallel("
                            << fused_rvar << ","
                            << num_threads_rvar / (params.parallelism * 8) << ","
                            << tail << ")\n";
        } else {
            func_or_stage.atomic()
                .parallel(RVar(fused_rvar));
            schedule_source << "    .atomic()\n";
            schedule_source << "    .parallel("
                            << fused_rvar << ")\n";
        }
    }
    if (!vectorized_var.empty()) {
        func_or_stage.vectorize(Var(vectorized_var));
        schedule_source << "    .vectorize("
                        << vectorized_var << ")\n";
    }
    if (!vectorized_rvar.empty()) {
        func_or_stage.atomic().vectorize(RVar(vectorized_rvar));
        schedule_source << "    .atomic()\n";
        schedule_source << "    .vectorize("
                        << vectorized_rvar << ")\n";
    }
}

template<typename FuncOrStage>
void parallelize_vars_and_rvars(
    const GradientAutoschedulerParams &params,
    FuncOrStage func_or_stage,
    int natural_vector_size,
    bool is_pure_def,
    const std::vector<Var> &vars,
    const std::vector<int> &var_bounds,
    const std::vector<RVar> &rvars,
    const std::vector<int> &rvar_bounds,
    TailStrategy tail,
    bool is_gpu,
    std::ostringstream &schedule_source) {
    if (is_gpu) {
        return parallelize_vars_and_rvars_gpu(
            params,
            func_or_stage,
            is_pure_def,
            vars,
            var_bounds,
            rvars,
            rvar_bounds,
            tail,
            schedule_source);
    } else {
        return parallelize_vars_and_rvars_cpu(
            params,
            func_or_stage,
            natural_vector_size,
            is_pure_def,
            vars,
            var_bounds,
            rvars,
            rvar_bounds,
            tail,
            schedule_source);
    }
}

void apply_schedule(const GradientAutoschedulerParams &params,
                    const Target &target,
                    Func func,
                    int update_id,
                    const std::vector<int> &var_bounds,
                    bool is_gpu,
                    std::ostringstream &schedule_source) {
    if (update_id == -1) {
        func.compute_root();
        schedule_source << func.name() << ".compute_root()\n";
        if (func.dimensions() > 0) {
            parallelize_vars_and_rvars(
                params,
                func,
                natural_vector_size(target, func.values()[0].type()),
                true,  // is_pure_def
                func.args(),
                var_bounds,
                {},  // rvars
                {},  // rvar_bounds
                TailStrategy::ShiftInwards,
                is_gpu,
                schedule_source);
        }
    } else {
        // If the pure domain is smaller than some thresholds,
        // we try to apply rfactor to increase parallelism:
        bool checked_associative = false;
        bool is_associative = false;
        int domain_size = 1;
        for (int b : var_bounds) {
            domain_size *= b;
        }
        std::vector<ReductionVariable> reduction_vars =
            func.update(update_id).get_schedule().rvars();
        std::vector<int> rvar_bounds = get_rvar_bounds(reduction_vars);
        std::vector<RVar> rvars;
        rvars.reserve(reduction_vars.size());
        for (const ReductionVariable &r : reduction_vars) {
            rvars.emplace_back(r.var);
        }
        // Define the thresholds for the pure domain.
        // For CPU we want at least params.parallelism number of elements
        // to launch threads. For GPU we want to launch at least 64 GPU blocks.
        // We don't use a larger domain size for GPU since we can also use atomic
        // to increase parallelism and atomics are faster on GPU.
        // These numbers can be better tuned (issue 4346).
        const int cpu_max_domain_size = 8 * params.parallelism;
        constexpr int gpu_max_domain_size = 4096;
        int max_domain_size = is_gpu ? gpu_max_domain_size : cpu_max_domain_size;
        if (domain_size < max_domain_size) {
            if (!rvars.empty()) {
                // Check associativity
                std::vector<Expr> values =
                    func.update_values(update_id).as_vector();
                const auto &prover_result =
                    prove_associativity(func.name(),
                                        func.update_args(update_id),
                                        values);
                // Cache the associative check for later use.
                checked_associative = true;
                is_associative = prover_result.associative();
                if (is_associative) {
                    schedule_source << func.name() << ".update(" << update_id << ")\n";
                    // Generate a list of tiled RVars
                    std::vector<RVar> outer_rvars, inner_rvars;
                    std::vector<int> outer_rvar_sizes, inner_rvar_sizes;
                    for (int i = 0; i < (int)rvars.size(); i++) {
                        if (rvar_bounds[i] >= 8) {
                            // Let split_size = 8 * n where n is an integer and
                            // split_size > sqrt(rvar_bounds)
                            float target = std::sqrt(rvar_bounds[i]);
                            int split_size = int(std::ceil(target / 8.f)) * 8;
                            // Split the rvar
                            RVar outer, inner;
                            func.update(update_id)
                                .split(rvars[i], outer, inner, split_size,
                                       TailStrategy::GuardWithIf);
                            schedule_source << "    .split("
                                            << rvars[i].name() << ","
                                            << outer.name() << ","
                                            << inner.name() << ","
                                            << split_size << ","
                                            << TailStrategy::GuardWithIf << ")\n";
                            outer_rvars.push_back(outer);
                            inner_rvars.push_back(inner);
                            int outer_size = rvar_bounds[i] % split_size == 0 ? split_size : split_size + 1;
                            outer_rvar_sizes.push_back(outer_size);
                            inner_rvar_sizes.push_back(split_size);
                        } else {
                            inner_rvars.push_back(rvars[i]);
                            inner_rvar_sizes.push_back(rvar_bounds[i]);
                        }
                    }
                    schedule_source << ";\n";
                    if (!outer_rvars.empty() && !inner_rvars.empty()) {
                        // Rfactor all the outer RVars.
                        std::vector<std::pair<RVar, Var>> preserved;
                        std::vector<Var> interim_vars;
                        preserved.reserve(outer_rvars.size());
                        interim_vars.reserve(outer_rvars.size());
                        for (const RVar &r : outer_rvars) {
                            Var v;
                            preserved.emplace_back(r, v);
                            interim_vars.push_back(v);
                        }

                        Var factored;
                        Func interim =
                            func.update(update_id)
                                .rfactor(preserved)
                                .compute_root();
                        schedule_source << interim.name() << " = "
                                        << func.name() << ".update(" << update_id << ")\n";
                        schedule_source << "    .rfactor({";
                        for (int i = 0; i < (int)preserved.size(); i++) {
                            schedule_source << "{" << preserved[i].first.name() << ","
                                            << preserved[i].second.name() << "}";
                            if (i != (int)preserved.size() - 1) {
                                schedule_source << ",";
                            }
                        }
                        schedule_source << "})\n";
                        schedule_source << "    .compute_root()\n";

                        parallelize_vars_and_rvars(
                            params,
                            interim,
                            natural_vector_size(target, interim.values()[0].type()),
                            true,
                            interim_vars,
                            outer_rvar_sizes,
                            {},
                            {},
                            TailStrategy::ShiftInwards,
                            is_gpu,
                            schedule_source);
                        schedule_source << ";\n";
                        schedule_source << interim.name() << ".update()\n";
                        parallelize_vars_and_rvars(
                            params,
                            interim.update(0),
                            natural_vector_size(target, interim.values()[0].type()),
                            false,
                            interim_vars,
                            outer_rvar_sizes,
                            inner_rvars,
                            inner_rvar_sizes,
                            TailStrategy::GuardWithIf,
                            is_gpu,
                            schedule_source);
                        // Update rvars
                        rvars = outer_rvars;
                        rvar_bounds = outer_rvar_sizes;
                    }
                }
            }
        }
        // Gather pure variables
        std::vector<Expr> update_args = func.update_args(update_id);
        std::vector<Var> pure_args;
        std::vector<int> pure_arg_bounds;
        pure_args.reserve(update_args.size());
        pure_arg_bounds.reserve(update_args.size());
        int parallelism = 1;
        for (int arg_id = 0; arg_id < (int)update_args.size(); arg_id++) {
            Expr arg = update_args[arg_id];
            const Variable *var = arg.as<Variable>();
            if (var != nullptr &&
                !var->param.defined() &&
                !var->image.defined() &&
                !var->reduction_domain.defined()) {
                pure_args.emplace_back(var->name);
                pure_arg_bounds.push_back(var_bounds[arg_id]);
                parallelism *= pure_arg_bounds.back();
            }
        }
        // For CPU we want at least (8 * cores) * 16 parallelism
        // for vectorization + threading.
        // For GPU we want at least 10 * (num SMs) * 32 parallelism
        // Turing has ~70 SMs
        // These numbers can be better tuned (issue 4346).
        int cpu_min_parallelism = 8 * params.parallelism * 16;
        int gpu_min_parallelism = 10 * 70 * 32;
        int min_parallelism =
            is_gpu ? gpu_min_parallelism : cpu_min_parallelism;
        if (parallelism >= min_parallelism) {
            schedule_source << func.name() << ".update(" << update_id << ")\n";
            parallelize_vars_and_rvars(
                params,
                func.update(update_id),
                natural_vector_size(target, func.values()[0].type()),
                false,  // is_pure_def
                pure_args,
                pure_arg_bounds,
                {},  // rvars
                {},  // rvar_bounds
                TailStrategy::GuardWithIf,
                is_gpu,
                schedule_source);
        } else {
            // Not enough parallelism. Find parallelism from RDoms.
            if (!checked_associative) {
                // We can only do this for associative RDoms.
                std::vector<Expr> values =
                    func.update_values(update_id).as_vector();
                const auto &prover_result =
                    prove_associativity(func.name(),
                                        func.update_args(update_id),
                                        values);
                checked_associative = true;
                is_associative = prover_result.associative();
            }
            if (is_associative) {
                schedule_source << func.name() << ".update(" << update_id << ")\n";
                parallelize_vars_and_rvars(
                    params,
                    func.update(update_id),
                    natural_vector_size(target, func.values()[0].type()),
                    false,  // is_pure_def
                    pure_args,
                    pure_arg_bounds,
                    rvars,
                    rvar_bounds,
                    TailStrategy::GuardWithIf,
                    is_gpu,
                    schedule_source);
            } else {
                // Fall back to pure var parallelization
                schedule_source << func.name() << ".update(" << update_id << ")\n";
                parallelize_vars_and_rvars(
                    params,
                    func.update(update_id),
                    natural_vector_size(target, func.values()[0].type()),
                    false,  // is_pure_def
                    pure_args,
                    pure_arg_bounds,
                    {},  // rvars
                    {},  // rvar_bounds
                    TailStrategy::GuardWithIf,
                    is_gpu,
                    schedule_source);
            }
        }
    }
    schedule_source << ";\n";
}

}  // namespace

void generate_schedule(const std::vector<Function> &outputs,
                       const Target &target,
                       const GradientAutoschedulerParams &params,
                       AutoSchedulerResults *auto_scheduler_results) {
    // The first few steps are the same as src/AutoSchedule.cpp
    // Make an environment map which is used throughout the auto scheduling process.
    std::map<std::string, Function> env;
    for (const auto &func : outputs) {
        std::map<std::string, Function> local_env =
            find_transitive_calls(func);
        env.insert(local_env.begin(), local_env.end());
    }

    // Finalize all the LoopLevels
    for (auto &iter : env) {
        iter.second.lock_loop_levels();
    }

    // Compute the topological order
    std::vector<std::string> top_order = topological_order(outputs, env);
    // Run a pre-pass that inlines all trivial Funcs (i.e. the cost of
    // computing a Func <= calling that Func).
    // TODO: Note that the cost is estimated using heuristics based on CPU statistics
    // so this can be bad on GPU. In particular GPU should inline more aggressively.
    if (inline_all_trivial_functions(outputs, top_order, env)) {
        // Recompute env map since some functions are inlined.
        env.clear();
        for (const Function &f : outputs) {
            std::map<std::string, Function> more_funcs = find_transitive_calls(f);
            env.insert(more_funcs.begin(), more_funcs.end());
        }
    }
    std::vector<std::string> order =
        realization_order(outputs, env).first;
    // Repeatedly inline the functions that are only used by another function
    while (inline_all_element_wise_functions(outputs, order, env)) {
        // Recompute env map since some functions are inlined.
        env.clear();
        for (const Function &f : outputs) {
            std::map<std::string, Function> more_funcs = find_transitive_calls(f);
            env.insert(more_funcs.begin(), more_funcs.end());
        }
        order = realization_order(outputs, env).first;
    }

    // Bounds inference using the given estimation
    std::vector<Box> output_bounds_expr;
    output_bounds_expr.reserve(outputs.size());
    for (const auto &output : outputs) {
        const FuncSchedule &schedule = output.schedule();
        const std::vector<Bound> &estimates = schedule.estimates();

        std::vector<Interval> b;
        b.reserve(output.args().size());
        for (const auto &arg : output.args()) {
            bool found = false;
            Bound est;
            for (int i = (int)estimates.size() - 1; i >= 0; --i) {
                if ((estimates[i].var == arg) && estimates[i].min.defined() &&
                    estimates[i].extent.defined()) {
                    found = true;
                    est = estimates[i];
                    break;
                }
            }
            user_assert(found && est.min.type().is_int() && est.extent.type().is_int())
                << "Please provide a valid estimate for dimension "
                << arg << " of output \"" << output.name() << "\"\n";
            b.emplace_back(est.min, simplify(est.min + est.extent - 1));
        }
        output_bounds_expr.emplace_back(b);
    }

    std::map<std::string, Box> func_bounds = inference_bounds(outputs, output_bounds_expr);
    for (const auto &it : func_bounds) {
        const Box &bounds = it.second;
        for (int d = 0; d < (int)bounds.size(); d++) {
            user_assert(bounds[d].is_bounded()) << "Access to function or buffer " << it.first << " at dimension " << d << " is not bounded. "
                                                << "We can only schedule bounded accesses.\n";
        }
    }

    std::set<std::string> output_set;
    for (const auto &output : outputs) {
        output_set.insert(output.name());
    }

    std::ostringstream schedule_source;
    // Traverse from the consumers to the producers
    for (auto it = order.rbegin(); it != order.rend(); it++) {
        Func func(env[*it]);
        debug(1) << "[gradient_autoscheduler] Processing function:" << *it << "\n";
        // Get the bounds in integer constant by substitute all the parameters' estimates.
        Box bounds = func_bounds[*it];
        std::vector<int> int_bounds = get_int_bounds(bounds);
        // Scheduling pure definition
        apply_schedule(params, target, func, -1, int_bounds, target.has_gpu_feature(), schedule_source);
        // Scheduling the updates
        for (int update_id = 0;
             update_id < func.num_update_definitions(); update_id++) {
            apply_schedule(params, target, func, update_id, int_bounds, target.has_gpu_feature(), schedule_source);
        }
    }

    auto_scheduler_results->schedule_source = schedule_source.str();
    debug(1) << schedule_source.str() << "\n";
}

struct Li2018 {
    void operator()(const Pipeline &p, const Target &target, const AutoschedulerParams &params_in, AutoSchedulerResults *results) {
        internal_assert(params_in.name == "Li2018");

        std::vector<Function> outputs;
        for (const Func &f : p.outputs()) {
            outputs.push_back(f.function());
        }
        GradientAutoschedulerParams params;
        {
            ParamParser parser(params_in.extra);
            parser.parse("parallelism", &params.parallelism);
            parser.finish();
        }
        generate_schedule(outputs, target, params, results);
        results->autoscheduler_params = params_in;
    }
};

REGISTER_AUTOSCHEDULER(Li2018)

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide
