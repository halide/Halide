#include "Halide.h"

#ifndef user_error
#define user_error                Halide::Internal::ErrorReport(__FILE__, __LINE__, nullptr, Halide::Internal::ErrorReport::User)
#endif

#ifndef user_warning
#define user_warning              Halide::Internal::ErrorReport(__FILE__, __LINE__, nullptr, Halide::Internal::ErrorReport::User | Halide::Internal::ErrorReport::Warning)
#endif

#ifndef user_assert
#define user_assert(c)            _halide_internal_assertion(c, Halide::Internal::ErrorReport::User)
#endif

#ifndef internal_assert
#define internal_assert(c)        _halide_internal_assertion(c, 0)
#endif

#ifndef internal_error
#define internal_error            Halide::Internal::ErrorReport(__FILE__, __LINE__, nullptr, 0)
#endif

namespace Halide {
namespace Internal {
namespace Autoscheduler {

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

// Taken from src/AutoSchedule.cpp
// If the cost of computing a Func is about the same as calling the Func,
// inline the Func. Return true of any of the Funcs is inlined.
bool inline_all_trivial_functions(const std::vector<Function> &outputs,
                                  const std::vector<std::string> &order,
                                  const std::map<std::string, Function> &env) {
    bool inlined = false;
    // The very last few functions in 'order' are the last to be realized in the
    // pipeline (the final producers) so there is no point in checking it.
    for (int i = 0; i < (int)order.size() - (int)outputs.size(); ++i) {
        bool is_output = false;
        for (const Function &f : outputs) {
            if (order[i] == f.name()) {
                is_output = true;
                break;
            }
        }
        if (is_output) {
            // Should not inline output Func
            debug(5) << "Skip inlining " << order[i] << " since it is an output\n";
            continue;
        }
        Function f1 = env.at(order[i]);
        if (is_func_trivial_to_inline(f1)) {
            f1.schedule().store_level().lock();
            inlined = true;
            debug(4) << "Function \"" << order[i] << "\" is trivial to inline\n";
            for (int j = i + 1; j < (int)order.size() - (int)outputs.size(); ++j) {
                internal_assert(order[i] != order[j]);
                Function f2 = env.at(order[j]);

                if (f2.has_extern_definition() &&  !f1.is_wrapper()) {
                    debug(5) << "Skip inlining of function \"" << f1.name()
                             << "\" inside \"" << f2.name() << "\", because "
                             << "non-wrapper functions cannot be inlined inside "
                             << "extern functions.\n";
                } else {
                    debug(5) << "Inline trivial function \"" << f1.name()
                             << "\" inside \"" << f2.name() << "\"\n";
                    inline_function(f2, f1);
                }
            }
        }
    }
    return inlined;
}

// Taken from src/AutoSchedule.cpp
// Determine if a Func (order[index]) is only consumed by another single Func
// in element-wise manner. If it is, return the name of the consumer Func;
// otherwise, return an empty string.
std::string is_func_called_element_wise(const std::vector<std::string> &order, size_t index,
                                        const std::map<std::string, Function> &env) {
    Function f1 = env.at(order[index]);
    if (f1.has_extern_definition() || !f1.can_be_inlined()) {
        return "";
    }
    internal_assert(index < order.size());

    std::string caller = "";
    for (size_t i = index + 1; i < order.size(); ++i) {
        Function f2 = env.at(order[i]);
        if (f2.has_extern_definition()) {
            continue;
        }
        int num_stages = f2.updates().size() + 1;
        for (int s = 0; s < num_stages; ++s) {
            Definition def = get_stage_definition(f2, s);
            FindAllCalls find;
            def.accept(&find);

            if (find.funcs_called.count(f1.name())) {
                if (caller.empty()) {
                    caller = f2.name();
                } else {
                    // Found another caller of 'f1'
                    return "";
                }
            }
            for (const auto &iter : find.call_args) {
                if (iter.first != f1.name()) {
                    continue;
                }
                if (def.args().size() != iter.second.size()) {
                    // It's not an element-wise access
                    return "";
                }
                for (size_t j = 0; j < iter.second.size(); ++j) {
                    if (!equal(def.args()[j], iter.second[j])) {
                        // It's not an element-wise access
                        return "";
                    }
                }
            }
        }
    }
    return caller;
}

// Taken from src/AutoSchedule.cpp
// Inline a Func if its values are only consumed by another single Func in
// element-wise manner.
bool inline_all_element_wise_functions(const std::vector<Function> &outputs,
                                       const std::vector<std::string> &order,
                                       const std::map<std::string, Function> &env) {
    bool inlined = false;
    // The very last few functions in 'order' are the last to be realized in the
    // pipeline (the final producers) so there is no point in checking it.
    for (int i = 0; i < (int)order.size() - (int)outputs.size(); ++i) {
        bool is_output = false;
        for (const Function &f : outputs) {
            if (order[i] == f.name()) {
                is_output = true;
                break;
            }
        }
        if (is_output) {
            // Should not inline output Func
            debug(5) << "Skip inlining " << order[i] << " since it is an output\n";
            continue;
        }
        std::string caller = is_func_called_element_wise(order, i, env);
        if (!caller.empty()) {
            inlined = true;
            debug(4) << "Inline function \"" << order[i] << "\" since it is called only by "
                     << caller << " in element-wise manner\n";
            internal_assert(order[i] != caller);
            Function f1 = get_element(env, order[i]);
            f1.schedule().store_level().lock();
            inline_function(env.at(caller), f1);
        }
    }
    return inlined;
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
    // Can't reorder storage of a stage.
}

int natural_vector_size(const Target &target, const Type &t) {
    const bool is_integer = t.is_int() || t.is_uint();
    const int data_size = t.bytes();
    if (is_integer && (target.has_feature(Halide::Target::AVX512_Skylake) ||
            target.has_feature(Halide::Target::AVX512_Cannonlake))) {
        // AVX512BW exists on Skylake and Cannonlake
        return 64 / data_size;
    } else if (t.is_float() && (target.has_feature(Halide::Target::AVX512) ||
            target.has_feature(Halide::Target::AVX512_KNL) ||
            target.has_feature(Halide::Target::AVX512_Skylake) ||
            target.has_feature(Halide::Target::AVX512_Cannonlake))) {
        // AVX512F is on all AVX512 architectures
        return 64 / data_size;
    } else {
        return 32 / data_size;
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
    int split_size = 64;
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
    // Only reorder if there's any variable at all.
    if (all_vars.size() > 0) {
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
            int b = var_bounds[i] / split_size;
            if (var_bounds[i] % split_size == 0) {
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

    // Fuse all parallel  rvars into a single variable for parallelism
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
    if (all_vars.size() > 0) {
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
            vectorized_var.name() << ")\n";
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
        schedule_source << ";\n";
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
                    int tile_size = rvars.size() >= 2 ? 8 : 64;
                    // Apply heuristics tiling to split the args.
                    int num_tilable = 0;
                    for (int i = 0; i < (int)rvars.size(); i++) {
                        if (rvar_bounds[i] >= tile_size) {
                            num_tilable += 1;
                        }
                    }
                    if (rvars.size() >= 2 && num_tilable <= 1) {
                        // The case where Func has a single very long dimension
                        // but the rest are short.
                        tile_size = 64;
                    }
                    // Generate a list of tiled RVars
                    std::vector<RVar> outer_rvars, inner_rvars;
                    int outer_size = 1;
                    std::vector<int> inner_rvar_sizes;
                    for (int i = 0; i < (int)rvars.size(); i++) {
                        if (rvar_bounds[i] > tile_size) {
                            // Split the rvar
                            RVar outer, inner;
                            func.update(update_id)
                                .split(rvars[i], outer, inner, tile_size,
                                    TailStrategy::GuardWithIf);
                            schedule_source << "    .split(" <<
                                rvars[i].name() << "," <<
                                outer.name() << "," <<
                                inner.name() << "," <<
                                tile_size << "," <<
                                TailStrategy::GuardWithIf << ")\n";
                            outer_rvars.push_back(outer);
                            inner_rvars.push_back(inner);
                            int size = (rvar_bounds[i] / tile_size);
                            if (rvar_bounds[i] % tile_size != 0) {
                                size += 1;
                            }
                            outer_size *= size;
                            inner_rvar_sizes.push_back(tile_size);
                        } else {
                            inner_rvars.push_back(rvars[i]);
                            inner_rvar_sizes.push_back(rvar_bounds[i]);
                        }
                    }
                    // Fuse all outer RVars
                    if (outer_rvars.size() > 0) {
                        RVar fused = outer_rvars[0];
                        // inner to outer
                        for (int i = 1; i < (int)outer_rvars.size(); i++) {
                            func.update(update_id)
                                .fuse(fused, outer_rvars[i], fused);
                            schedule_source << "    .fuse(" <<
                                fused.name() << "," <<
                                outer_rvars[i].name() << "," <<
                                fused.name() << ")\n";
                        }
                        // Reorder
                        std::vector<VarOrRVar> all_rvars;
                        all_rvars.reserve(inner_rvars.size() + 1);
                        for (RVar r : inner_rvars) {
                            all_rvars.push_back(r);
                        }
                        all_rvars.push_back(fused);
                        func.update(update_id).reorder(all_rvars);
                        schedule_source << "    .reorder(";
                        for (int i = 0; i < (int)all_rvars.size(); i++) {
                            schedule_source << all_rvars[i].name();
                            if (i != (int)all_rvars.size() - 1) {
                                schedule_source << ",";
                            }
                        }
                        schedule_source << ")\n";
                        schedule_source << ";\n";
                        // If there is any inner RVars, rfactor all the
                        // outer RVars
                        if (inner_rvars.size() > 0) {
                            Var factored;
                            Func interm =
                                func.update(update_id)
                                    .rfactor(fused, factored);
                            schedule_source << interm.name() << " = " <<
                                func.name() << ".update(" << update_id << ")" <<
                                ".rfactor(" << fused.name() << "," << factored.name() << ");\n";
                            if (is_gpu) {
                                Var outer, inner;
                                interm.compute_root()
                                      .split(factored, outer, inner, 64, TailStrategy::GuardWithIf)
                                      .gpu_blocks(outer)
                                      .gpu_threads(inner);
                                schedule_source << interm.name() << ".compute_root()\n";
                                schedule_source << "    .split(" <<
                                    factored.name() << "," << outer.name() << "," << inner.name() << "," <<
                                    64 << "," << TailStrategy::GuardWithIf << ")\n";
                                schedule_source << "    .gpu_blocks(" << outer.name() << ")\n" << std::endl;
                                schedule_source << "    .gpu_threads(" << inner.name() << ")\n" << std::endl;
                                schedule_source << ";\n";
                                interm.update()
                                      .split(factored, outer, inner, 64,
                                             TailStrategy::GuardWithIf)
                                      .gpu_blocks(outer)
                                      .gpu_threads(inner);
                                schedule_source << interm.name() << ".update()\n";
                                schedule_source << "    .split(" <<
                                    factored.name() << "," << outer.name() << "," << inner.name() << "," <<
                                    64 << "," << TailStrategy::GuardWithIf << ")\n";
                                schedule_source << "    .gpu_blocks(" << outer.name() << ")\n";
                                schedule_source << "    .gpu_threads(" << inner.name() << ")\n";
                                schedule_source << ";\n";
                            } else {
                                Var outer, inner;
                                interm.compute_root()
                                      .split(factored, outer, inner, 16,
                                             TailStrategy::GuardWithIf)
                                      .parallel(outer)
                                      .vectorize(inner);
                                schedule_source << interm.name() << ".compute_root()\n";
                                schedule_source << "    .split(" <<
                                    factored.name() << "," << outer.name() << "," << inner.name() << "," <<
                                    16 << "," << TailStrategy::GuardWithIf << ")\n";
                                schedule_source << "    .parallel(" << outer.name() << ")\n";
                                schedule_source << "    .vectorize(" << inner.name() << ")\n";
                                schedule_source << ";\n";
                                interm.update()
                                      .split(factored, outer, inner, 16,
                                             TailStrategy::GuardWithIf)
                                      .parallel(outer)
                                      .vectorize(inner);
                                schedule_source << interm.name() << ".update()\n";
                                schedule_source << "    .split(" <<
                                    factored.name() << "," << outer.name() << "," << inner.name() << "," <<
                                    16 << "," << TailStrategy::GuardWithIf << ")\n";
                                schedule_source << "    .parallel(" << outer.name() << ")\n";
                                schedule_source << "    .vectorize(" << inner.name() << ")\n";
                                schedule_source << ";\n";
                            }
                        }
                        // Update rvars
                        rvars = {fused};
                        rvar_bounds = {outer_size};
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
