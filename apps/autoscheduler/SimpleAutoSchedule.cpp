#include "SimpleAutoSchedule.h"
#include "Errors.h"

#include <numeric>

namespace Halide {

using namespace Internal;

template <typename T>
std::vector<int> sort_indices(const std::vector<T> &v) {
    std::vector<int> idx(v.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(),
         [&v](int i1, int i2) {return v[i1] < v[i2];});
    return idx;
}

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


void simple_autoschedule(std::vector<Func> &outputs,
                         const std::map<std::string, Expr> &parameters,
                         const std::vector<std::vector<std::pair<int, int>>> &output_bounds,
                         const SimpleAutoscheduleOptions &options) {
    user_assert(outputs.size() == output_bounds.size()) <<
        "[simple_autoschedule] outputs size and output_bounds size don't match \n";
    for (int i = 0; i < (int)output_bounds.size(); i++) {
        user_assert(outputs[i].dimensions() == (int)output_bounds[i].size()) <<
            "[simple_autoschedule] outputs dimensionality don't match with output_bounds. " <<
            outputs[i].name() << " " << outputs[i].dimensions() << " " << output_bounds[i].size() << "\n";
    }

    std::vector<Function> output_functions;
    output_functions.reserve(outputs.size());
    for (const auto &func : outputs) {
        output_functions.push_back(func.function());
    }
    // The first few steps are the same as AutoSchedule.cpp
    // Make an environment map which is used throughout the auto scheduling process.
    std::map<std::string, Function> env;
    for (const auto &func : output_functions) {
        std::map<std::string, Function> local_env =
            find_transitive_calls(func);
        env.insert(local_env.begin(), local_env.end());
    }
    // Compute the topological order
    std::vector<std::string> top_order = topological_order(output_functions, env);
    // Run a pre-pass that inline all trivial Funcs (i.e. the cost of
    // computing a Func <= calling that Func).
    // XXX: Note that the cost is estimated using heuristics based on CPU statistics
    // so this can be bad on GPU.
    if (inline_all_trivial_functions(output_functions, top_order, env)) {
        // Recompute env map since some functions are inlined.
        env.clear();
        for (Function f : output_functions) {
            std::map<std::string, Function> more_funcs = find_transitive_calls(f);
            env.insert(more_funcs.begin(), more_funcs.end());
        }
    }
    std::vector<std::string> order =
        realization_order(output_functions, env).first;
    // Repeatedly inline the functions that are only used by another function
    while (inline_all_element_wise_functions(output_functions, order, env)) {
        // Recompute env map since some functions are inlined.
        env.clear();
        for (Function f : output_functions) {
            std::map<std::string, Function> more_funcs = find_transitive_calls(f);
            env.insert(more_funcs.begin(), more_funcs.end());
        }
        order = realization_order(output_functions, env).first;
    }

    // Bounds inference using the given estimation
    std::vector<Box> output_bounds_expr;
    for (const auto &bounds : output_bounds) {
        std::vector<Interval> func_bounds;
        for (const auto &bound : bounds) {
            func_bounds.push_back(
                Interval(bound.first, bound.first + bound.second - 1));
        }
        output_bounds_expr.push_back(func_bounds);
    }
    std::map<std::string, Box> func_bounds =
        inference_bounds(outputs, output_bounds_expr);
    std::set<std::string> output_set;
    for (const auto &output : outputs) {
        output_set.insert(output.name());
    }

    debug(1) << "[simple_autoschedule] order:\n";
    for (auto it = order.begin(); it != order.end(); it++) {
        debug(1) << *it << "\n";
    }

    // Traverse from the consumers to the producers
    for (auto it = order.rbegin(); it != order.rend(); it++) {
        Func func(env[*it]);
        debug(1) << "[simple_autoschedule] processing function:" << *it << "\n";
        // Get the bounds in integer constant by substitute all the parameters in.
        Box bounds = func_bounds[*it];
        std::vector<int> int_bounds;
        int_bounds.reserve(bounds.size());
        debug(1) << "[simple_autoschedule] bounds:\n";
        for (int i = 0; i < (int)bounds.size(); i++) {
            Interval interval = bounds[i];
            Expr extent = simplify(interval.max - interval.min + 1);
            for (const auto &param : parameters) {
                extent = substitute(param.first, param.second, extent);
            }
            extent = simplify(extent);
            const int64_t *extent_int = as_const_int(extent);
            user_assert(extent_int != nullptr) << "extent:" << extent <<
                " is not constant.\n";
            int_bounds.push_back(*extent_int);
            debug(1) << (*extent_int) << "\n";
        }
        std::vector<int> bounds_rank = sort_indices(int_bounds);
        // Find the largest two dimensions
        int dim_width = -1, dim_height = -1;
        if ((int)int_bounds.size() >= 2) {
            int last_index = bounds_rank.size() - 1;
            dim_width = std::min(bounds_rank[last_index], bounds_rank[last_index-1]);
            dim_height = std::max(bounds_rank[last_index], bounds_rank[last_index-1]);
        }
        debug(1) << "[simple_autoschedule] dim_width:" << dim_width <<
            ", dim_height:" << dim_height << "\n";
        int largest_dim = -1;
        if (int_bounds.size() >= 1) {
            largest_dim = bounds_rank.back();
        }
        debug(1) << "[simple_autoschedule] largest_dim:" << largest_dim << "\n";

        if (output_set.find(func.name()) == output_set.end()) {
            // TODO(mgharbi): this should distinguish between internal Funcs and Generator Output params, which
            // break the memoization
            // func.memoize();
        }

        func.compute_root();
        // Initial definition is easy: everything is pure variables.
        // Just parallelize and vectorize if there are enough entries to launch threads.
        debug(1) << "[simple_autoschedule] scheduling initial definition" << "\n";
        int tile_width =
            options.gpu ? options.gpu_tile_width : options.cpu_tile_width;
        int tile_height =
            options.gpu ? options.gpu_tile_height : options.cpu_tile_height;
        int tile_channel = options.gpu_tile_channel;
        int min_gpu_threads = 1;
        int min_cpu_threads = 8;
        int min_threads = options.gpu ? min_gpu_threads : min_cpu_threads;
        int vectorize_width = 8;
        bool tilable = false;
        // If there's enough tiles
        if ((int)int_bounds.size() >= 2 &&
                int_bounds[dim_width] >= tile_width &&
                int_bounds[dim_height] >= tile_height &&
                (int_bounds[dim_width] / tile_width) *
                (int_bounds[dim_height] / tile_height) >= min_threads) {
            debug(1) << "[simple_autoschedule] Perform 2D tiling\n";
            // 2D tiling
            Var xo, yo, zo, xi, yi, zi;
            if (options.gpu) {
                // Fuse the rest dimensions and tile on the them
                Var fused_var;
                bool has_extra_dimensions = func.args().size() > 2;
                if (func.args().size() > 2) {
                    int extra_dim_size = 1;
                    for (int i = 0; i < (int)func.args().size(); i++) {
                        if (i == dim_width || i == dim_height) {
                            continue;
                        }
                        extra_dim_size *= int_bounds[i];
                    }
                    if (extra_dim_size >= options.gpu_tile_channel) {
                        bool first = true;
                        for (int i = 0; i < (int)func.args().size(); i++) {
                            if (i == dim_width || i == dim_height) {
                                continue;
                            }
                            if (first) {
                                fused_var = func.args()[i];
                                first = false;
                            } else {
                                func.fuse(fused_var, func.args()[i], fused_var);
                            }
                        }
                    } else {
                        has_extra_dimensions = false;
                    }
                }
                debug(1) << "[simple_autoschedule] has_extra_dimensions:" <<
                    has_extra_dimensions << "\n";
                if (!has_extra_dimensions) {
                    // No fused_vars
                    func.reorder(func.args()[dim_width], func.args()[dim_height])
                        .gpu_tile(func.args()[dim_width], func.args()[dim_height],
                            xo, yo, xi, yi, tile_width, tile_height);
                } else {
                    func.reorder(func.args()[dim_width], func.args()[dim_height], fused_var)
                        .gpu_tile(func.args()[dim_width], func.args()[dim_height], fused_var,
                            xo, yo, zo, xi, yi, zi, tile_width, tile_height, tile_channel);
                }
            } else {
                // CPU
                Var tile_index;
                func.tile(func.args()[dim_width], func.args()[dim_height],
                        xo, yo, xi, yi, tile_width, tile_height)
                    .fuse(xo, yo, tile_index)
                    .parallel(tile_index)
                    .vectorize(xi, vectorize_width);
            }
            tilable = true;
        } else if ((int)int_bounds.size() >= 1 &&
                        int_bounds[largest_dim] >= (tile_width * tile_height) &&
                        (int_bounds[largest_dim] / (tile_width * tile_height)) >=
                        min_threads) {
            debug(1) << "[simple_autoschedule] Perform 1D tiling\n";
            // Fallback to 1D tiling
            Var xo, yo, xi, yi;
            if (options.gpu) {
                // Fuse the rest dimensions and tile on the them
                Var fused_var;
                bool has_extra_dimensions = func.args().size() > 1;
                if (func.args().size() > 1) {
                    int extra_dim_size = 1;
                    for (int i = 0; i < (int)func.args().size(); i++) {
                        if (i == largest_dim) {
                            continue;
                        }
                        extra_dim_size *= int_bounds[i];
                    }
                    if (extra_dim_size >= options.gpu_tile_channel) {
                        bool first = true;
                        for (int i = 0; i < (int)func.args().size(); i++) {
                            if (i == largest_dim) {
                                continue;
                            }
                            if (first) {
                                fused_var = func.args()[i];
                                first = false;
                            } else {
                                func.fuse(fused_var, func.args()[i], fused_var);
                            }
                        }
                    } else {
                        has_extra_dimensions = false;
                    }
                }
                debug(1) << "[simple_autoschedule] has_extra_dimensions:" <<
                    has_extra_dimensions << "\n";
                if (!has_extra_dimensions) {
                    // No fused_vars
                    func.gpu_tile(func.args()[largest_dim],
                        xo, xi, tile_width * tile_height);
                } else {
                    func.reorder(func.args()[largest_dim], fused_var)
                        .gpu_tile(func.args()[largest_dim], fused_var,
                            xo, yo, xi, yi, tile_width * tile_height, tile_channel);
                }
            } else {
                // CPU
                func.split(func.args()[largest_dim],
                           xo, xi, tile_width * tile_height)
                    .parallel(xo)
                    .vectorize(xi, vectorize_width);
            }
            tilable = true;
        } else if (options.gpu) {
            debug(1) << "[simple_autoschedule] Not enough parallelism, still launch GPU tiles.\n";
            // Even if there's not enough parallelism it's still a good idea to launch
            // gpu tiles to avoid memory copy
            if (func.args().size() == 0) {
                func.gpu_single_thread();
            } else {
                // Fuse variables
                Var fused_var = func.args()[0];
                int var_size = int_bounds[0];
                for (int i = 1; i < (int)func.args().size(); i++) {
                    func.fuse(fused_var, func.args()[i], fused_var);
                    var_size *= int_bounds[i];
                }
                // Launch GPU threads
                Var block, thread;
                func.gpu_tile(fused_var, block, thread, std::min(var_size, 32));
            }
        } else {
            debug(1) << "[simple_autoschedule] Not enough parallelism, serialize on CPU.\n";
        }

        // Scheduling the updates
        for (int update_id = 0;
                update_id < func.num_update_definitions(); update_id++) {
            std::vector<ReductionVariable> rvars =
                func.update(update_id).get_schedule().rvars();
            debug(1) << "[simple_autoschedule] Scheduling update " << update_id << ".\n";
            // Compute the largest two dimensions of the reduction variables.
            int rdim_width = -1;
            int rdim_height = -1;
            int largest_rdim = -1;
            bool rvar_tilable = false;
            if (rvars.size() > 0) {
                std::vector<int> rvar_extents;
                rvar_extents.reserve(rvars.size());
                Expr extent = rvars[0].extent;
                for (const auto &param : parameters) {
                    extent = substitute(param.first, Expr(param.second), extent);
                }
                extent = simplify(extent);
                const int64_t *extent_int = as_const_int(extent);
                user_assert(extent_int != nullptr) << "extent:" <<
                    extent << " is not constant.\n";
                debug(1) << "[simple_autoschedule] rvar_extents:\n";
                debug(1) << "[simple_autoschedule] " << (*extent_int) << "\n";
                rvar_extents.push_back(*extent_int);
                for (int arg_id = 1; arg_id < (int)rvars.size(); arg_id++) {
                    Expr extent = rvars[arg_id].extent;
                    for (const auto &param : parameters) {
                        extent = substitute(param.first, Expr(param.second), extent);
                    }
                    extent = simplify(extent);
                    const int64_t *extent_int = as_const_int(extent);
                    user_assert(extent_int != nullptr) << "extent:" <<
                        extent << " is not constant.\n";
                    debug(1) << "[simple_autoschedule] " << (*extent_int) << "\n";
                    rvar_extents.push_back(*extent_int);
                }
                std::vector<int> bounds_rank = sort_indices(rvar_extents);
                if ((int)bounds_rank.size() >= 2) {
                    int last_index = bounds_rank.size() - 1;
                    int dwidth = std::min(bounds_rank[last_index],
                        bounds_rank[last_index-1]);
                    int dheight = std::max(bounds_rank[last_index],
                        bounds_rank[last_index-1]);
                    if (rvar_extents[dwidth] >= tile_width &&
                            rvar_extents[dheight] >= tile_height) {
                        rdim_width = dwidth;
                        rdim_height = dheight;
                    }
                }
                if ((int)bounds_rank.size() >= 1) {
                    if (rvar_extents[bounds_rank.back()] >=
                            tile_width * tile_height) {
                        largest_rdim = bounds_rank.back();
                    }
                }
                debug(1) << "[simple_autoschedule] rdim_width:" <<
                    rdim_width << ", rdim_height:" << rdim_height << "\n";
            }
            // Unroll known, small rvars
            for (int rvar_id = 0; rvar_id < (int)rvars.size(); rvar_id++) {
                if (rvar_id != rdim_width && rvar_id != rdim_height) {
                    Expr extent = rvars[rvar_id].extent;
                    const int64_t *extent_int = as_const_int(extent);
                    if (extent_int != nullptr && *extent_int <= options.unroll_rvar_size) {
                        debug(1) << "[simple_autoschedule] unroll rvars[" << rvar_id << "]\n";
                        func.update(update_id)
                            .unroll(RVar(rvars[rvar_id].var));
                    }
                }
            }
            rvar_tilable = (rdim_width != -1 && rdim_height != -1) ||
                largest_rdim != -1;
            debug(1) << "[simple_autoschedule] rvar_tilable:" << rvar_tilable << "\n";

            // If the domain of the image is small and the reduction is large,
            // use rfactor
            // TODO: gracefully fallback if factorization is impossible
            if (!tilable && rvar_tilable) {
                debug(1) << "[simple_autoschedule] Perform parallel reduction\n";
                if (rdim_width != -1 && rdim_height != -1) {
                    debug(1) << "[simple_autoschedule] 2D parallel reduction\n";
                    // 2D tiling
                    if (options.gpu) {
                        // GPU
                        assert(rdim_width != rdim_height);
                        RVar rx(rvars[rdim_width].var);
                        RVar ry(rvars[rdim_height].var);
                        // Change < 1 to something else for multi-level reduction
                        for (int level = 0; level < 1; level++) {
                            RVar rxo, rxi, ryo, ryi;
                            int size = 32;
                            func.update(update_id)
                                .split(rx, rxo, rxi, size)
                                .split(ry, ryo, ryi, size);
                            Var xi, xo, yo;
                            Func interm = func.update(update_id)
                                              .rfactor({{rxi, xi},
                                                        {rxo, xo},
                                                        {ryo, yo}});
                            std::vector<VarOrRVar> new_order;
                            new_order.push_back(ryi);
                            for (const auto &arg : interm.update_args()) {
                                const Variable *var = arg.as<Variable>();
                                if (var != nullptr &&
                                        !var->reduction_domain.defined() &&
                                        var->name != xi.name() &&
                                        var->name != xo.name() &&
                                        var->name != yo.name()) {
                                    new_order.push_back(Var(var->name));
                                }
                            }
                            new_order.push_back(xi);
                            new_order.push_back(xo);
                            new_order.push_back(yo);
                            Var txo, txi, tyo, tyi;
                            interm.compute_root()
                                  .reorder(xi, xo, yo)
                                  .gpu_blocks(xo, yo)
                                  .gpu_threads(xi);
                            interm.update()
                                  .reorder(new_order)
                                  .gpu_blocks(xo, yo)
                                  .gpu_threads(xi);
                        }
                    } else {
                        // CPU
                        // Parallelize on rxo, ryo, vectorize on rxi
                        RVar rxo, ryo, rxi, ryi;
                        func.update(update_id)
                            .split(RVar(rvars[rdim_width].var), rxo, rxi, tile_width)
                            .split(RVar(rvars[rdim_height].var), ryo, ryi, tile_height);
                        Var xo, yo, xi;
                        Func interm = func.update(update_id)
                                          .rfactor({{rxo, xo},
                                                    {ryo, yo},
                                                    {rxi, xi}});
                        Var tile_index;
                        std::vector<VarOrRVar> new_order;
                        new_order.push_back(ryi);
                        new_order.push_back(xi);
                        for (const auto &arg : interm.update_args()) {
                            const Variable *var = arg.as<Variable>();
                            if (var != nullptr && !var->reduction_domain.defined() &&
                                    var->name != xi.name() && var->name != xo.name() &&
                                    var->name != yo.name()) {
                                new_order.push_back(Var(var->name));
                            }
                        }
                        new_order.push_back(tile_index);
                        interm.compute_root()
                              .fuse(xo, yo, tile_index)
                              .parallel(tile_index)
                              .vectorize(xi);
                        interm.update()
                              .fuse(xo, yo, tile_index)
                              .reorder(new_order)
                              .parallel(tile_index)
                              .vectorize(xi);
                    }
                } else if (largest_rdim != -1) {
                    debug(1) << "[simple_autoschedule] 1D parallel reduction\n";
                    // 1D tiling
                    if (options.gpu) {
                        RVar rx(rvars[largest_rdim].var);
                        // Change < 1 to something else for multi-level reduction
                        for (int level = 0; level < 1; level++) {
                            RVar rxo, rxi, ryo, ryi;
                            int size = tile_width * tile_height;
                            func.update(update_id)
                                .split(rx, rxo, rxi, size)
                                .split(rxi, ryi, rxi, tile_width);
                            Var xi, xo;
                            Func interm = func.update(update_id)
                                              .rfactor({{rxi, xi},
                                                        {rxo, xo}});
                            std::vector<VarOrRVar> new_order;
                            new_order.push_back(ryi);
                            for (const auto &arg : interm.update_args()) {
                                const Variable *var = arg.as<Variable>();
                                if (var != nullptr &&
                                        !var->reduction_domain.defined() &&
                                        var->name != xi.name() &&
                                        var->name != xo.name()) {
                                    new_order.push_back(Var(var->name));
                                }
                            }
                            new_order.push_back(xi);
                            new_order.push_back(xo);
                            Var txo, txi, tyo, tyi;
                            interm.compute_root()
                                  .reorder(xi, xo)
                                  .gpu_blocks(xo)
                                  .gpu_threads(xi);
                            interm.update()
                                  .reorder(new_order)
                                  .gpu_blocks(xo)
                                  .gpu_threads(xi);
                        }
                    } else {
                        // CPU
                        // Parallel on tiles and vectorize inside tile
                        RVar rx(rvars[largest_rdim].var);
                        RVar rxo, rxi, ryi;
                        int size = tile_width * tile_height;
                        func.update(update_id)
                            .split(rx, rxo, rxi, size)
                            .split(rxi, ryi, rxi, tile_width);
                        Var xo, yo, xi;
                        Func interm = func.update(update_id)
                                          .rfactor({{rxo, xo},
                                                    {rxi, xi}});
                        std::vector<VarOrRVar> new_order;
                        new_order.push_back(ryi);
                        new_order.push_back(xi);
                        for (const auto &arg : interm.update_args()) {
                            const Variable *var = arg.as<Variable>();
                            if (var != nullptr && !var->reduction_domain.defined() &&
                                    var->name != xi.name() && var->name != xo.name()) {
                                new_order.push_back(Var(var->name));
                            }
                        }
                        interm.compute_root()
                              .parallel(xo)
                              .vectorize(xi);
                        interm.update()
                              .reorder(new_order)
                              .parallel(xo)
                              .vectorize(xi);
                    }
                }
            }
            std::vector<Expr> update_args = func.update_args(update_id);
            std::vector<Var> pure_args;
            std::vector<int> pure_arg_bounds;
            pure_args.reserve(update_args.size());
            pure_arg_bounds.reserve(update_args.size());
            for (int arg_id = 0; arg_id < (int)update_args.size(); arg_id++) {
                Expr arg = update_args[arg_id];
                const Variable *var = arg.as<Variable>();
                if (var != nullptr &&
                        !var->param.defined() &&
                        !var->image.defined() &&
                        !var->reduction_domain.defined()) {
                    pure_args.push_back(Var(var->name));
                    pure_arg_bounds.push_back(int_bounds[arg_id]);
                }
            }
            int pdim_width = -1;
            int pdim_height = -1;
            std::vector<int> bounds_rank = sort_indices(pure_arg_bounds);
            if ((int)bounds_rank.size() >= 2) {
                int last_index = bounds_rank.size() - 1;
                pdim_width =
                    std::min(bounds_rank[last_index], bounds_rank[last_index-1]);
                pdim_height =
                    std::max(bounds_rank[last_index], bounds_rank[last_index-1]);
            }
            int largest_pdim = -1;
            if (bounds_rank.size() >= 1) {
                largest_pdim = bounds_rank.back();
            }
            debug(1) << "[simple_autoschedule] pdim_width:" << pdim_width << ", "
                << "pdim_height:" << pdim_height << "\n";
            debug(1) << "[simple_autoschedule] largest_pdim:" << largest_pdim << "\n";

            if ((int)pure_arg_bounds.size() >= 2 &&
                     pure_arg_bounds[pdim_width] >= tile_width &&
                     pure_arg_bounds[pdim_height] >= tile_height &&
                    (pure_arg_bounds[pdim_width] / tile_width) *
                    (pure_arg_bounds[pdim_height] / tile_height) >= min_threads) {
                debug(1) << "[simple_autoschedule] Perform 2D tiling\n";
                Var xo, yo, zo, xi, yi, zi;
                if (options.gpu) {
                    // GPU
                    bool first = true;
                    Var fused_var;
                    for (int i = 0; i < (int)pure_args.size(); i++) {
                        if (i == pdim_width || i == pdim_height) {
                            continue;
                        }
                        if (first) {
                            fused_var = pure_args[i];
                            first = false;
                        } else {
                            func.update(update_id)
                                .fuse(fused_var, pure_args[i], fused_var);
                        }
                    }
                    if (first) {
                        // no fused_var
                        func.update(update_id)
                            .reorder(pure_args[pdim_width], pure_args[pdim_height])
                            .gpu_tile(pure_args[pdim_width], pure_args[pdim_height],
                                      xo, yo, xi, yi, tile_width, tile_height);

                    } else {
                        func.update(update_id)
                            .reorder(pure_args[pdim_width], pure_args[pdim_height], fused_var)
                            .gpu_tile(pure_args[pdim_width], pure_args[pdim_height], fused_var,
                                      xo, yo, zo, xi, yi, zi, tile_width, tile_height, tile_channel);
                    }
                } else {
                    // CPU
                    Var tile_index;
                    func.update(update_id)
                        .tile(pure_args[pdim_width], pure_args[pdim_height],
                              xo, yo, xi, yi, tile_width, tile_height,
                              TailStrategy::GuardWithIf)
                        .fuse(xo, yo, tile_index)
                        .parallel(tile_index)
                        .vectorize(xi, vectorize_width);
                }
            } else if ((int)pure_arg_bounds.size() >= 1 &&
                            pure_arg_bounds[largest_pdim] >= (tile_width * tile_height) &&
                            (pure_arg_bounds[largest_pdim] / (tile_width * tile_height)) >=
                            min_threads) {
                debug(1) << "[simple_autoschedule] Perform 1D tiling\n";
                Var xo, yo, xi, yi;
                if (options.gpu) {
                    // GPU
                    bool first = true;
                    Var fused_var;
                    for (int i = 0; i < (int)pure_args.size(); i++) {
                        if (i == largest_pdim) {
                            continue;
                        }
                        if (first) {
                            fused_var = pure_args[i];
                            first = false;
                        } else {
                            func.update(update_id)
                                .fuse(fused_var, pure_args[i], fused_var);
                        }
                    }
                    if (first) {
                        // no fused_var
                        func.update(update_id)
                            .gpu_tile(pure_args[largest_pdim],
                                      xo, xi, tile_width * tile_height);

                    } else {
                        func.update(update_id)
                            .reorder(pure_args[largest_pdim], fused_var)
                            .gpu_tile(pure_args[largest_pdim], fused_var,
                                      xo, yo, xi, yi, tile_width * tile_height, tile_channel);
                    }
                } else {
                    // CPU
                    Var tile_index;
                    func.update(update_id)
                        .split(pure_args[largest_dim],
                               xo, xi, tile_width * tile_height,
                               TailStrategy::GuardWithIf)
                        .parallel(xo)
                        .vectorize(xi, vectorize_width);
                }
            } else if (!options.gpu && pure_args.size() > 0) {
                debug(1) << "[simple_autoschedule] \n" <<
                    "Merging pure variables and parallelize them.\n";
                // On CPU, merge all pure variables and parallelize them
                Var fused_var = pure_args[0];
                for (int i = 1; i < (int)pure_args.size(); i++) {
                    func.update(update_id)
                        .fuse(fused_var, pure_args[i], fused_var);
                }
                func.update(update_id)
                    .parallel(fused_var);
            } else if (options.gpu) {
                debug(1) << "[simple_autoschedule] Parallelizing reduction" <<
                    " using atomics.\n";
                // If the reduction domain is large enough, parallelize the reduction domain
                if (tilable && rvar_tilable) {
                    RVar xo, yo, xi, yi;
                    if (pure_args.size() > 0) {
                        Var zo, zi;
                        Var fused_var;
                        fused_var = pure_args[0];
                        for (int i = 1; i < (int)pure_args.size(); i++) {
                            func.update(update_id)
                                .fuse(fused_var, pure_args[i], fused_var);
                        }
                        func.update(update_id)
                            .allow_race_conditions()
                            .split(RVar(rvars[rdim_width].var), xo, xi, tile_width)
                            .split(RVar(rvars[rdim_height].var), yo, yi, tile_height)
                            .split(fused_var, zo, zi, tile_channel)
                            .reorder(xi, yi, zi, xo, yo, zo)
                            .gpu_blocks(xo, yo, zo)
                            .gpu_threads(xi, yi, zi);
                    } else {
                        func.update(update_id)
                            .allow_race_conditions()
                            .split(RVar(rvars[rdim_width].var), xo, xi, tile_width)
                            .split(RVar(rvars[rdim_height].var), yo, yi, tile_height)
                            .reorder(xi, yi, xo, yo)
                            .gpu_blocks(xo, yo)
                            .gpu_threads(xi, yi);
                    }
                } else {
                    // Even if there's not enough parallelism it's still a good idea to launch
                    // gpu tiles to avoid memory copy
                    if (pure_args.size() == 0) {
                        func.update(update_id).gpu_single_thread();
                    } else {
                        // Fuse variables
                        std::vector<Var> fused_vars;
                        fused_vars.push_back(pure_args[0]);
                        int var_size = pure_arg_bounds[0];
                        for (int i = 1; i < (int)pure_args.size(); i++) {
                            Var new_var;
                            func.update(update_id).fuse(fused_vars.back(), pure_args[i], new_var);
                            fused_vars.push_back(new_var);
                            var_size *= pure_arg_bounds[i];
                        }
                        // Launch GPU threads
                        // TODO: don't fuse when var_size is > 128
                        Var block, thread;
                        func.update(update_id)
                            .gpu_tile(fused_vars.back(), block, thread, std::min(var_size, 128));
                    }
                }
            } else {
                debug(1) << "[simple_autoschedule] Not enough parallelism, " <<
                    "serialize on CPU.\n";
            }

            // Special pattern: if we see f(r.x, r.y, ...) = f(r.x, r.y, ...) + ...
            // we will parallelize over r
            // only for CPU since we use atomics for gpu
            auto is_parallelizable_reduction = [&]() -> bool {
                if (update_args.size() == 0) {
                    return false;
                }
                for (const auto &arg : update_args) {
                    const Variable *var = arg.as<Variable>();
                    if (!(var != nullptr &&
                              !var->param.defined() &&
                              !var->image.defined() &&
                              var->reduction_domain.defined())) {
                        return false;
                    }
                }
                std::vector<Expr> update_vals = func.update_values(update_id).as_vector();
                for (const auto &val : update_vals) {
                    const Add *add = val.as<Add>();
                    if (add == nullptr) {
                        return false;
                    }
                    const Call *call = add->a.as<Call>();
                    if (call == nullptr) {
                        return false;
                    }
                    if (!call->func.defined()) {
                        return false;
                    }
                    Function called_func(call->func);
                    if (called_func.name() != func.name()) {
                        return false;
                    }

                    for (int arg_id = 0; arg_id < (int)call->args.size(); arg_id++) {
                        const Variable *var = call->args[arg_id].as<Variable>();
                        if (!(var != nullptr &&
                                    !var->param.defined() &&
                                    !var->image.defined() &&
                                    var->reduction_domain.defined())) {
                            return false;
                        }
                        const Variable *update_var = update_args[arg_id].as<Variable>();
                        if (var->name != update_var->name) {
                            return false;
                        }
                    }
                }
                return true;
            };

            if (!options.gpu && is_parallelizable_reduction()) {
                debug(1) << "[simple_autoschedule] Parallelize reduction without atomics on CPU\n";
                std::vector<RVar> rvar_args;
                std::vector<int> rvar_arg_bounds;
                for (int arg_id = 0; arg_id < (int)update_args.size(); arg_id++) {
                    const Variable *var = update_args[arg_id].as<Variable>();
                    assert(var != nullptr);
                    rvar_args.push_back(RVar(var->name));
                    assert(var->reduction_domain.defined());
                    ReductionDomain rdom = var->reduction_domain;
                    const auto &domain = rdom.domain();
                    Expr extent = domain[arg_id].extent;
                    for (const auto &param : parameters) {
                        extent = substitute(param.first, Expr(param.second), extent);
                    }
                    extent = simplify(extent);
                    const int64_t *extent_int = as_const_int(extent);
                    user_assert(extent_int != nullptr) << "extent:" << extent <<
                        " is not constant.\n";
                    rvar_arg_bounds.push_back(*extent_int);
                }
                int rdim_width = -1;
                int rdim_height = -1;
                std::vector<int> bounds_rank = sort_indices(rvar_arg_bounds);
                if ((int)int_bounds.size() >= 2) {
                    int last_index = bounds_rank.size() - 1;
                    rdim_width = std::min(bounds_rank[last_index], bounds_rank[last_index-1]);
                    rdim_height = std::max(bounds_rank[last_index], bounds_rank[last_index-1]);
                }

                if ((int)rvar_arg_bounds.size() >= 2 &&
                         rvar_arg_bounds[rdim_width] >= tile_width &&
                         rvar_arg_bounds[rdim_height] >= tile_height &&
                        (rvar_arg_bounds[rdim_width] / tile_width) *
                        (rvar_arg_bounds[rdim_height] / tile_height) >= min_threads) {
                    RVar xo, yo, xi, yi;
                    RVar tile_index;
                    func.update(update_id)
                        .allow_race_conditions()
                        .tile(rvar_args[rdim_width], rvar_args[rdim_height],
                              xo, yo, xi, yi, tile_width, tile_height)
                        .fuse(xo, yo, tile_index)
                        .parallel(tile_index)
                        .vectorize(xi, vectorize_width);
                }
            }
        }
    }
}

void simple_autoschedule(Func &output,
                         const std::map<std::string, Expr> &parameters,
                         const std::vector<std::pair<int, int>> &output_bounds,
                         const SimpleAutoscheduleOptions &options) {
    std::vector<Func> outputs{output};
    std::vector<std::vector<std::pair<int, int>>> vector_output_bounds{output_bounds};
    return simple_autoschedule(outputs,
                               parameters,
                               vector_output_bounds,
                               options);
}

namespace Internal {

void simple_autoschedule_test() {
    // For now we just test whether it compiles or not.
    SimpleAutoscheduleOptions cpu_options;
    Var x("x"), y("y"), z("z");
    { // Simple pointwise operations. Should inline.
        Func in("in");
        in(x, y) = cast<float>(x + y);
        Func f0("f0");
        f0(x, y) = 2.f * in(x, y);
        Func f1("f1");
        f1(x, y) = sin(f0(x, y));
        Func f2("f2");
        f2(x, y) = f1(x, y) * f1(x, y);

        simple_autoschedule(f2,
                            {}, // parameters map
                            {{0, 127},
                             {0, 127}}, // output bounds (min, max)
                            cpu_options);

        Buffer<float> output = f2.realize(128, 128);
    }
    { // 1D convolution. Should just parallize.
        Buffer<float> buf(16384);
        Buffer<float> k(5);
        Func conv("conv");
        RDom r(k);
        conv(x) = 0.f;
        conv(x) += buf(x + r) * k(r);

        simple_autoschedule(conv,
                            {}, // parameters map
                            {{0, 16384 - 6}}, // output bounds (min, max)
                            cpu_options);

        Buffer<float> output = conv.realize(16384 - 5);
    }
    { // 1D convolution in 2D. Should just parallelize the first dimension.
        Buffer<float> buf(16384, 3);
        Buffer<float> k(5);
        Func conv("conv");
        RDom r(k);
        conv(x, y) = 0.f;
        conv(x, y) += buf(x + r, y) * k(r);

        simple_autoschedule(conv,
                            {}, // parameters map
                            {{0, 16384 - 6},
                             {0, 3 - 1}}, // output bounds (min, max)
                            cpu_options);

        Buffer<float> output = conv.realize(16384 - 5, 3);
    }
    { // 2D convolution. Should just parallize.
        Buffer<float> buf(128, 128);
        Buffer<float> k(5, 5);
        Func conv("conv");
        RDom r(k);
        conv(x, y) = 0.f;
        conv(x, y) += buf(x + r.x, y + r.y) * k(r.x, r.y);

        simple_autoschedule(conv,
                            {}, // parameters map
                            {{0, 128 - 6},
                             {0, 128 - 6}}, // output bounds (min, max)
                            cpu_options);

        Buffer<float> output = conv.realize(128 - 5, 128 - 5);
    }
    { // 2D convolution on 3D image. Should just parallelize.
        Buffer<float> buf(128, 128, 16);
        Buffer<float> k(5, 5);
        Func conv("conv");
        RDom r(k);
        conv(x, y, z) = 0.f;
        conv(x, y, z) += buf(x + r.x, y + r.y, z) * k(r.x, r.y);

        debug(1) << "[simple_autoschedule] Test 2D conv in 3D\n.";
        simple_autoschedule(conv,
                            {}, // parameters map
                            {{0, 128 - 6},
                             {0, 128 - 6},
                             {0, 16 - 1},
                             }, // output bounds
                            cpu_options);

        Buffer<float> output = conv.realize(128 - 5, 128 - 5, 16);
    }
    { // 1D reduction onto a scalar. Should perform parallel reduction
        Buffer<float> buf(16384);
        Func sum("sum");
        RDom r(buf);
        sum() += buf(r);

        simple_autoschedule(sum,
                            {}, // parameters map
                            {}, // output bounds (min, max)
                            cpu_options);

        Buffer<float> output = sum.realize();
    }
    { // 2D reduction onto a scalar. Should perform parallel reduction
        Buffer<float> buf(128, 128);
        Func sum("sum");
        RDom r(buf);
        sum() += buf(r.x, r.y);

        simple_autoschedule(sum,
                            {}, // parameters map
                            {}, // output bounds (min, max)
                            cpu_options);

        Buffer<float> output = sum.realize();
    }

    debug(0) << "Simple autoschedule test passed\n";
}

} // namespace Internal

} // namespace Halide
