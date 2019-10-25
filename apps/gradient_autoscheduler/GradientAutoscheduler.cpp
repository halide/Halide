#include "Halide.h"
#include "Errors.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

namespace {

std::map<std::string, Box> inference_bounds(const std::vector<Function> &functions,
                                            const std::vector<Box> &output_bounds) {
    std::vector<Func> funcs;
    funcs.reserve(functions.size());
    for (const auto &f : functions) {
        funcs.push_back(Func(f));
    }
    return inference_bounds(funcs, output_bounds);
}

template <typename T>
std::vector<int> sort_indices(const std::vector<T> &v) {
    std::vector<int> idx(v.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(),
         [&v](int i1, int i2) {return v[i1] < v[i2];});
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
        user_assert(extent_int != nullptr) <<
            "extent:" << extent << " is not constant.\n";
        int_bounds.push_back(*extent_int);
    }
    return int_bounds;
}

std::vector<int> get_rvar_bounds(const std::vector<ReductionVariable> &rvars) {
    std::vector<int> rvar_bounds;
    rvar_bounds.reserve(rvars.size());
    for (int arg_id = 0; arg_id < (int)rvars.size(); arg_id++) {
        Expr extent = simplify(substitute_var_estimates(rvars[arg_id].extent));
        const int64_t *extent_int = as_const_int(extent);
        user_assert(extent_int != nullptr) <<
            "extent:" << extent << " is not constant.\n";
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

void reorder_storage(Stage stage,
                     const std::vector<Var> &all_vars,
                     std::ostringstream &schedule_source) {
    internal_assert(false) << "Can't reorder storage of a stage.";
}

int natural_vector_size(const Target &target, const Type &t) {
    const int data_size = t.bytes();
    if (target.os == Target::OSUnknown || target.arch == Target::ArchUnknown || target.bits != 0) {
        return 32 / data_size;
    } else {
        return target.natural_vector_size(t);
    }
}

template <typename FuncOrStage>
void parallelize_vars_and_rvars_gpu(
        const MachineParams &params,
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
    const int split_size = 64;
    std::vector<Var> gpu_blocks;
    Var gpu_threads("");
    int gpu_thread_dim = -1;
    for (int i = 0; i < (int)vars.size(); i++) {
        if (gpu_threads.name().empty() && var_bounds[i] >= split_size) {
            gpu_thread_dim = i;
            Var outer, inner;
            func_or_stage.split(vars[i],
                                outer,
                                inner,
                                split_size,
                                tail);
            schedule_source << "    .split(" <<
                vars[i].name() << "," <<
                outer.name() << "," <<
                inner.name() << "," <<
                split_size << "," <<
                tail << ")\n";
            gpu_blocks.push_back(outer);
            gpu_threads = inner;
        } else {
            gpu_blocks.push_back(vars[i]);
        }
    }

    std::vector<RVar> serial_rvars;
    std::vector<RVar> r_gpu_blocks;
    RVar r_gpu_threads("");
    if (!gpu_threads.name().empty()) {
        // If we can't find any GPU threads, parallelize RVars to find more parallelism
        for (int i = 0; i < (int)rvars.size(); i++) {
            if (!r_gpu_threads.name().empty() && rvar_bounds[i] > split_size) {
                RVar outer, inner;
                func_or_stage.split(rvars[i],
                                    outer,
                                    inner,
                                    split_size,
                                    tail);
                schedule_source << "    .split(" <<
                    rvars[i].name() << "," <<
                    outer.name() << "," <<
                    inner.name() << "," <<
                    split_size << "," <<
                    tail << ")\n";
                r_gpu_blocks.push_back(outer);
                r_gpu_threads = inner;
            } else {
                r_gpu_blocks.push_back(rvars[i]);
            }
        }
    } else {
        serial_rvars = rvars;
    }

    // Fuse all gpu blocks into a single variable
    Var fused_var("");
    if (gpu_blocks.size() > 0) {
        fused_var = gpu_blocks[0];
        // inner to outer
        for (int i = 1; i < (int)gpu_blocks.size(); i++) {
            func_or_stage.fuse(fused_var, gpu_blocks[i], fused_var);
            schedule_source << "    .fuse(" <<
                fused_var.name() << "," <<
                gpu_blocks[i].name() << "," <<
                fused_var.name() << ")\n";
        }
    }
    RVar fused_rvar("");
    if (r_gpu_blocks.size() > 0) {
        fused_rvar = r_gpu_blocks[0];
        // inner to outer
        for (int i = 1; i < (int)r_gpu_blocks.size(); i++) {
            func_or_stage.fuse(fused_rvar, r_gpu_blocks[i], fused_rvar);
            schedule_source << "    .fuse(" <<
                fused_rvar.name() << "," <<
                r_gpu_blocks[i].name() << "," <<
                fused_rvar.name() << ")\n";
        }
    }

    // Reorder: the order is rvars -> gpu_threads -> gpu_blocks
    std::vector<VarOrRVar> all_vars;
    all_vars.reserve(serial_rvars.size() + 4);
    for (RVar v : serial_rvars) {
        all_vars.push_back(v);
    }
    if (!r_gpu_threads.name().empty()) {
        all_vars.push_back(r_gpu_threads);
    }
    if (!gpu_threads.name().empty()) {
        all_vars.push_back(gpu_threads);
    }
    if (!fused_var.name().empty()) {
        all_vars.push_back(fused_var);
    }
    if (!fused_rvar.name().empty()) {
        all_vars.push_back(fused_rvar);
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
    
    if (gpu_blocks.size() + r_gpu_blocks.size() > 0) { 
        // Assign outer loops to GPU blocks
        if (!fused_var.name().empty()) {
            func_or_stage.gpu_blocks(fused_var);
            schedule_source << "    .gpu_blocks(" << fused_var.name() << ")\n";
        }
        if (!fused_rvar.name().empty()) {
            func_or_stage.atomic()
                         .gpu_blocks(fused_rvar);
            schedule_source << "    .atomic()\n";
            schedule_source << "    .gpu_blocks(" << fused_rvar.name() << ")\n";
        }
        // Assign inner loops to GPU threads
        if (!gpu_threads.name().empty()) {
            func_or_stage.gpu_threads(gpu_threads);
            schedule_source << "    .gpu_threads(" << gpu_threads.name() << ")\n";
        }
        if (!r_gpu_threads.name().empty()) {
            func_or_stage.gpu_threads(r_gpu_threads);
            schedule_source << "    .r_gpu_threads(" << r_gpu_threads.name() << ")\n";
        }
    } else {
        // Not enough parallelism, use a single GPU thread
        func_or_stage.gpu_single_thread();
        schedule_source << "    .gpu_single_thread()\n";
    }
}


template <typename FuncOrStage>
void parallelize_vars_and_rvars_cpu(
        const MachineParams &params,
        FuncOrStage func_or_stage,
        int natural_vector_size,
        bool is_pure_def,
        const std::vector<Var> &vars,
        const std::vector<int> &var_bounds,
        const std::vector<RVar> &rvars,
        const std::vector<int> &rvar_bounds,
        TailStrategy tail,
        std::ostringstream &schedule_source) {
    // Find the first variable that has bounds larger or equal than 16,
    // this is our vectorized dimension
    int split_size = natural_vector_size;
    std::vector<Var> parallel_vars;
    Var vectorized_var("");
    int num_threads_var = 1;
    int vectorized_dim = -1;
    for (int i = 0; i < (int)vars.size(); i++) {
        if (vectorized_var.name().empty() && var_bounds[i] >= split_size) {
            vectorized_dim = i;
            Var outer, inner;
            func_or_stage.split(vars[i],
                                outer,
                                inner,
                                split_size,
                                tail);
            schedule_source << "    .split(" <<
                vars[i].name() << "," <<
                outer.name() << "," <<
                inner.name() << "," <<
                split_size << "," <<
                tail << ")\n";
            parallel_vars.push_back(outer);
            vectorized_var = inner;
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
    RVar vectorized_rvar("");
    int num_threads_rvar = 1;
    for (int i = 0; i < (int)rvars.size(); i++) {
        if (vectorized_var.name().empty() && vectorized_rvar.name().empty() &&
                rvar_bounds[i] >= split_size) {
            RVar outer, inner;
            func_or_stage.split(rvars[i],
                                outer,
                                inner,
                                split_size,
                                tail);
            schedule_source << "    .split(" <<
                rvars[i].name() << "," <<
                outer.name() << "," <<
                inner.name() << "," <<
                split_size << "," <<
                tail << ")\n";
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
            vectorized_rvar = inner;
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
    Var fused_var("");
    if (parallel_vars.size() > 0) {
        fused_var = parallel_vars[0];
        // inner to outer
        for (int i = 1; i < (int)parallel_vars.size(); i++) {
            func_or_stage.fuse(fused_var, parallel_vars[i], fused_var);
            schedule_source << "    .fuse(" <<
                fused_var.name() << "," <<
                parallel_vars[i].name() << "," <<
                fused_var.name() << ")\n";
        }
    }

    // Fuse all parallel rvars into a single variable for parallelism
    RVar fused_rvar("");
    if (parallel_rvars.size() > 0) {
        fused_rvar = parallel_rvars[0];
        // inner to outer
        for (int i = 1; i < (int)parallel_rvars.size(); i++) {
            func_or_stage.fuse(fused_rvar, parallel_rvars[i], fused_rvar);
            schedule_source << "    .fuse(" <<
                fused_rvar.name() << "," <<
                parallel_rvars[i].name() << "," <<
                fused_rvar.name() << ")\n";
        }
    }

    // Reorder: the order is serial_rvars -> vectorized_rvar/vectorized_var ->
    //                       fused_rvars -> fused_vars
    std::vector<VarOrRVar> all_vars;
    all_vars.reserve(serial_rvars.size() + 4);
    for (RVar v : serial_rvars) {
        all_vars.push_back(v);
    }
    if (!vectorized_rvar.name().empty()) {
        all_vars.push_back(vectorized_rvar);
    }
    if (!vectorized_var.name().empty()) {
        all_vars.push_back(vectorized_var);
    }
    if (!fused_rvar.name().empty()) {
        all_vars.push_back(fused_rvar);
    }
    if (!fused_var.name().empty()) {
        all_vars.push_back(fused_var);
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
  
    if (!fused_var.name().empty()) {
        // Parallelize vars
        if (num_threads_var > params.parallelism * 8) {
            func_or_stage.parallel(fused_var,
                                   params.parallelism * 8,
                                   tail);
            schedule_source << "    .parallel(" <<
                fused_var.name() << "," <<
                params.parallelism * 8 << "," <<
                tail << ")\n";
        } else {
            func_or_stage.parallel(fused_var);
            schedule_source << "    .parallel(" <<
                fused_var.name() << ")\n";
        }
    }
    if (!fused_rvar.name().empty()) {
        // Parallelize rvars
        if (num_threads_rvar > params.parallelism * 8) {
            func_or_stage.atomic()
                         .parallel(fused_rvar,
                                   params.parallelism * 8,
                                   tail);
            schedule_source << "    .atomic()\n";
            schedule_source << "    .parallel(" <<
                fused_rvar.name() << "," <<
                params.parallelism * 8 << "," <<
                tail << ")\n";
        } else {
            func_or_stage.atomic()
                         .parallel(fused_rvar);
            schedule_source << "    .atomic()\n";
            schedule_source << "    .parallel(" <<
                fused_rvar.name() << ")\n";
        }
    }
    if (!vectorized_var.name().empty()) {
        func_or_stage.vectorize(vectorized_var);
        schedule_source << "    .vectorize(" <<
            vectorized_var.name() << ")\n";
    }
    if (!vectorized_rvar.name().empty()) {
        func_or_stage.atomic().vectorize(vectorized_rvar);
        schedule_source << "    .atomic()\n";
        schedule_source << "    .vectorize(" <<
            vectorized_rvar.name() << ")\n";
    }
}

template <typename FuncOrStage>
void parallelize_vars_and_rvars(
        const MachineParams &params,
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

void apply_schedule(const MachineParams &params,
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
                true, // is_pure_def
                func.args(),
                var_bounds,
                {}, // rvars
                {}, // rvar_bounds
                TailStrategy::ShiftInwards,
                is_gpu,
                schedule_source);
        }
    } else {
        // If the pure domain is smaller than the reduction domain,
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
        for (ReductionVariable r : reduction_vars) {
            rvars.push_back(RVar(r.var));
        }
        int rdomain_size = 1;
        for (int b : rvar_bounds) {
            rdomain_size *= b;
        }
        int cpu_max_domain_size = 8 * params.parallelism;
        int gpu_max_domain_size = 4096;
        int max_domain_size = is_gpu ? gpu_max_domain_size : cpu_max_domain_size;
        if (domain_size < max_domain_size) {
            if (rvars.size() > 0) {
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
                            schedule_source << "    .split(" <<
                                rvars[i].name() << "," <<
                                outer.name() << "," <<
                                inner.name() << "," <<
                                split_size << "," <<
                                TailStrategy::GuardWithIf << ")\n";
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
                    if (outer_rvars.size() > 0 && inner_rvars.size() > 0) {
                        // Rfactor all the outer RVars.
                        std::vector<std::pair<RVar, Var>> preserved;
                        std::vector<Var> interm_vars;
                        preserved.reserve(outer_rvars.size());
                        interm_vars.reserve(outer_rvars.size());
                        for (RVar r : outer_rvars) {
                            Var v;
                            preserved.push_back({r, v});
                            interm_vars.push_back(v);
                        }

                        Var factored;
                        Func interm =
                            func.update(update_id)
                                .rfactor(preserved)
                                .compute_root();
                        schedule_source << interm.name() << " = " <<
                            func.name() << ".update(" << update_id << ")\n";
                        schedule_source << "    .rfactor({";
                        for (int i = 0; i < (int)preserved.size(); i++) {
                            schedule_source << "{" << preserved[i].first.name() << "," <<
                                                      preserved[i].second.name() << "}";
                            if (i != (int)preserved.size() - 1) {
                                schedule_source << ",";
                            }
                        }
                        schedule_source << "})\n";
                        schedule_source << "    .compute_root()\n";

                        parallelize_vars_and_rvars(
                            params,
                            interm,
                            natural_vector_size(target, interm.values()[0].type()),
                            true,
                            interm_vars,
                            outer_rvar_sizes,
                            {},
                            {},
                            TailStrategy::ShiftInwards,
                            is_gpu,
                            schedule_source);
                        schedule_source << ";\n";
                        schedule_source << interm.name() << ".update()\n";
                        parallelize_vars_and_rvars(
                            params,
                            interm.update(0),
                            natural_vector_size(target, interm.values()[0].type()),
                            false,
                            interm_vars,
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
                pure_args.push_back(Var(var->name));
                pure_arg_bounds.push_back(var_bounds[arg_id]);
                parallelism *= pure_arg_bounds.back();
            }
        }
        // For CPU we want at least (8 * cores) * 16 parallelism
        // for vectorization + threading.
        // For GPU we want at least 10 * (num SMs) * 32 parallelism
        // Turing has ~70 SMs
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
                false, // is_pure_def
                pure_args,
                pure_arg_bounds,
                {}, // rvars
                {}, // rvar_bounds
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
                        false, // is_pure_def
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
                        false, // is_pure_def
                        pure_args,
                        pure_arg_bounds,
                        {}, // rvars
                        {}, // rvar_bounds
                        TailStrategy::GuardWithIf,
                        is_gpu,
                        schedule_source);
            }
        }
    }
    schedule_source << ";\n";
}

}

void generate_schedule(const std::vector<Function> &outputs,
                       const Target &target,
                       const MachineParams &params,
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
    // Run a pre-pass that inline all trivial Funcs (i.e. the cost of
    // computing a Func <= calling that Func).
    // XXX: Note that the cost is estimated using heuristics based on CPU statistics
    // so this can be bad on GPU.
    if (inline_all_trivial_functions(outputs, top_order, env)) {
        // Recompute env map since some functions are inlined.
        env.clear();
        for (Function f : outputs) {
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
        for (Function f : outputs) {
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
        user_assert((int)estimates.size() == output.dimensions()) <<
            "Bound estimates of function " << output.name() << " are not provided.\n";
        std::vector<Interval> b;
        b.reserve(estimates.size());
        for (const auto &e : estimates) {
            b.push_back(Interval(e.min, simplify(e.min + e.extent - 1)));
        }
        output_bounds_expr.push_back(Box(b));
    }

    std::map<std::string, Box> func_bounds = inference_bounds(outputs, output_bounds_expr);
    for (const auto &it : func_bounds) {
        const Box &bounds = it.second;
        for (int d = 0; d < (int)bounds.size(); d++) {
            user_assert(bounds[d].is_bounded()) << "Access to function or buffer " << it.first << " at dimension " << d << " is not bounded. "
                                                << "We can only differentiate bounded accesses.\n";
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

    auto_scheduler_results->scheduler_name = "gradient autoscheduler";
    auto_scheduler_results->schedule_source = schedule_source.str();
}


// Halide uses a plugin architecture for registering custom
// autoschedulers. We register our autoscheduler using a static
// constructor.
struct RegisterGradientAutoscheduler {
    RegisterGradientAutoscheduler() {
        debug(1) << "[gradient_autoscheduler] Registering autoscheduler...\n";
        Pipeline::set_custom_auto_scheduler(*this);
    }

    void operator()(Pipeline p, const Target &target, const MachineParams &params, AutoSchedulerResults *results) {
        std::vector<Function> outputs;
        for (Func f : p.outputs()) {
            outputs.push_back(f.function());
        }
        generate_schedule(outputs, target, params, results);
    }
} register_auto_scheduler;

} // Autoscheduler
} // Internal
} // Halide
