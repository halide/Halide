#include <algorithm>
#include <regex>

#include "Halide.h"

using namespace std;
using namespace Halide;
using namespace Halide::Internal;

using std::deque;
using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

#define user_assert _halide_user_assert
#define internal_assert _halide_user_assert
#define user_warning std::cerr

namespace {

// Substitute parameter estimates into the exprs describing the box bounds.
void substitute_estimates_box(Box &box) {
    box.used = substitute_var_estimates(box.used);
    for (auto &b : box.bounds) {
        b.min = substitute_var_estimates(b.min);
        b.max = substitute_var_estimates(b.max);
    }
}

// Substitute parameter estimates into the boxes in 'region'.
void substitute_estimates_region(map<string, Box> &region) {
    for (auto &iter : region) {
        substitute_estimates_box(iter.second);
    }
}

bool sort_by_Expr(const pair<string, Expr> &a, const pair<string, Expr> &b) {
    return can_prove(get<1>(a) > get<1>(b));
}

Expr roundUp(Expr num, Expr multiple) {
    if (can_prove(multiple != 1)) {
        Expr numToRound = cast<int>(num);
        if (can_prove(multiple == 0))
            return numToRound;

        Expr remainder = numToRound % multiple;
        if (can_prove(remainder == 0))
            return numToRound;

        return simplify(numToRound + multiple - remainder);
    } else {
        Expr numToRound;
        Expr thresh = make_const(Float(32), 0.5);
        //   cout<<"NUM TO ROUND UP "<<simplify(num)<<endl;
        Expr fraction = num - cast<int>(num);
        Expr truncated = cast<int>(num);
        if (can_prove(fraction >= thresh))
            numToRound = simplify(truncated + 1);
        else
            numToRound = simplify(truncated);
        if (can_prove(numToRound == 0))
            return (make_one(Int(32)));
        //  cout<<"NUM TO ROUND "<<simplify(numToRound)<<endl;
        return (numToRound);
    }
}

string get_expr_str(Expr expr) {
    ostringstream ostr;
    ostr << expr;
    string nst;
    nst = ostr.str();
    nst.erase(std::remove(nst.begin(), nst.end(), '"'), nst.end());
    return nst;
}

Expr roundDown(Expr num, Expr m) {
    Expr n = cast<int>((num));
    Expr result = can_prove(n >= 0) ? (n / m) * m : ((n - m + 1) / m) * m;
    return simplify(result);
}
// Return true if any of the box dimension is unbounded.
bool is_box_unbounded(const Box &b) {
    for (size_t i = 0; i < b.size(); i++) {
        if (!b[i].is_bounded()) {
            return true;
        }
    }
    return false;
}

// Helper function to simplify the upper and lower bounds of each dimension of a
// box.
void simplify_box(Box &b) {
    for (size_t i = 0; i < b.size(); i++) {
        b[i].min = simplify(b[i].min);
        b[i].max = simplify(b[i].max);
    }
}

// Helper function to merge the partial region map into the result region map.
void merge_regions(map<string, Box> &result, const map<string, Box> &partial) {
    // Merge regions from 'partial' with an existing region in 'result'.
    for (const auto &reg : partial) {
        auto iter = result.find(reg.first);
        if (iter == result.end()) {
            result.emplace(reg.first, reg.second);
        } else {
            merge_boxes(iter->second, reg.second);
        }
    }
}

// Replace all occurrences of non-alphanumeric chars in 'name' with '_'.
string get_sanitized_name(string name) {
    if (isdigit(name[0])) {
        name = "_" + name;
    }
    for (size_t i = 0; i < name.size(); ++i) {
        if (!isalnum(name[i])) {
            name[i] = '_';
        }
    }
    return name;
}

// Representation of the gpu arch constants per CC
struct GPU_Params {

    Expr max_regs_per_thread;
    Expr total_regs_per_SM;
    Expr max_regs_per_block;
    Expr limit_threads_per_warp;
    Expr min_shared_mem_unit;
    Expr limit_warps_per_SM;
    Expr max_blocks_per_SM;
    Expr limit_shared_mem_per_SM;
    Expr limit_shared_mem_per_block;
    Expr limit_threads_per_SM;
    Expr limit_threads_per_block;
    Expr n_SM;
    Expr warp_alloc_granularity;
    Expr reg_alloc_unit_size;

    /*const Expr &max_regs_per_thread=make_const(Float(64),64);
  const Expr &total_regs_per_SM=make_const(Float(64),65536);
  const Expr &max_regs_per_block=make_const(Float(64),65536);
  const Expr &limit_threads_per_warp=make_const(Float(64),32);
  const Expr &min_shared_mem_unit=make_const(Float(64),256);
            //const Expr &reg_alloc_unit_size=make_const(Float(64),256);
  const Expr &limit_warps_per_SM=make_const(Float(64),64);
  const Expr &max_blocks_per_SM=make_const(Float(64),16);
  const Expr &limit_shared_mem_per_SM=make_const(Float(64),49152);
  const Expr &limit_threads_per_SM=make_const(Float(64),2048);
  const Expr &n_SM=make_const(Int(32),arch_params.parallelism);
  const Expr &warp_alloc_granularity=make_const(Float(32),4);
  const Expr &reg_alloc_unit_size=make_const(Float(32),256);*/
};

// Representation of a function stage in the pipeline.
struct FStage {
    std::map<string, Expr> vars;
    std::vector<string> producers;
    Expr compute_level;
    Expr compute_stage;
    std::map<string, map<string, Expr>> re;
    bool is_input = 1;
    bool output;
    string name;
    string statement;
    string costf;
    double cost;
    double rcost;
    std::vector<string> cols;
    double buffer;
    bool store_inter = false;
    bool compute_inter = false;
    bool is_inline = false;
    bool is_root = false;

    std::set<string> rvars;
    std::map<string, int> deps;
    std::map<string, int> fused_order = {};
    std::map<string, int> var_order;
    std::map<string, int> strides;
    std::map<string, float> strided_access;

    Function func;
    uint32_t stage_num;
    FStage(Function func, uint32_t stage_num)
        : func(func), stage_num(stage_num) {
    }

    bool operator==(const FStage &other_stage) const {
        return (func.name() == other_stage.func.name()) &&
               (stage_num == other_stage.stage_num);
    }

    bool operator<(const FStage &other_stage) const {
        return func.name() < other_stage.func.name() ||
               ((func.name() == other_stage.func.name()) &&
                (stage_num < other_stage.stage_num));
    }

    friend std::ostream &operator<<(std::ostream &stream, const FStage &s) {
        if (s.stage_num == 0) {
            stream << s.func.name();
        } else {
            stream << s.func.name() << ".update(" << (s.stage_num - 1) << ")";
        }
        return stream;
    }
};

// Check if all the pipeline outputs have estimates specified
// on each of their dimensions; otherwise, throw an assertion.
void check_estimates_on_outputs(const vector<Function> &outputs) {
    for (const auto &out : outputs) {
        const vector<Bound> &estimates = out.schedule().estimates();
        // Check if the estimate for each dimension of the output is available
        // and is an integer. If there are duplicates for the estimate of a
        // dimension, we only check the last defined estimate (which min and
        // extent values are defined) since it is the one that would be
        // eventually used.
        Bound est;
        for (const auto &arg : out.args()) {
            bool found = false;
            for (int i = (int)estimates.size() - 1; i >= 0; --i) {
                if ((estimates[i].var == arg) && estimates[i].min.defined() &&
                    estimates[i].extent.defined()) {
                    found = true;
                    est = estimates[i];
                    break;
                }
            }
            user_assert(found && est.min.type().is_int() &&
                        est.extent.type().is_int())
                << "Please provide a valid estimate for dimension " << arg
                << " of output \"" << out.name() << "\"\n";
        }
    }
}

struct DependenceAnalysis {
    // Map containing all the functions in the pipeline.
    map<string, Function> env;
    vector<string> order;
    FuncValueBounds func_val_bounds;

    struct RegionsRequiredQuery {
        string f;
        int stage;
        set<string> prods;
        bool only_regions_computed;

        RegionsRequiredQuery(const string &f, int stage, const set<string> &prods,
                             bool only_regions_computed)
            : f(f), stage(stage), prods(prods),
              only_regions_computed(only_regions_computed) {
        }

        bool operator==(const RegionsRequiredQuery &other) const {
            return (f == other.f) && (stage == other.stage) &&
                   (prods == other.prods) &&
                   (only_regions_computed == other.only_regions_computed);
        }
        bool operator<(const RegionsRequiredQuery &other) const {
            if (f < other.f) {
                return true;
            } else if (f > other.f) {
                return false;
            }
            if (stage < other.stage) {
                return true;
            } else if (stage > other.stage) {
                return false;
            }
            if (only_regions_computed < other.only_regions_computed) {
                return true;
            } else if (only_regions_computed > other.only_regions_computed) {
                return false;
            }
            return prods < other.prods;
        }
    };
    struct RegionsRequired {
        DimBounds bounds;
        // Regions required to compute 'bounds' given a particular
        // RegionsRequiredQuery.
        map<string, Box> regions;
        RegionsRequired(const DimBounds &b, const map<string, Box> &r)
            : bounds(b), regions(r) {
        }
    };
    // Cache for bounds queries (bound queries with the same parameters are
    // common during the grouping process).
    map<RegionsRequiredQuery, vector<RegionsRequired>> regions_required_cache;

    DependenceAnalysis(const map<string, Function> &env,
                       const vector<string> &order,
                       const FuncValueBounds &func_val_bounds)
        : env(env), order(order), func_val_bounds(func_val_bounds) {
    }

    // Return the regions of the producers ('prods') required to compute the
    // region of the function stage ('f', 'stage_num') specified by 'bounds'. When
    // 'only_regions_computed' is set to true, this only returns the computed
    // regions and not the total allocated regions.
    map<string, Box> regions_required(Function f, int stage_num,
                                      const DimBounds &bounds,
                                      const set<string> &prods,
                                      bool only_regions_computed,
                                      const Scope<Interval> *input_estimates);

    // Return the regions of the producers ('prods') required to compute the
    // region of the function specified by 'pure_bounds'. When
    // 'only_regions_computed' is set to true, this only returns the computed
    // regions and not the total allocated regions.
    map<string, Box> regions_required(Function f, const DimBounds &pure_bounds,
                                      const set<string> &prods,
                                      bool only_regions_computed,
                                      const Scope<Interval> *input_estimates);

    // Return redundantly computed regions of producers ('prods') while computing
    // a region of the function stage ('f', 'stage_num') specified by 'bounds'.
    // 'var' is the dimension along which redundant computation is accounted for.
    // When 'only_regions_computed' is set to true, this only returns the computed
    // regions and not the total allocated regions. When 'only_regions_computed'
    // is set to true, this only returns the computed regions and not the total
    // allocated regions.
    map<string, Box> redundant_regions(Function f, int stage_num, string var,
                                       const DimBounds &bounds,
                                       const set<string> &prods,
                                       bool only_regions_computed,
                                       const Scope<Interval> *input_estimates);

    // Return overlapping regions of producers ('prods') while computing a
    // function stage along each of the dimensions.
    vector<map<string, Box>>
    overlap_regions(Function f, int stage_num, const DimBounds &bounds,
                    const set<string> &prods, bool only_regions_computed,
                    const Scope<Interval> *input_estimates);
};

// Return the regions of the producers ('prods') required to compute the region
// of the function specified by 'pure_bounds'.
map<string, Box> DependenceAnalysis::regions_required(
    Function f, const DimBounds &pure_bounds, const set<string> &prods,
    bool only_regions_computed, const Scope<Interval> *input_estimates) {
    // Find the regions required for each stage and merge them.
    map<string, Box> regions;
    int num_stages = f.updates().size() + 1;
    for (int s = 0; s < num_stages; s++) {
        DimBounds bounds = get_stage_bounds(f, s, pure_bounds);
        map<string, Box> stage_regions = regions_required(
            f, s, bounds, prods, only_regions_computed, input_estimates);

        merge_regions(regions, stage_regions);
    }
    return regions;
}

struct StageBounds {
    FStage f_stage;
    DimBounds bounds;

    StageBounds(const FStage &fs, const DimBounds &b)
        : f_stage(fs), bounds(b) {
    }
    StageBounds(Function func, uint32_t stage_num, const DimBounds &b)
        : f_stage(FStage(func, stage_num)), bounds(b) {
    }

    bool operator==(const StageBounds &other) const {
        return (f_stage == other.f_stage) && (bounds == other.bounds);
    }
    bool operator<(const StageBounds &other) const {
        return (f_stage < other.f_stage) || ((f_stage == other.f_stage) &&
                                             (bounds.size() < other.bounds.size()));
    }
    friend std::ostream &operator<<(std::ostream &stream, const StageBounds &s) {
        stream << "Stage: " << s.f_stage << "\n";
        stream << "Bounds:\n";
        for (const auto &iter : s.bounds) {
            stream << "\t" << iter.first << " -> [" << iter.second.min << ", "
                   << iter.second.max << "]\n";
        }
        stream << "\n";
        return stream;
    }
};

// Helper function to queue regions that need to be traversed. 'fs_bounds' is
// the queue into which the regions specified by 'prod_func' and 'region'
// will be added.
void queue_func_regions(map<FStage, DimBounds> &fs_bounds,
                        const Function &prod_func, const Box &region,
                        const set<StageBounds> &visited) {
    DimBounds prod_pure_bounds;
    const vector<string> &args = prod_func.args();

    internal_assert(region.size() == args.size());

    // The region only specifies the extent of each dimension
    // by position. Populating a map which is keyed by name.
    for (size_t v = 0; v < args.size(); v++) {
        prod_pure_bounds[args[v]] = region[v];
    }

    // Get the bounds of all stages in a function from the
    // bounds on the pure dimensions.
    vector<DimBounds> prod_bounds = get_stage_bounds(prod_func, prod_pure_bounds);

    size_t num_stages = prod_func.updates().size() + 1;

    internal_assert(prod_bounds.size() == num_stages);

    // Add all stages of a function into the queue.
    for (size_t prod_s = 0; prod_s < num_stages; prod_s++) {
        StageBounds sb(prod_func, prod_s, prod_bounds[prod_s]);
        if (visited.find(sb) == visited.end()) {
            auto iter = fs_bounds.find(sb.f_stage);
            if (iter == fs_bounds.end()) {
                fs_bounds.emplace(sb.f_stage, sb.bounds);
            } else {
                for (const auto &b : sb.bounds) {
                    DimBounds &curr_bounds = iter->second;
                    auto b_iter = curr_bounds.find(b.first);
                    if (b_iter == curr_bounds.end()) {
                        curr_bounds.emplace(b.first, b.second);
                    } else {
                        if (b_iter->second.has_lower_bound() &&
                            b.second.has_lower_bound()) {
                            b_iter->second.min = simplify(
                                Interval::make_min(b_iter->second.min, b.second.min));
                        } else {
                            b_iter->second.min = Interval::neg_inf();
                        }

                        if (b_iter->second.has_upper_bound() &&
                            b.second.has_upper_bound()) {
                            b_iter->second.max = simplify(
                                Interval::make_max(b_iter->second.max, b.second.max));
                        } else {
                            b_iter->second.max = Interval::pos_inf();
                        }
                    }
                }
            }
        }
    }
}

// Helper function for merging 'curr_regions' to the global map of regions
// and adding them to the queue of regions that need to be traversed.
// 'prods' is the set of producer functions that are under consideration.
void merge_and_queue_regions(map<FStage, DimBounds> &fs_bounds,
                             map<string, Box> &regions,
                             map<string, Box> &curr_regions,
                             const set<string> &prods,
                             const map<string, Function> &env,
                             bool only_regions_computed, string curr_func_name,
                             const set<StageBounds> &visited) {
    for (const auto &reg : curr_regions) {
        // Merge region with an existing region of a function in the
        // global map. Do not merge the parent function itself to the region
        // when querying only for the values computed.
        if (!only_regions_computed ||
            (only_regions_computed && (reg.first != curr_func_name))) {
            auto iter = regions.find(reg.first);
            if (iter == regions.end()) {
                regions.emplace(reg.first, reg.second);
            } else {
                merge_boxes(iter->second, reg.second);
            }
        }

        // Skip adding the current region into to the queue if the function
        // is not in 'prods'.
        if (prods.find(reg.first) == prods.end()) {
            continue;
        }

        const auto &it = env.find(reg.first);
        if ((it != env.end()) && (reg.first != curr_func_name)) {
            // Add all stages of the function representing the
            // region into the queue.
            queue_func_regions(fs_bounds, it->second, reg.second, visited);
        }
    }
}

// Return the regions of the producers ('prods') required to compute the region
// of the function stage ('f', 'stage_num') specified by 'bounds'.
map<string, Box> DependenceAnalysis::regions_required(
    Function f, int stage_num, const DimBounds &bounds,
    const set<string> &prods, bool only_regions_computed,
    const Scope<Interval> *input_estimates) {
    // Iteratively compute the required regions by traversing the chain
    // of dependencies.

    // Check the cache if we've already computed this previously.
    RegionsRequiredQuery query(f.name(), stage_num, prods, only_regions_computed);
    const auto &iter = regions_required_cache.find(query);
    if (iter != regions_required_cache.end()) {
        const auto &it = std::find_if(
            iter->second.begin(), iter->second.end(),
            [&bounds](const RegionsRequired &r) { return (r.bounds == bounds); });
        if (it != iter->second.end()) {
            internal_assert((iter->first == query) && (it->bounds == bounds));
            return it->regions;
        }
    }

    // Map of all the required regions.
    map<string, Box> regions;
    map<FStage, DimBounds> fs_bounds;
    set<StageBounds> visited;

    // Add the query function and its region to the queue.
    fs_bounds.emplace(FStage(f, stage_num), bounds);

    while (!fs_bounds.empty()) {
        for (int i = order.size() - 1; i >= 0; --i) {
            const Function &f = env.find(order[i])->second;
            int num_stages = f.updates().size() + 1;
            for (int stage_num = 0; stage_num < num_stages; ++stage_num) {
                FStage s(f, stage_num);

                const auto &iter = fs_bounds.find(s);
                if (iter == fs_bounds.end()) {
                    continue;
                }

                DimBounds curr_bounds = iter->second;
                visited.insert(StageBounds(s, curr_bounds));

                // Scope for containing all the estimates on parameters and intervals.
                Scope<Interval> curr_scope;
                curr_scope.set_containing_scope(input_estimates);

                // If the function has an extern definition, there is no visibility into
                // the expression defining the function. So the regions required will be
                // the entire domain of the inputs to the extern func. Use the estimates
                // on the inputs to the extern function if available.
                //
                // TODO: Query the extern function for bounds of the functions which it
                // it depends on. This can be done by calling the extern func in the
                // bounds query mode.
                if (s.func.has_extern_definition()) {
                    for (const ExternFuncArgument &arg : s.func.extern_arguments()) {
                        if (arg.is_func()) {
                            // If the argument is an entire function, the bounds of the
                            // function required are unknown. Create an infinite region
                            // of the correct dimension, update the region map, and
                            // add it to the queue.
                            string prod_name = Function(arg.func).name();
                            const Function &prod_func = get_element(env, prod_name);
                            map<string, Box> prod_reg;
                            const vector<string> &args = prod_func.args();
                            for (size_t v = 0; v < args.size(); v++) {
                                prod_reg[prod_name].push_back(Interval());
                            }
                            merge_and_queue_regions(fs_bounds, regions, prod_reg, prods, env,
                                                    only_regions_computed, s.func.name(),
                                                    visited);
                        } else if (arg.is_expr()) {
                            // Find the boxes required for the expression and add the regions
                            // to the queue.
                            Expr subs_arg = substitute_var_estimates(arg.expr);
                            map<string, Box> arg_regions =
                                boxes_required(subs_arg, curr_scope, func_val_bounds);
                            substitute_estimates_region(arg_regions);
                            merge_and_queue_regions(fs_bounds, regions, arg_regions, prods,
                                                    env, only_regions_computed, s.func.name(),
                                                    visited);
                        } else if (arg.is_image_param() || arg.is_buffer()) {
                            // If the argument is an image or a buffer, the required
                            // bounds are unknown. Create an infinite region of the
                            // correct dimension and update the region map.
                            Buffer<> buf;
                            if (arg.is_image_param()) {
                                buf = arg.image_param.buffer();
                            } else {
                                buf = arg.buffer;
                            }
                            map<string, Box> buf_reg;
                            for (int v = 0; v < buf.dimensions(); v++) {
                                buf_reg[buf.name()].push_back(Interval());
                            }
                            merge_regions(regions, buf_reg);
                        }
                    }
                } else {
                    Definition def = get_stage_definition(s.func, s.stage_num);
                    const vector<Dim> &dims = def.schedule().dims();

                    // Substitute parameter estimates into the bounds and add them to the
                    // current scope.
                    for (int d = 0; d < (int)dims.size() - 1; d++) {
                        Interval simple_bounds = get_element(curr_bounds, dims[d].var);
                        simple_bounds.min = substitute_var_estimates(simple_bounds.min);
                        simple_bounds.max = substitute_var_estimates(simple_bounds.max);
                        curr_scope.push(dims[d].var, simple_bounds);
                    }

                    // Find the regions required for each value of the current function
                    // stage, update the region map, and add them to the queue.
                    for (const auto &val : def.values()) {
                        // Substitute the parameter estimates into the expression and get
                        // the regions required for the expression.
                        Expr subs_val = substitute_var_estimates(val);
                        map<string, Box> curr_regions =
                            boxes_required(subs_val, curr_scope, func_val_bounds);
                        substitute_estimates_region(curr_regions);

                        // Arguments to the definition may require regions of functions.
                        // For example, update definitions in histograms where the bin is
                        // based on the value of a function.
                        Box left_reg;
                        for (const Expr &arg : def.args()) {
                            Expr subs_arg = substitute_var_estimates(arg);
                            map<string, Box> arg_regions =
                                boxes_required(subs_arg, curr_scope, func_val_bounds);
                            substitute_estimates_region(arg_regions);

                            // Merge the regions with the regions found while looking at
                            // the values.
                            merge_regions(curr_regions, arg_regions);

                            Interval arg_bounds =
                                bounds_of_expr_in_scope(arg, curr_scope, func_val_bounds);
                            left_reg.push_back(arg_bounds);
                        }

                        auto iter_curr = curr_regions.find(s.func.name());
                        if (iter_curr == curr_regions.end()) {
                            curr_regions.emplace(s.func.name(), left_reg);
                        } else {
                            merge_boxes(iter_curr->second, left_reg);
                        }

                        // Update the region map, and add 'curr_regions' to the queue.
                        merge_and_queue_regions(fs_bounds, regions, curr_regions, prods,
                                                env, only_regions_computed, s.func.name(),
                                                visited);
                    }
                }

                // Remove processed region from the queue.
                fs_bounds.erase(iter);
            }
        }
    }

    // Simplify the bounds on each region and substitute global pipeline
    // bounds for function regions which lower and upper bounds could not be
    // determined.
    map<string, Box> concrete_regions;

    for (auto &f_reg : regions) {
        simplify_box(f_reg.second);

        Box concrete_box;
        for (size_t i = 0; i < f_reg.second.size(); i++) {
            Expr lower = f_reg.second[i].min;
            Expr upper = f_reg.second[i].max;

            auto iter = env.find(f_reg.first);
            bool in_env = (iter != env.end());

            if (!lower.as<IntImm>() && in_env) {
                const Function &curr_f = iter->second;
                for (const auto &b : curr_f.schedule().estimates()) {
                    size_t num_pure_args = curr_f.args().size();
                    if ((i < num_pure_args) && (b.var == curr_f.args()[i])) {
                        lower = b.min;
                    }
                }
            }

            if (!upper.as<IntImm>() && in_env) {
                const Function &curr_f = iter->second;
                for (const auto &b : curr_f.schedule().estimates()) {
                    size_t num_pure_args = curr_f.args().size();
                    if ((i < num_pure_args) && (b.var == curr_f.args()[i])) {
                        const IntImm *bmin = b.min.as<IntImm>();
                        const IntImm *bextent = b.extent.as<IntImm>();
                        upper = IntImm::make(Int(32), bmin->value + bextent->value - 1);
                    }
                }
            }

            Interval concrete_bounds = Interval(lower, upper);
            concrete_box.push_back(concrete_bounds);
        }
        concrete_regions[f_reg.first] = concrete_box;
    }

    regions_required_cache[query].push_back(
        RegionsRequired(bounds, concrete_regions));
    return concrete_regions;
}

// Return redundantly computed regions of producers ('prods') while computing a
// region of the function stage ('f', 'stage_num') specified by 'bounds'. 'var'
// is the dimension along which redundant computation is accounted for.
map<string, Box> DependenceAnalysis::redundant_regions(
    Function f, int stage_num, string var, const DimBounds &bounds,
    const set<string> &prods, bool only_regions_computed,
    const Scope<Interval> *input_estimates) {
    // Find the regions required to compute the region of 'f' specified
    // by 'bounds'.
    map<string, Box> regions = regions_required(
        f, stage_num, bounds, prods, only_regions_computed, input_estimates);

    // Shift the bounds by the size of the interval along the direction
    // of var.
    DimBounds shifted_bounds;

    for (const auto &b : bounds) {
        if (b.first == var) {
            Expr len = b.second.max - b.second.min + 1;
            Interval bound = Interval(b.second.min + len, b.second.max + len);
            shifted_bounds[b.first] = bound;
        } else {
            shifted_bounds[b.first] = b.second;
        }
    }

    // Find the regions required to compute the region of f specified
    // by shifted_bounds.
    map<string, Box> regions_shifted =
        regions_required(f, stage_num, shifted_bounds, prods,
                         only_regions_computed, input_estimates);

    // Compute the overlaps between 'regions_shifted' and the original
    // regions required.
    map<string, Box> overlaps;
    for (const auto &reg : regions) {
        auto iter = regions_shifted.find(reg.first);
        if (iter == regions.end()) {
            // It will be interesting to log cases where this actually happens
            // i.e., the shifted regions do not contain a function that was
            // there in the original regions.
            continue;
        }
        const Box &b = reg.second;
        const Box &b_shifted = iter->second;
        // The boxes should be of the same size.
        internal_assert(b.size() == b_shifted.size());

        Box b_intersect;
        for (uint32_t i = 0; i < b.size(); i++) {
            b_intersect.push_back(Interval::make_intersection(b[i], b_shifted[i]));
        }
        // A function should appear once in the regions and therefore cannot
        // already be present in the overlaps map.
        internal_assert(overlaps.find(reg.first) == overlaps.end());
        overlaps.emplace(reg.first, b_intersect);
    }

    // Simplify the bounds of each of the overlap regions.
    for (auto &f : overlaps) {
        simplify_box(f.second);
    }

    return overlaps;
}

// Return overlapping regions of producers ('prods') while computing a function
// stage along each of the dimensions.
vector<map<string, Box>> DependenceAnalysis::overlap_regions(
    Function f, int stage_num, const DimBounds &bounds,
    const set<string> &prods, bool only_regions_computed,
    const Scope<Interval> *input_estimates) {
    vector<map<string, Box>> conc_overlaps;

    const vector<Dim> &dims = get_stage_dims(f, stage_num);

    // Get the redundant regions along each dimension of f.
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        map<string, Box> conc_reg =
            redundant_regions(f, stage_num, dims[d].var, bounds, prods,
                              only_regions_computed, input_estimates);
        conc_overlaps.push_back(conc_reg);
    }
    return conc_overlaps;
}

// Return the regions of each function required for computing the
// outputs of the pipeline.
map<string, Box> get_pipeline_bounds(DependenceAnalysis &analysis,
                                     const vector<Function> &outputs,
                                     const Scope<Interval> *input_estimates) {
    map<string, Box> pipeline_bounds;

    // Find the regions required for each of the outputs and merge them
    // to compute the full pipeline_bounds.
    for (const auto &out : outputs) {
        DimBounds pure_bounds;
        Box out_box;
        // Use the estimates on the output for determining the output bounds.
        // If there are duplicates, use the most recent estimate.
        const auto &estimates = out.schedule().estimates();
        for (const auto &arg : out.args()) {
            int i;
            for (i = estimates.size() - 1; i >= 0; --i) {
                const auto &est = estimates[i];
                if ((est.var == arg) && est.min.defined() && est.extent.defined()) {
                    Interval in = Interval(est.min, simplify(est.min + est.extent - 1));
                    pure_bounds.emplace(arg, in);
                    out_box.push_back(in);
                    break;
                }
            }
            internal_assert(i >= 0) << "Could not find estimate for " << arg << "\n";
        }

        set<string> prods;
        for (const pair<string, Function> &fpair : analysis.env) {
            prods.insert(fpair.first);
        }

        map<string, Box> regions = analysis.regions_required(
            out, pure_bounds, prods, false, input_estimates);

        // Add the output region to the pipeline bounds as well.
        regions.emplace(out.name(), out_box);

        merge_regions(pipeline_bounds, regions);
    }

    return pipeline_bounds;
}

struct AutoSchedule {
    struct Stage {
        string function;
        size_t stage;

        Stage(const string &f, size_t s)
            : function(f), stage(s) {
        }

        bool operator==(const Stage &other) const {
            return (function == other.function) && (stage == other.stage);
        }
        bool operator<(const Stage &other) const {
            return (function < other.function) ||
                   ((function == other.function) && (stage < other.stage));
        }
    };

    const map<string, Function> &env;

    // Contain maps from function name to the topological order of the pipeline.
    map<string, size_t> topological_order;

    // Cache for storing all internal vars/rvars that have been declared during
    // the course of schedule generation, to ensure that we don't introduce any
    // duplicates in the string representation of the schedules.
    map<string, VarOrRVar> internal_vars;

    // Store the list of schedules applied to some function stages (most recent
    // schedule is placed last in the list).
    map<string, map<int, vector<string>>> func_schedules;

    // Store the list of vars/rvars used in the schedule applied to some
    // function stages.
    map<string, map<int, set<string>>> used_vars;

    AutoSchedule(const map<string, Function> &env, const vector<string> &order)
        : env(env) {
        for (size_t i = 0; i < order.size(); ++i) {
            topological_order.emplace(order[i], i);
        }
        // Allocate a slot in 'used_vars' for each function stages in the pipeline
        for (const auto &iter : env) {
            for (size_t i = 0; i < iter.second.updates().size() + 1; ++i) {
                used_vars[iter.first][i];
            }
        }
    }

    size_t get_func_index(const string &name) const {
        size_t index = get_element(topological_order, name);
        return index;
    }
    // Given a function name, return a string representation of getting the
    // function handle
    string get_func_handle(const string &name) const {
        size_t index = get_element(topological_order, name);
        return "pipeline.get_func(" + std::to_string(index) + ")";
    }

    friend std::ostream &operator<<(std::ostream &stream,
                                    const AutoSchedule &sched) {
        stream << "// Delete this line if not using Generator\n";
        stream << "Pipeline pipeline = get_pipeline();\n\n";

        for (const auto &iter : sched.internal_vars) {
            if (iter.second.is_rvar) {
                stream << "RVar ";
            } else {
                stream << "Var ";
            }
            stream << iter.first << "(\"" << iter.first << "\");\n";
        }
        stream << "\n";

        // Declare all the functions + schedules
        std::ostringstream func_ss;
        std::ostringstream schedule_ss;

        for (const auto &f : sched.func_schedules) {
            const string &fname = get_sanitized_name(f.first);
            func_ss << "Func " << fname << " = " << sched.get_func_handle(f.first)
                    << ";\n";

            schedule_ss << "{\n";

            // Declare all the Vars and RVars that are actually used in the schedule
            const Function &func = get_element(sched.env, f.first);
            for (size_t i = 0; i < func.args().size(); ++i) {
                if (sched.used_vars.at(func.name()).at(0).find(func.args()[i]) !=
                    sched.used_vars.at(func.name()).at(0).end()) {
                    schedule_ss << "    Var " << func.args()[i] << " = " << fname
                                << ".args()[" << i << "];\n";
                }
            }
            set<string> declared_rvars;
            for (size_t i = 0; i < func.updates().size(); ++i) {
                const vector<ReductionVariable> &rvars =
                    func.updates()[i].schedule().rvars();
                const set<string> &var_list = sched.used_vars.at(func.name()).at(i + 1);
                for (size_t j = 0; j < rvars.size(); ++j) {
                    if ((var_list.find(rvars[j].var) == var_list.end()) ||
                        (declared_rvars.find(rvars[j].var) != declared_rvars.end())) {
                        continue;
                    }
                    declared_rvars.insert(rvars[j].var);
                    schedule_ss << "    RVar " << rvars[j].var << "(" << fname
                                << ".update(" << i << ").get_schedule().rvars()[" << j
                                << "].var);\n";
                }
            }

            for (const auto &s : f.second) {
                internal_assert(!s.second.empty());
                schedule_ss << "    " << fname;
                if (s.first > 0) {
                    schedule_ss << ".update(" << std::to_string(s.first - 1) << ")";
                }
                for (size_t i = 0; i < s.second.size(); ++i) {
                    schedule_ss << "\n        ." << s.second[i];
                }
                schedule_ss << ";\n";
            }

            schedule_ss << "}\n";
        }

        stream << func_ss.str() << "\n";
        stream << schedule_ss.str() << "\n";

        return stream;
    }

    void push_schedule(const string &stage_name, size_t stage_num,
                       const string &sched, const set<string> &vars) {
        vector<string> v = split_string(stage_name, ".");
        internal_assert(!v.empty());

        used_vars[v[0]][stage_num].insert(vars.begin(), vars.end());

        // If the previous schedule applied is the same as this one,
        // there is no need to re-apply the schedule
        auto &schedules = func_schedules[v[0]][stage_num];
        if (schedules.empty()) {
            schedules.push_back(sched);
        } else {
            if (schedules[schedules.size() - 1] != sched) {
                schedules.push_back(sched);
            }
        }
    }
};

// Implement the grouping algorithm and the cost model for making the grouping
// choices.
struct Partitioner {
    // GroupingChoice encodes the grouping of the 'prod' function into the 'cons'
    // stage.
    struct GroupingChoice {
        string prod;
        FStage cons;

        GroupingChoice(const string &prod, const FStage &cons)
            : prod(prod), cons(cons) {
        }

        bool operator==(const GroupingChoice &other) const {
            return (prod == other.prod) && (cons == other.cons);
        }

        bool operator<(const GroupingChoice &other) const {
            return (prod < other.prod) ||
                   ((prod == other.prod) && (cons < other.cons));
        }

        friend std::ostream &operator<<(std::ostream &stream,
                                        const GroupingChoice &choice) {
            stream << "Choice: " << choice.prod << " -> " << choice.cons << '\n';
            return stream;
        }
    };
    int total_inlines;

    // A group is a sub-pipeline with a single output. Members of a group are
    // either inlined into the consumer functions within the group or computed
    // at tiles of the output, specified by 'tile_sizes'.
    //
    // TODO: The restriction of computing either at the inline or tile level
    // makes the space of scheduling choices for a group very tractable.
    // However, the restriction might miss good schedules which can only be
    // realized by computing the members of the group at different levels of
    // the group.
    //
    // There are two approaches to extend the space of schedules considered:
    // 1) Recursive grouping: Treat the problem of determining the compute levels
    // within a group as a smaller instance of the grouping problem with
    // different parameters for the input, output sizes, and cache model.
    //
    // 2) Tightening: Always compute a function at the lowest level possible
    // without introducing redundant work. This is a restricted form of recursive
    // grouping which does not explore the trade-off between redundant work and
    // locality.
    //
    // Either approach can be implemented as a post process for each group
    // after the initial grouping process finishes. The cost model may
    // already make sub-optimal higher level partitioning when it is not aware
    // of the benefits of the post processing. However, it should strictly be
    // an improvement over the initial grouping. As a first step, it is good
    // to make it a post process.
    //
    // Incorporating the recursive grouping process into the cost model can be
    // tricky and can potentially make the cost of analyzing a group
    // prohibitive, as it requires solving smaller instances of the grouping
    // problem for analyzing each configuration. On the other hand, tightening
    // can be integrated into the cost model with out significantly increasing
    // the time to analyze a grouping configuration.
    //
    // TODO: Add sliding window optimizations. For start, it may be enough to
    // implement sliding window as a post-pass by moving the store level of all
    // the members of the group to the outermost serial loop. This could possibly
    // be incorporated in the cost model with some effort. Line-buffering
    // presents additional challenges for this post-processing strategy though.
    // A typical line-buffer would use terrible tile size for tiling, but its
    // performance will improve significantly once sliding window is turned on.
    //
    // TODO: Register tiling is an important transformation especially for
    // benchmarks with significant reuse of the data (like matrix multiply and
    // convolutional layers). The mechanism for realizing register tiling is to
    // completely unroll small tiles of the innermost kernels. Unrolling
    // interacts with vectorization, storage layout, and depends on the outer
    // level tiling.
    struct Group {
        // The output stage representing the group.
        FStage output;
        // Functions that belong to the group.
        vector<FStage> members;
        // Members of the group which are inlined.
        set<string> inlined;
        // Tile sizes along dimensions of the output function of the group.
        map<string, Expr> tile_sizes;

        Group(const FStage &output, const vector<FStage> &members)
            : output(output), members(members) {
        }

        friend std::ostream &operator<<(std::ostream &stream, const Group &g) {
            stream << "Output FStage: " << g.output << '\n';
            stream << "Members: " << '{';
            for (size_t i = 0; i < g.members.size(); ++i) {
                if (i > 0) {
                    stream << ", ";
                }
                stream << g.members[i];
            }
            stream << "}" << '\n';

            stream << "Inlined: " << '{';
            for (auto iter = g.inlined.begin(); iter != g.inlined.end(); ++iter) {
                if (std::distance(g.inlined.begin(), iter) > 0) {
                    stream << ", ";
                }
                stream << *iter;
            }
            stream << "}" << '\n';

            stream << "Tile sizes: "
                   << "{";
            for (auto iter = g.tile_sizes.begin(); iter != g.tile_sizes.end();
                 ++iter) {
                if (std::distance(g.tile_sizes.begin(), iter) > 0) {
                    stream << ", ";
                }
                stream << "(" << iter->first << ", " << iter->second << ")";
            }
            stream << "}" << '\n';

            return stream;
        }
    };
    // Result of the analysis of a group.
    struct GroupAnalysis {
        // Estimate of the arithmetic and memory cost for computing the group.
        Cost cost;
        // Estimate of the parallelism that can be exploited while computing
        // the group.
        Expr parallelism;
        map<FStage, Expr> est_parallelism;
        map<FStage, Expr> est_occupancy;
        Expr occupancy;
        map<FStage, Expr> threads;
        map<string, Expr> thread_blocks;
        map<FStage, Expr> est_active_threads;
        // n_threads
        Expr n_threads;

        Expr n_blocks;
        Expr active_threads;
        Expr total_cost;
        Expr threads_out;
        // estimation of the shared memory usage
        Expr shared_mem;
        Expr allocated_root;
        GroupAnalysis()
            : cost(Cost()), parallelism(Expr()) {
        }
        GroupAnalysis(const Cost &c, Expr p)
            : cost(c), parallelism(std::move(p)) {
        }

        inline bool defined() const {
            return cost.defined() && parallelism.defined() && threads_out.defined() &&
                   n_threads.defined() && occupancy.defined() &&
                   active_threads.defined();
        }

        void simplify() {
            cost.simplify();
            if (parallelism.defined()) {
                parallelism = Internal::simplify(parallelism);
            }
            if (occupancy.defined()) {
                occupancy = Internal::simplify(occupancy);
            }
            if (n_threads.defined()) {
                n_threads = Internal::simplify(n_threads);
            }
            if (threads_out.defined()) {
                threads_out = Internal::simplify(threads_out);
            }
            if (active_threads.defined()) {
                active_threads = Internal::simplify(active_threads);
            }
        }
        /*
      if(target.has_feature(Target::CUDACapability30)){
    gparams.max_regs_per_thread=make_const(Float(32),63);
    gparams.total_regs_per_SM=make_const(Float(32),65536);
    gparams.max_regs_per_block=make_const(Float(32),65536);
    gparams.limit_threads_per_warp=make_const(Float(32),32);
    gparams.min_shared_mem_unit=make_const(Float(32),256);
    gparams.limit_warps_per_SM=make_const(Float(32),64);
    gparams.max_blocks_per_SM=make_const(Float(32),16);
    gparams.limit_shared_mem_per_SM=make_const(Float(32),49152);
    gparams.limit_shared_mem_per_block=make_const(Float(32),49152);
    gparams.limit_threads_per_SM=make_const(Float(32),2048);
    gparams.n_SM=arch_params.parallelism;
    gparams.warp_alloc_granularity=make_const(Float(32),4);
    gparams.reg_alloc_unit_size=make_const(Float(32),256);*/

        void gpu_cost() {
            if (n_threads.defined() && cost.defined() && occupancy.defined()) {
                total_cost = (cost.memory + cost.arith) / (n_threads)*occupancy;
                total_cost = Internal::simplify(total_cost);
            }
        }

        friend std::ostream &operator<<(std::ostream &stream,
                                        const GroupAnalysis &analysis) {
            stream << "[arith cost:" << analysis.cost.arith << ", ";
            stream << "memory cost:" << analysis.cost.memory << ", ";
            stream << "parallelism:" << analysis.parallelism << "]\n";
            return stream;
        }
    };

    // Configuration of a group and the corresponding analysis. A group is the
    // set of functions that are computed together in tiles and the group config
    // specifies at what granularity they are computed together ('tile_sizes').
    struct GroupConfig {
        map<string, Expr> tile_sizes;
        GroupAnalysis analysis;
        GroupConfig(const map<string, Expr> &tile_sizes,
                    const GroupAnalysis &analysis)
            : tile_sizes(tile_sizes), analysis(analysis) {
        }
        GroupConfig()
            : tile_sizes(map<string, Expr>()), analysis(GroupAnalysis()) {
        }
    };

    // Cache for storing the best configuration for the grouping choice. During
    // the grouping process, the impact of grouping two groups together is only
    // limited to the producers and consumers of the groups that are being grouped
    // together. The best grouping choices for the rest of the pipeline need not
    // be re-evaluated and caching them improves performance significantly.
    map<GroupingChoice, GroupConfig> grouping_cache;

    // Each group in the pipeline has a single output stage. A group is comprised
    // of function stages that are computed together in tiles (stages of a
    // function are always grouped together). 'groups' is the mapping from the
    // output stage of the group to the group.
    map<FStage, Group> groups;
    // The child stages of each stage (i.e. stages that depend on or use the
    // values computed by a particular stage) in the pipeline.
    map<FStage, set<FStage>> children;
    map<FStage, set<FStage>> global_children;
    // Map from the output stage of the group to the analysis of the group. The
    // mapping needs to be updated whenever the grouping changes.
    map<FStage, GroupAnalysis> group_costs;
    vector<FStage> all_stages;
    // Levels that are targeted by the grouping algorithm. In the 'Inline' mode,
    // the grouping algorithm groups the functions by inlining the expression for
    // the producer function into the consumer stage. In the 'FastMem' mode, the
    // grouping is done at the level of tiles of the group output stage.
    enum class Level { Inline,
                       FastMem };

    // Bounds of each function stage in the pipeline. These bounds are inferred
    // from the estimates of the outputs and other functions in the pipeline.
    const map<string, Box> &pipeline_bounds;
    // Parameters of the machine model that is used for estimating the cost of
    // each group in the pipeline.
    const MachineParams &arch_params;
    GPU_Params gparams;

    // Dependency analysis of the pipeline. This support queries on regions
    // accessed and computed for producing some regions of some functions.
    DependenceAnalysis &dep_analysis;
    // The arithmetic and memory costs of evaluating the expressions which define
    // each function in the pipeline.
    RegionCosts &costs;
    // Output functions of the pipeline.
    const vector<Function> &outputs;

    Partitioner(const map<string, Box> &_pipeline_bounds,
                const MachineParams &_arch_params,
                const vector<Function> &_outputs,
                DependenceAnalysis &_dep_analysis, RegionCosts &_costs);
    void get_gpu_params(const Target &target);
    void initialize_groups();
    void evaluate_new_tiles();
    void evaluate_final_tiles();
    // Merge 'prod_group' into 'cons_group'. The output stage of 'cons_group'
    // will be the output stage of the merged group.
    Group merge_groups(const Group &prod_group, const Group &cons_group);

    // Merge 'prods' in 'choice' into 'cons'. Set the tile size of the new group
    // to the one specified by 'eval'. If 'level' is set to Inline, all members
    // of 'prods' will be inlined in the new group.
    void merge_groups(const GroupingChoice &choice, const GroupConfig &eval,
                      Partitioner::Level level);

    // check if it's a single stage group (with multiple update defs)
    bool is_singleton_group(const Group &g);
    // checks a group's members for boundary condition funcs
    bool check_for_boundary(const Group &g);
    // Given a grouping 'g', compute the estimated cost (arithmetic + memory) and
    // parallelism that can be potentially exploited when computing that group.
    GroupAnalysis analyze_group(const Group &g, bool show_analysis,
                                bool to_inline);

    Expr estimate_threads(const map<string, Expr>);
    pair<map<string, Expr>, vector<pair<FStage, Expr>>>
    eval_max_threads(const Group &g, bool show_analysis);
    Expr estimate_threads_out(const Group &g, bool show_analysis);

    // For each group in the partition, return the regions of the producers
    // need to be allocated to compute a tile of the group's output.

    map<FStage, vector<map<string, Expr>>> tile_configs_per_stage;
    map<FStage, map<string, Box>> group_storage_bounds();
    Group optimize_granularity(const Group &pre_group, const AutoSchedule &sched);
    map<FStage, DimBounds> group_solo_bounds(const Group &g);
    map<FStage, map<string, map<string, Expr>>> reuse_per_stage;
    // For each group in the partition, return the regions of the producers
    // required to compute a tile of the group's output.
    map<FStage, map<FStage, DimBounds>> group_loop_bounds();

    // Partition the pipeline by iteratively merging groups until a fixpoint is
    // reached.
    void group(Partitioner::Level level);

    // Given a grouping choice, return a configuration for the group that gives
    // the highest estimated benefits.
    GroupConfig evaluate_choice(Group &group, Partitioner::Level level);

    // Pick the best choice among all the grouping options currently available.
    // Uses the cost model to estimate the benefit of each choice. This returns a
    // vector of choice and configuration pairs which describe the best grouping
    // choice.
    vector<pair<GroupingChoice, GroupConfig>>
    choose_candidate_grouping(const vector<pair<string, string>> &cands,
                              Partitioner::Level level);

    // Return the bounds required to produce a function stage.
    DimBounds get_bounds(const FStage &stg);

    // Return the bounds required to produce a tile of a function stage.
    DimBounds get_bounds_from_tile_sizes(const FStage &stg,
                                         const map<string, Expr> &tile_sizes);

    // Return the estimated size of the bounds.
    map<string, Expr> bounds_to_estimates(const DimBounds &bounds);

    // Given a function stage, return a vector of possible tile configurations for
    // that function stage.
    vector<map<string, Expr>> generate_tile_configs(const FStage &stg,
                                                    bool final_tile);

    // Find the best tiling configuration for a group 'g' among a set of tile
    // configurations. This returns a pair of configuration with the highest
    // estimated benefit and the estimated benefit.
    pair<map<string, Expr>, GroupAnalysis>
    find_best_tile_config(const Group &g, bool is_init, bool is_final);
    map<string, Expr> find_min_tile_dims(const FStage &stg);
    set<string> dims_to_tile(const FStage &stg);
    // estimates the occupancy on a gpu for this group
    vector<Expr> estimate_occupancy(const Expr threads, const Expr footprint,
                                    Expr n_blocks);
    Expr estimate_tile_benefit(const GroupAnalysis &old_grouping,
                               const GroupAnalysis &new_grouping,
                               bool final_tiles, bool ensure_parallelism);

    // Estimate the benefit (arithmetic + memory) of 'new_grouping' over
    // 'old_grouping'. Positive values indicates that 'new_grouping' may be
    // preferrable over 'old_grouping'. When 'ensure_parallelism' is set to true,
    // this will return an undefined cost if the estimated parallelism is smaller
    // than the machine parameters. If 'no_redundant_work' is set, we only
    // consider the arithmetic cost, i.e. if the arithmetic benefit is negative,
    // we will treat it as no benefits and we should not perform the new grouping.
    Expr estimate_benefit(const GroupAnalysis &old_grouping,
                          const GroupAnalysis &new_grouping,
                          bool no_redundant_work, bool ensure_parallelism);

    // Same as above; however, 'new_grouping' is a vector of function pairs that
    // are to be grouped together.
    Expr estimate_benefit(
        const vector<pair<GroupingChoice, GroupConfig>> &new_grouping,
        bool no_redundant_work, bool ensure_parallelism,
        Partitioner::Level level);

    // Return the total estimate on arithmetic and memory costs of computing all
    // groups within the pipeline.
    Cost get_pipeline_cost();

    // Return the maximum access stride to allocation of 'func_acc' along any
    // loop variable specified in 'vars'. Access expressions along each dimension
    // of the allocation are specified by 'acc_exprs'. The dimension bounds of the
    // allocation are specified by 'buffer_bounds'.
    Expr find_max_access_stride(const Scope<> &vars, const string &func_acc,
                                const vector<Expr> &acc_exprs,
                                const Box &buffer_bounds);

    // Return the sum of access strides along each of the loop variables in
    // a function stage. The bounds of all the allocations accessed are specified
    // in 'allocation_bounds'. Return an empty map if it can't figure out any of
    // the stride dimension.
    map<string, Expr>
    analyze_spatial_locality(const FStage &stg,
                             const map<string, Box> &parent_bounds,
                             const set<string> &inlines = set<string>());

    map<string, map<string, Expr>> evaluate_reuse(const FStage &stg,
                                                  const set<string> &prods);
    map<string, Expr> find_dims(const FStage &stg, unsigned int stage_num);
    // Generate and apply schedules for all functions within a pipeline by
    // following their grouping structure.
    //
    // TODO: A mode where schedules are not applied to the functions might be
    // interesting.
    //
    // TODO: The current form of the schedule returned is not very useful since it
    // cannot be manipulated and introspected very easily. The problem is that all
    // of the scheduling uses internal function and variable names which are not
    // visible to the user. Additionally, functions like sum and maximum are not
    // user visible. More thought needs to go into interaction between the user
    // and auto scheduling.
    void generate_cpu_schedule(const Target &t, AutoSchedule &sched);

    // Same as \ref Partitioner::generate_cpu_schedule, but this generates and
    // applies schedules for a group of function stages.

    void
    generate_group_cpu_schedule(const Group &g, const Target &t,
                                const map<FStage, DimBounds> &group_loop_bounds,
                                const map<string, Box> &group_storage_bounds,
                                const set<string> &inlines, AutoSchedule &sched,
                                bool will_fold);

    // Split the dimension of stage 'f_handle' along 'v' into inner and outer
    // dimensions. Modify 'estimates' according to the split and append the split
    // schedule to 'sched'.
    pair<VarOrRVar, VarOrRVar>
    split_dim(const Group &g, Stage f_handle, int stage_num, Definition def,
              bool is_group_output, VarOrRVar v, const Expr &factor,
              string in_suffix, string out_suffix, map<string, Expr> &estimates,
              AutoSchedule &sched);

    // Loop over the dimensions of function stage 'f_handle' starting from
    // innermost and vectorize the first pure dimension encountered.
    void vectorize_stage(const Group &g, Stage f_handle, int stage_num,
                         Definition def, Function func, bool is_group_output,
                         bool is_singleton, const Target &t, set<string> &rvars,
                         map<string, Expr> &estimates, AutoSchedule &sched,
                         vector<string> thread_dims);
    // unroll innermostsssss
    void unroll_group_outer_stage(const Group &g, Stage f_handle, int stage_num,
                                  Definition def, Function func,
                                  bool is_group_output, const Target &t,
                                  set<string> &rvars,
                                  map<string, Expr> &estimates,
                                  AutoSchedule &sched, vector<string> thread_dims,
                                  vector<string> non_thread_dims);

    // unroll / tile + unroll members
    void unroll_group_inner_stage(const Group &g, Stage f_handle, int stage_num,
                                  Definition def, Function func,
                                  bool is_group_output, const Target &t,
                                  set<string> &rvars,
                                  map<string, Expr> &estimates,
                                  AutoSchedule &sched, vector<string> thread_dims,
                                  vector<string> non_thread_dims);

    // Reorder the dimensions to preserve spatial locality. This function
    // checks the stride of each access. The dimensions of the loop are reordered
    // such that the dimension with the smallest access stride is innermost.
    // This takes the strides along each dimension as input.
    void reorder_dims(Stage f_handle, int stage_num, Definition def,
                      map<string, Expr> strides, AutoSchedule &sched,
                      map<string, Expr> sbounds, vector<string> thread_dims);

    // Helper functions to display partition information of the pipeline.
    void disp_pipeline_costs();
    void disp_pipeline_bounds();
    void disp_pipeline_graph();
    void disp_grouping();
};

void Partitioner::disp_grouping() {
    debug(0) << "\n=========" << '\n';
    debug(0) << "Grouping:" << '\n';
    debug(0) << "=========" << '\n';
    for (const auto &g : groups) {
        debug(0) << g.second << '\n';
    }
    debug(0) << "=========" << '\n';
}

void Partitioner::disp_pipeline_graph() {
    debug(0) << "\n================" << '\n';
    debug(0) << "Pipeline graph:" << '\n';
    debug(0) << "================" << '\n';
    for (const auto &f : global_children) {
        debug(0) << f.first << ": {";
        for (auto iter = f.second.begin(); iter != f.second.end(); ++iter) {
            if (std::distance(f.second.begin(), iter) > 0) {
                debug(0) << ", ";
            }
            debug(0) << *iter;
        }
        debug(0) << "}" << '\n';
    }
    debug(0) << "================" << '\n';
}

void Partitioner::disp_pipeline_bounds() {
    debug(0) << "\n================" << '\n';
    debug(0) << "Pipeline bounds:" << '\n';
    debug(0) << "================" << '\n';
    disp_regions(pipeline_bounds);
    debug(0) << "===============" << '\n';
}

Cost Partitioner::get_pipeline_cost() {
    internal_assert(!group_costs.empty());

    Cost total_cost(0, 0);
    for (const pair<FStage, Group> &g : groups) {
        const GroupAnalysis &analysis = get_element(group_costs, g.first);
        if (!analysis.cost.defined()) {
            return Cost();
        }
        total_cost.arith += analysis.cost.arith;
        total_cost.memory += analysis.cost.memory;
    }
    total_cost.simplify();
    return total_cost;
}

void Partitioner::disp_pipeline_costs() {
    internal_assert(!group_costs.empty());
    Cost total_cost(0, 0);
    debug(0) << "\n===============" << '\n';
    debug(0) << "Pipeline costs:" << '\n';
    debug(0) << "===============" << '\n';
    debug(0) << "Group: (name) [arith cost, mem cost, parallelism]" << '\n';
    for (const pair<FStage, Group> &g : groups) {
        const GroupAnalysis &analysis = get_element(group_costs, g.first);
        if (!total_cost.arith.defined()) {
            continue;
        } else if (!analysis.cost.arith.defined()) {
            total_cost.arith = Expr();
        } else {
            total_cost.arith += analysis.cost.arith;
        }

        if (!total_cost.memory.defined()) {
            continue;
        } else if (!analysis.cost.memory.defined()) {
            total_cost.memory = Expr();
        } else {
            total_cost.memory += analysis.cost.memory;
        }

        debug(0) << "Group: " << g.first << " [";
        debug(0) << analysis.cost.arith << ", " << analysis.cost.memory << ", "
                 << analysis.parallelism << "]\n";
    }
    total_cost.simplify();
    debug(0) << "Total arithmetic cost: " << total_cost.arith << '\n';
    debug(0) << "Total memory cost: " << total_cost.memory << '\n';
    debug(0) << "===============" << '\n';
}

// Construct a partitioner and build the pipeline graph on which the grouping
// algorithm operates.
Partitioner::Partitioner(const map<string, Box> &_pipeline_bounds,
                         const MachineParams &_arch_params,
                         const vector<Function> &_outputs,
                         DependenceAnalysis &_dep_analysis, RegionCosts &_costs)
    : pipeline_bounds(_pipeline_bounds), arch_params(_arch_params),
      dep_analysis(_dep_analysis), costs(_costs), outputs(_outputs) {
    // Place each stage of a function in its own group. Each stage is
    // a node in the pipeline graph.
    for (const auto &f : dep_analysis.env) {
        if (!pipeline_bounds.count(f.first)) {
            // If a function does not have a pipeline bound (i.e. it can be
            // statically proven that no one ever uses it), we should not
            // consider it during the grouping.
            debug(5) << "Creating partitioner: ignore function \"" << f.first
                     << "\" since it has empty pipeline bounds\n";
            continue;
        }
        int num_stages = f.second.updates().size() + 1;
        for (int s = 0; s < num_stages; s++) {
            FStage stg(f.second, s);
            Group g(stg, {stg});
            groups.insert(make_pair(stg, g));
        }
    }

    // Find the consumers of each function and use it to populate the children
    // map.
    for (const auto &f : dep_analysis.env) {
        int num_stages = f.second.updates().size() + 1;
        for (int s = 0; s < num_stages; s++) {
            set<string> parents = get_parents(f.second, s);
            for (const string &c : parents) {
                // Filter out the calls to pipeline inputs. 'env' only contains
                // the functions computed and not the inputs.
                auto iter = dep_analysis.env.find(c);
                if ((c != f.first) && (iter != dep_analysis.env.end())) {
                    // Consumer depends only on the last stage of a producer
                    // with multiple stages.
                    const Function &prod_func = iter->second;
                    int final_stage = prod_func.updates().size();

                    FStage prod_stage(prod_func, final_stage);
                    FStage cons_stage(f.second, s);
                    children[prod_stage].insert(cons_stage);
                }
            }

            if (s > 0) {
                // Update the children map to reflect the dependencies between
                // different stages of the same function.
                FStage prod_stage(f.second, s - 1);
                FStage cons_stage(f.second, s);
                children[prod_stage].insert(cons_stage);
            }
        }
    }
}

bool Partitioner::check_for_boundary(const Group &group) {
    bool has_boundary = false;
    const vector<string> bound_conds = {"constant_exterior", "repeat_edge",
                                        "repeat_image", "mirror_image",
                                        "mirror_interior"};
    for (const auto &mem : group.members) {
        const string &mem_name = mem.func.name();
        if (group.inlined.find(mem_name) != group.inlined.end())
            continue;
        for (const auto &bcs : bound_conds) {
            if (mem_name.find(bcs) != std::string::npos)
                return true;
            // else if(mem.stage_num>0)  return true;
        }
        // if(mem_name.find(
    }
    return has_boundary;
}

bool Partitioner::is_singleton_group(const Group &group) {

    for (const auto &mem : group.members) {
        if (group.inlined.find(mem.func.name()) != group.inlined.end())
            continue;
        if (mem.func.name() != group.output.func.name())
            return false;
    }
    return true;
}

map<string, Expr> Partitioner::find_dims(const FStage &stg,
                                         unsigned int stage_num) {

    Expr order = make_zero(Int(64));
    map<string, Expr> dim_order;
    // cout<<stg.func.name()<<" finding dims"<<endl;
    // Partitioner::get_bounds(const FStage &s)
    const map<string, Interval> &def_bounds = get_bounds(stg);
    const vector<Dim> &dims = get_stage_dims(stg.func, stage_num);
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        //  cout<<dims[d].var;
        const Interval &bound = get_element(def_bounds, dims[d].var);
        Expr extent = get_extent(bound);
        dim_order[dims[d].var] = extent;
        // order=simplify(order+1);
    }
    return dim_order;
}

Partitioner::Group
Partitioner::optimize_granularity(const Group &pre_g,
                                  const AutoSchedule &sched) {
    //  disp_pipeline_graph();
    Group g = pre_g;
    // populate a set for members
    set<string> g_members;
    // cout<<"fusion analysis..."<<endl;
    // Group stages
    // compute all at their consumers IFF there is reuse or consumed > 1 time
    for (auto &st : g.members) {
        g_members.insert(st.func.name());
        if (st.func.name() == g.output.func.name())
            continue;
        if (g.inlined.find(st.func.name()) != g.inlined.end())
            continue;
        // find its consumers - set storage 1 level above reuse (if any)
        for (const auto cons : g.members) {
            if (cons.re.size() == 0)
                continue;
            if (cons.func.name() == st.func.name())
                continue;

            const auto &overlap_dim = cons.re.find(st.func.name());
            if (overlap_dim == cons.re.end())
                continue;
            for (const auto &overlap_n : overlap_dim->second) {
                if ((!overlap_n.second.defined()) || can_prove(overlap_n.second == 0))
                    continue;
                bool new_clevel = true;
                if (st.compute_level.defined()) {
                    cout << "Stage " << st.func.name() << " compute at "
                         << cons.func.name() << " , " << overlap_n.first << endl;

                    const auto &stage_pos = sched.get_func_index(cons.func.name());
                    string old_clevel = get_expr_str(st.compute_stage);
                    const auto &stage_old = sched.get_func_index(old_clevel);
                    if (stage_pos < stage_old)
                        new_clevel = false;
                }
                if (new_clevel) {
                    st.compute_stage = cons.func.name();
                    st.compute_level = overlap_n.first;
                    cout << "Stage " << st.func.name() << " compute at "
                         << cons.func.name() << " , " << overlap_n.first << endl;
                }
            }
        }
    }
    // now we want to make sure that stages with no reuse on the output just move
    // to their externalmost (not a word!) consumers i.e.

    for (auto &st : g.members) {
        if (g.inlined.find(st.func.name()) != g.inlined.end())
            continue;
        if (st.func.name() == g.output.func.name())
            continue;
        if (g.inlined.find(get_expr_str(st.compute_stage)) != g.inlined.end())
            st.compute_stage = g.output.func.name();
        int sum = 0;
        vector<string> consumers;
        if (global_children.find(st) != global_children.end()) {
            for (const FStage &c : get_element(global_children, st)) {
                cout << "cons " << c.func.name() << " prod " << st.func.name() << endl;
                if (c.func.name() == st.func.name())
                    continue;
                if (g_members.find(c.func.name()) != g_members.end() &&
                    (g.inlined.find(c.func.name()) == g.inlined.end())) {
                    sum++;
                    consumers.push_back(c.func.name());
                } else if (g.inlined.find(c.func.name()) != g.inlined.end())
                    sum++;
            }
            if (sum > 1) {
                st.compute_stage = g.output.func.name();
                st.compute_level = Expr();
            } else if (sum == 1)
                st.compute_stage = g.output.func.name();

        } else {
            // cout<<"mem "<<st.func.name() <<" no children? "<<endl;
            st.compute_stage = g.output.func.name();
        }
        // cout<<"Mem "<<st.func.name()<<" compute at "<<st.compute_stage<<" ,
        // "<<st.compute_level<<endl;
    }
    return g;
};

void Partitioner::initialize_groups() {
    for (pair<const FStage, Group> &g : groups) {
        all_stages.push_back(g.first);
        pair<map<string, Expr>, GroupAnalysis> best =
            find_best_tile_config(g.second, true, false);
        g.second.tile_sizes = best.first;
        group_costs.emplace(g.second.output, best.second);
        if (!best.second.cost.memory.defined())
            best.second.cost.memory = Float(32).max();
        if (!best.second.cost.arith.defined())
            best.second.cost.arith = Float(32).max();
        for (auto &st : g.second.members) {
            const auto &st_re = reuse_per_stage.find(st);
            if (st_re != reuse_per_stage.end())
                st.re = st_re->second;
        }
        //   cout<<"g.output "<<g.second.output<<" cost
        //   "<<best.second.cost.memory<<endl;
    }
    grouping_cache.clear();
}

/*const Expr &max_regs_per_thread=make_const(Float(64),64);
const Expr &total_regs_per_SM=make_const(Float(64),65536);
const Expr &max_regs_per_block=make_const(Float(64),65536);
const Expr &limit_threads_per_warp=make_const(Float(64),32);
const Expr &min_shared_mem_unit=make_const(Float(64),256);
          //const Expr &reg_alloc_unit_size=make_const(Float(64),256);
const Expr &limit_warps_per_SM=make_const(Float(64),64);
const Expr &max_blocks_per_SM=make_const(Float(64),16);
const Expr &limit_shared_mem_per_SM=make_const(Float(64),49152);
const Expr &limit_threads_per_SM=make_const(Float(64),2048);
const Expr &n_SM=make_const(Int(32),arch_params.parallelism);
const Expr &warp_alloc_granularity=make_const(Float(32),4);
const Expr &reg_alloc_unit_size=make_const(Float(32),256);*/

void Partitioner::get_gpu_params(const Target &target) {
    internal_assert(target.has_feature(Target::CUDA));
    if (target.has_feature(Target::CUDACapability30)) {
        gparams.max_regs_per_thread = make_const(Float(32), 63);
        gparams.total_regs_per_SM = make_const(Float(32), 65536);
        gparams.max_regs_per_block = make_const(Float(32), 65536);
        gparams.limit_threads_per_warp = make_const(Float(32), 32);
        gparams.min_shared_mem_unit = make_const(Float(32), 256);
        gparams.limit_warps_per_SM = make_const(Float(32), 64);
        gparams.max_blocks_per_SM = make_const(Float(32), 16);
        gparams.limit_shared_mem_per_SM = make_const(Float(32), 49152);
        gparams.limit_shared_mem_per_block = make_const(Float(32), 49152);
        gparams.limit_threads_per_SM = make_const(Float(32), 2048);
        gparams.limit_threads_per_block = make_const(Float(32), 1024);
        gparams.n_SM = arch_params.parallelism;
        gparams.warp_alloc_granularity = make_const(Float(32), 4);
        gparams.reg_alloc_unit_size = make_const(Float(32), 256);
        return;

    } else if (target.has_feature(Target::CUDACapability32)) {
        gparams.max_regs_per_thread = make_const(Float(32), 255);
        gparams.total_regs_per_SM = make_const(Float(32), 32768);
        gparams.max_regs_per_block = make_const(Float(32), 32768);
        gparams.limit_threads_per_warp = make_const(Float(32), 32);
        gparams.min_shared_mem_unit = make_const(Float(32), 256);
        gparams.limit_warps_per_SM = make_const(Float(32), 64);
        gparams.max_blocks_per_SM = make_const(Float(32), 16);
        gparams.limit_shared_mem_per_SM = make_const(Float(32), 49152);
        gparams.limit_shared_mem_per_block = make_const(Float(32), 49152);
        gparams.limit_threads_per_SM = make_const(Float(32), 2048);
        gparams.limit_threads_per_block = make_const(Float(32), 1024);
        gparams.n_SM = arch_params.parallelism;
        gparams.warp_alloc_granularity = make_const(Float(32), 4);
        gparams.reg_alloc_unit_size = make_const(Float(32), 256);
        return;

    } else if (target.has_feature(Target::CUDACapability35)) {
        gparams.max_regs_per_thread = make_const(Float(32), 255);
        gparams.total_regs_per_SM = make_const(Float(32), 65536);
        gparams.max_regs_per_block = make_const(Float(32), 65536);
        gparams.limit_threads_per_warp = make_const(Float(32), 32);
        gparams.min_shared_mem_unit = make_const(Float(32), 256);
        gparams.limit_warps_per_SM = make_const(Float(32), 64);
        gparams.max_blocks_per_SM = make_const(Float(32), 16);
        gparams.limit_shared_mem_per_SM = make_const(Float(32), 49152);
        gparams.limit_shared_mem_per_block = make_const(Float(32), 49152);
        gparams.limit_threads_per_SM = make_const(Float(32), 2048);
        gparams.limit_threads_per_block = make_const(Float(32), 1024);
        gparams.n_SM = arch_params.parallelism;
        gparams.warp_alloc_granularity = make_const(Float(32), 4);
        gparams.reg_alloc_unit_size = make_const(Float(32), 256);
        return;

    } else if (target.has_feature(Target::CUDACapability50)) {
        gparams.max_regs_per_thread = make_const(Float(32), 255);
        gparams.total_regs_per_SM = make_const(Float(32), 65536);
        gparams.max_regs_per_block = make_const(Float(32), 65536);
        gparams.limit_threads_per_warp = make_const(Float(32), 32);
        gparams.min_shared_mem_unit = make_const(Float(32), 256);
        gparams.limit_warps_per_SM = make_const(Float(32), 64);
        gparams.max_blocks_per_SM = make_const(Float(32), 32);
        gparams.limit_shared_mem_per_SM = make_const(Float(32), 65536);
        gparams.limit_shared_mem_per_block = make_const(Float(32), 49152);
        gparams.limit_threads_per_SM = make_const(Float(32), 2048);
        gparams.limit_threads_per_block = make_const(Float(32), 1024);
        gparams.n_SM = arch_params.parallelism;
        gparams.warp_alloc_granularity = make_const(Float(32), 4);
        gparams.reg_alloc_unit_size = make_const(Float(32), 256);
        return;

    } else if (target.has_feature(Target::CUDACapability61)) {
        gparams.max_regs_per_thread = make_const(Float(32), 255);
        gparams.total_regs_per_SM = make_const(Float(32), 65536);
        gparams.max_regs_per_block = make_const(Float(32), 65536);
        gparams.limit_threads_per_warp = make_const(Float(32), 32);
        gparams.min_shared_mem_unit = make_const(Float(32), 256);
        gparams.limit_warps_per_SM = make_const(Float(32), 64);
        gparams.max_blocks_per_SM = make_const(Float(32), 32);
        gparams.limit_shared_mem_per_SM = make_const(Float(32), 98304);
        gparams.limit_shared_mem_per_block = make_const(Float(32), 49152);
        gparams.limit_threads_per_SM = make_const(Float(32), 2048);
        gparams.limit_threads_per_block = make_const(Float(32), 1024);
        gparams.n_SM = arch_params.parallelism;
        gparams.warp_alloc_granularity = make_const(Float(32), 4);
        gparams.reg_alloc_unit_size = make_const(Float(32), 256);
        return;

    } else if (target.has_feature(Target::CUDACapability70)) {
        gparams.max_regs_per_thread = make_const(Float(32), 255);
        gparams.total_regs_per_SM = make_const(Float(32), 65536);
        gparams.max_regs_per_block = make_const(Float(32), 65536);
        gparams.limit_threads_per_warp = make_const(Float(32), 32);
        gparams.min_shared_mem_unit = make_const(Float(32), 256);
        gparams.limit_warps_per_SM = make_const(Float(32), 64);
        gparams.max_blocks_per_SM = make_const(Float(32), 32);
        gparams.limit_shared_mem_per_SM = make_const(Float(32), 98304);
        gparams.limit_shared_mem_per_block = make_const(Float(32), 98304);
        gparams.limit_threads_per_SM = make_const(Float(32), 2048);
        gparams.limit_threads_per_block = make_const(Float(32), 1024);
        gparams.n_SM = arch_params.parallelism;
        gparams.warp_alloc_granularity = make_const(Float(32), 4);
        gparams.reg_alloc_unit_size = make_const(Float(32), 256);
        return;
    }
}

void Partitioner::evaluate_new_tiles() {
    group_costs.clear();
    for (pair<const FStage, Group> &g : groups) {
        // if(g.second.members.size()-g.second.inlined.size()>1)  continue;
        //  if(g.second.tile_sizes.size()!=0)  continue;
        pair<map<string, Expr>, GroupAnalysis> best =
            find_best_tile_config(g.second, false, false);
        g.second.tile_sizes = best.first;
        group_costs.emplace(g.second.output, best.second);
    }
    grouping_cache.clear();
}

void Partitioner::evaluate_final_tiles() {
    group_costs.clear();
    for (pair<const FStage, Group> &g : groups) {
        // if(g.second.members.size()-g.second.inlined.size()>1)  continue;
        //  if(g.second.tile_sizes.size()!=0)  continue;
        pair<map<string, Expr>, GroupAnalysis> best =
            find_best_tile_config(g.second, false, true);
        g.second.tile_sizes = best.first;
        group_costs.emplace(g.second.output, best.second);
    }
    grouping_cache.clear();
}

map<string, map<string, Expr>>
Partitioner::evaluate_reuse(const FStage &stg, const set<string> &prods) {
    map<string, map<string, Expr>> reuse;
    Function f = stg.func;

    // TODO: Check if tile size of 1 in each dimension gives a reasonable
    // answer or reuse should be evaluated at a much larger granularity or
    // symbolically. Using a symbolic version might be better if the objective
    // is to prove the dimension has no reuse. The only downside with the
    // symbolic method is that it is totally at the mercy of the simplifier.
    // Another option is sampling or using a larger granularity.
    map<string, Expr> tile_sizes;

    const vector<Dim> &dims = get_stage_dims(stg.func, stg.stage_num);
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        tile_sizes[dims[d].var] = 2;
    }

    DimBounds bounds = get_bounds_from_tile_sizes(stg, tile_sizes);
    // DimBounds stg_bounds = get_bounds(stg);
    // map<string,Expr> extents= bounds_to_estimates(stg_bounds);
    vector<map<string, Box>> reuse_regions = dep_analysis.overlap_regions(
        stg.func, stg.stage_num, bounds, prods, false, &costs.input_estimates);

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        Expr total_reuse = make_zero(Int(32));
        //  if (debug::debug_level() >= 3) {
        //      disp_regions(reuse_regions[d]);
        // }
        for (const auto &reg : reuse_regions[d]) {
            Expr size = box_size(reg.second);
            if (!size.defined()) {
                total_reuse = Expr();
                break;
            } else {
                //    ////cout<<reg.first<<" "<<size<<std::endl;
                total_reuse += size;
                // if(!dims[d].is_rvar())
                reuse[reg.first].emplace(dims[d].var, simplify(size));
                //  else reuse[reg.first].emplace(dims[d].var,extents[dims[d].var]);
            }
        }
        // reuse.emplace(dims[d].var, simplify(total_reuse));
    }

    return reuse;
}
vector<pair<Partitioner::GroupingChoice, Partitioner::GroupConfig>>
Partitioner::choose_candidate_grouping(
    const vector<pair<string, string>> &cands, Partitioner::Level level) {
    vector<pair<GroupingChoice, GroupConfig>> best_grouping;
    Expr best_benefit = make_zero(Int(64));
    for (const auto &p : cands) {
        bool illegal_config = false;
        // Compute the aggregate benefit of inlining into all the children.
        vector<pair<GroupingChoice, GroupConfig>> grouping;

        const Function &prod_f = get_element(dep_analysis.env, p.first);
        int final_stage = prod_f.updates().size();

        FStage prod(prod_f, final_stage);

        for (const FStage &c : get_element(children, prod)) {

            GroupConfig best_config;
            GroupingChoice cand_choice(prod_f.name(), c);

            // Check if the candidate has been evaluated for grouping before
            const auto &iter = grouping_cache.find(cand_choice);
            if (iter != grouping_cache.end()) {
                best_config = iter->second;
            } else {

                const Function &prod_f =
                    get_element(dep_analysis.env, cand_choice.prod);

                int num_prod_stages = prod_f.updates().size() + 1;
                vector<Group> prod_groups;

                for (int s = 0; s < num_prod_stages; s++) {
                    FStage prod_s(prod_f, s);
                    prod_groups.push_back(get_element(groups, prod_s));
                }

                Group cons = get_element(groups, cand_choice.cons);

                Group group = cons;
                for (const auto &prod_g : prod_groups) {
                    group = merge_groups(prod_g, group);
                }
                if (level == Partitioner::Level::Inline) {
                    for (const auto &prod_g : prod_groups) {
                        for (const FStage &s : prod_g.members) {
                            group.inlined.insert(s.func.name());
                        }
                    }

                    for (const string &f : cons.inlined) {
                        group.inlined.insert(f);
                    }

                } else {
                    //      group=inline_rest(group);
                    // groups.erase(group.output);
                    // groups.insert(make_pair(group.output, group));
                    // groups[group.output]=group;
                    //     for(const auto &is:group.inlined)   cout<<"new inl after
                    //     "<<is<<endl;
                }

                best_config = evaluate_choice(group, level);
                //          internal_assert(best_config.analysis.n_threads.defined());
                // Cache the result of the evaluation for the pair
                // if(best_config.analysis.is_functional())

                grouping_cache.emplace(cand_choice, best_config);

                /*else{
            cout<<"illegal found on "<<cand_choice.cons<<endl;
            illegal_config=true;
        }*/
            }

            // if(best_config.analysis.is_functional())
            grouping.push_back(make_pair(cand_choice, best_config));
            /*else{
          cout<<"illegal found on "<<cand_choice.cons<<endl;
          illegal_config=true;
      }*/
        }

        bool no_redundant_work = false;
        // tood here Levell::INLINE K LEVEL::FASTMEM
        Expr overall_benefit = Expr();

        if (!illegal_config)
            overall_benefit =
                estimate_benefit(grouping, no_redundant_work, true, level);

        debug(3) << "Candidate grouping:\n";
        for (const auto &g : grouping) {
            debug(3) << "  " << g.first;
        }
        debug(3) << "Candidate benefit: " << overall_benefit << '\n';
        // TODO: The grouping process can be non-deterministic when the costs
        // of two choices are equal
        if (!illegal_config && overall_benefit.defined() &&
            can_prove(best_benefit <= overall_benefit)) {
            best_grouping = grouping;
            best_benefit = overall_benefit;
        }
    }

    debug(3) << "\nBest grouping:\n";
    for (const auto &g : best_grouping) {
        debug(3) << "  " << g.first;
    }
    if (best_grouping.size() > 0) {
        debug(3) << "Best benefit: " << best_benefit << '\n';
    }

    return best_grouping;
}

inline bool operator==(const map<string, Expr> &m1,
                       const map<string, Expr> &m2) {
    if (m1.size() != m2.size()) {
        return false;
    }
    for (const auto &it1 : m1) {
        const auto &it2 = m2.find(it1.first);
        if (it2 == m2.end()) {
            return false;
        } else if (!equal(it1.second, it2->second)) {
            return false;
        }
    }
    return true;
}

/*
map<string,Expr> Partitioner::find_min_tile_dims(const FStage &stg, pipe_IR p){
  map<string,Expr> min_tiles;
  map<string,Expr> min_tile;
  set<string> group_members;
  for (const auto &stg : p.pipe) {
    for(const auto &st:stg.second){
      if(p.pipe.find(st.func.name())!=p.pipe.end())
group_members.insert(st.func.name());
           //     cout<<"member "<<st.func.name()<<endl;
    }
  }

  for( auto &stg:p.pipe){

    if(p.pipe[stg.first][0].is_inline) continue;
    cout<<"consumer "<<stg.first<<endl;
    for( auto &pipe_stages:stg.second){
      set<string> parents = get_parents(pipe_stages.func,
pipe_stages.stage_num); for( auto &prods : parents){ bool is_function =
(dep_analysis.env.find(prods) != dep_analysis.env.end());
            //        bool is_same_stage=(prods==pipe_stages.func.name());
        if(p.pipe.find(prods)==p.pipe.end())  continue;
        bool is_member=group_members.find(prods)!=group_members.end();
             // if(!is_function||!is_member) continue;
        if(!is_function||!is_member) continue;
        if(p.pipe.find(pipe_stages.func.name())==p.pipe.end())  continue;
        cout<<"Producer "<<prods<<std::endl;
        for( auto &ov : pipe_stages.re[prods]){

          if(ov.first==pipe_stages.cols[0])   continue;
          if(!min_tiles[ov.first].defined())  min_tiles[ov.first]=ov.second;
          else if(can_prove(min_tiles[ov.first]<ov.second))
min_tiles[ov.first]=ov.second; cout<<"Dim "<<ov.first<<" overlap
"<<ov.second<<std::endl; cout<<"Dim "<<ov.first<<" min tile
"<<min_tiles[ov.first]<<endl;
        }
      }
    }
  }
  for(const auto &mins : min_tiles){
           // if(!min_tile[mins.first].defined())
min_tile[mins.first]=make_zero(Int(32)); if(mins.second.defined())
min_tile[mins.first]=cast(Int(32),mins.second);
              //  cout<<"Dim "<<mins.first<<" min tile
"<<min_tile[mins.first]<<std::endl;
 }
 min_tile[p.pipe[stg.func.name()][0].cols[0]]=make_zero(Int(32));
 return min_tile;
}

*/

vector<map<string, Expr>> Partitioner::generate_tile_configs(const FStage &stg,
                                                             bool final_tile) {

    Expr bytes_per_ele = make_zero(Int(32));
    ;
    string stg_name = stg.func.name();
    const auto &iter = dep_analysis.env.find(stg_name);
    if (iter != dep_analysis.env.end()) {
        const Function &f = iter->second;
        for (const auto &e : f.values()) {
            bytes_per_ele += e.type().bytes();
        }
    }
    Expr max_tile = 64 / bytes_per_ele;

    const vector<Dim> &dims = get_stage_dims(stg.func, stg.stage_num);

    // Get the dimensions that are going to be tiled in this stage.
    // Skipping rvars for now.

    // find which vars we want to  tile
    vector<string> tile_vars;

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        if (!dims[d].is_rvar()) {
            tile_vars.push_back(dims[d].var);
        }
    }
    set<string> thread_vars = dims_to_tile(stg);

    vector<int> size_variants1 = {2, 4, 8, 16, 32};
    vector<int> size_variants2 = {2, 4, 8, 16, 32, 64, 128, 256};
    vector<int> size_variants;
    if (thread_vars.size() > 1)
        size_variants = size_variants1;
    else
        size_variants = size_variants2;
    vector<map<string, Expr>> tile_configs;

    DimBounds stg_bounds = get_bounds(stg);

    map<string, Expr> extents = bounds_to_estimates(stg_bounds);

    if (tile_vars.size() > 1) {

        int n;
        n = tile_vars.size();

        map<int, Expr> tiles;

        std::vector<int> a(n);
        for (int i = 0; i < n; i++) {
            if (thread_vars.find(tile_vars[i]) != thread_vars.end() &&
                can_prove(extents[tile_vars[i]] > 64))
                tiles[i] = 8;
            else if (thread_vars.find(tile_vars[i]) != thread_vars.end() &&
                     can_prove(extents[tile_vars[i]] <= 64))
                tiles[i] = 2;
            else if (thread_vars.size() >= 2)
                tiles[i] = extents[tile_vars[i]];
            else
                tiles[i] = make_one(Int(32));
        }
        int index = 0;
        int depth = tile_vars.size();
        int max_int = 32;
        bool flag_iter = true;
        while (flag_iter) {
            // main body
            map<string, Expr> tiling;
            for (size_t i = 0; i < tile_vars.size(); i++) {
                tiling.emplace(tile_vars[i],
                               simplify(min(tiles[i], extents[tile_vars[i]])));
            }
            if (!tiling.empty()) {
                bool is_duplicate =
                    std::find_if(tile_configs.begin(), tile_configs.end(),
                                 [&tiling](const map<string, Expr> &m) {
                                     return (tiling == m);
                                 }) != tile_configs.end();

                if ((!is_duplicate)) {
                    tile_configs.push_back(tiling);
                }
            }
            // check iter
            a[index]++;
            //  cout<<"GEN TILES "<<tile_vars[index]<<" "<<tiles[index]<<endl;
            if (final_tile && thread_vars.find(tile_vars[index]) != thread_vars.end())
                tiles[index] = simplify(tiles[index] + 2);
            else if ((!final_tile) &&
                     thread_vars.find(tile_vars[index]) != thread_vars.end())
                tiles[index] = simplify(tiles[index] * 2);
            else if (thread_vars.find(tile_vars[index]) == thread_vars.end())
                tiles[index] = extents[tile_vars[index]];
            if (thread_vars.size() <= 2 &&
                (thread_vars.find(tile_vars[index]) != thread_vars.end() &&
                 (can_prove(tiles[index] > 2 * max_tile))))
                a[index] = max_int;
            else if (thread_vars.size() > 2 &&
                     (thread_vars.find(tile_vars[index]) != thread_vars.end() &&
                      (can_prove(tiles[index] > max_tile))))
                a[index] = max_int;
            else if ((thread_vars.find(tile_vars[index]) != thread_vars.end() &&
                      (can_prove(extents[tile_vars[index]] / tiles[index] <= 2))))
                a[index] = max_int;
            if (can_prove(extents[tile_vars[0]] > 1024) &&
                (can_prove(tiles[index] >= extents[tile_vars[0]] / 32)))
                a[index] = max_int;
            ;
            if (can_prove(extents[tile_vars[0]] < 1024) &&
                (can_prove(tiles[index] > extents[tile_vars[0]] / 2)))
                a[index] = max_int;
            ;
            while (a[index] == max_int) {
                // Overflow, we're done
                if (index == depth - 1) {
                    //    ////cout<<"breaking"<<std::endl;
                    flag_iter = false;
                    break;
                }

                if (thread_vars.find(tile_vars[index]) != thread_vars.end() &&
                    can_prove(extents[tile_vars[index]] > 64))
                    tiles[index] = 8;
                else if (thread_vars.find(tile_vars[index]) != thread_vars.end() &&
                         can_prove(extents[tile_vars[index]] <= 64))
                    tiles[index] = 2;
                else if (thread_vars.size() >= 2)
                    tiles[index] = extents[tile_vars[index]];
                else
                    tiles[index] = make_one(Int(32));
                a[index] = 0;
                index++;

                a[index]++;
                if (final_tile &&
                    thread_vars.find(tile_vars[index]) != thread_vars.end())
                    tiles[index] = simplify(tiles[index] + 2);
                else if ((!final_tile) &&
                         thread_vars.find(tile_vars[index]) != thread_vars.end())
                    tiles[index] = simplify(tiles[index] * 2);
                else if (thread_vars.find(tile_vars[index]) == thread_vars.end())
                    tiles[index] = extents[tile_vars[index]];
                if (thread_vars.size() <= 2 &&
                    (thread_vars.find(tile_vars[index]) != thread_vars.end() &&
                     (can_prove(tiles[index] > 2 * max_tile))))
                    a[index] = max_int;
                else if (thread_vars.size() > 2 &&
                         (thread_vars.find(tile_vars[index]) != thread_vars.end() &&
                          (can_prove(tiles[index] > max_tile))))
                    a[index] = max_int;
                else if ((thread_vars.find(tile_vars[index]) != thread_vars.end() &&
                          (can_prove(extents[tile_vars[index]] / tiles[index] <= 2))))
                    a[index] = max_int;
                if (can_prove(extents[tile_vars[0]] > 1024) &&
                    (can_prove(tiles[index] >= extents[tile_vars[0]] / 32)))
                    a[index] = max_int;
                ;
                if (can_prove(extents[tile_vars[0]] < 1024) &&
                    (can_prove(tiles[index] > extents[tile_vars[0]] / 2)))
                    a[index] = max_int;
                ;
            }
            index = 0;
        };

    } else {

        for (const auto &dim_size : size_variants) {

            if (stg.stage_num > 0) {
                if (can_prove(extents[tile_vars[0]] > 1024) &&
                    (can_prove(dim_size >= extents[tile_vars[0]] / 128)))
                    continue;
                if (can_prove(extents[tile_vars[0]] < 1024) &&
                    (can_prove(dim_size > extents[tile_vars[0]] / 32)))
                    continue;
            }
            map<string, Expr> tiling;
            tiling.emplace(tile_vars[0], dim_size);

            if (!tiling.empty()) {
                bool is_duplicate =
                    std::find_if(tile_configs.begin(), tile_configs.end(),
                                 [&tiling](const map<string, Expr> &m) {
                                     return (tiling == m);
                                 }) != tile_configs.end();

                if ((!is_duplicate)) {
                    tile_configs.push_back(tiling);
                }
            }
        }
    }
    /*
  cout<<"tiles for "<<stg.func.name()<<endl;

        for(auto &iter :tile_configs){
          for(auto &iti:iter){
            cout<<iti.first<<" "<<iti.second<<std::endl;
          }
          cout<<"============"<<std::endl;
        }*/
    return tile_configs;
}
/*
set<string> Partitioner::dims_to_tile(const FStage &stg){
    const vector<Dim> &dims = get_stage_dims(stg.func, stg.stage_num);
    //create a map of extents to check the largest ones!
      vector<string> tile_vars_init;
  vector<string> tile_vars_intr;
  vector<pair<string,Expr>> extents;
  for (int d = 0; d < (int)dims.size() - 1; d++) {
    if (!dims[d].is_rvar()) {
      //cout<<"var "<<dims[d].var<<endl;
      tile_vars_init.push_back(dims[d].var);
  }
  if(dims.size()<=1)  return {};
  if(tile_vars_init.size()==0)  return {};
  for(auto &tiled_var:tile_vars_init){
    Interval &bound = get_element(stg_bounds, it1);
    Expr extent = get_extent(bound);
    //cout<<"it1 "<<it1<<endl;
    internal_assert(extent.defined());
    extents.emplace(tiled_var.first,extent);
            //  cout<<it1<<" "<<extent<<std::endl;
  }
  sort(extents.begin(),extents.end(),sort_by_expr


}
*/

set<string> Partitioner::dims_to_tile(const FStage &stg) {
    const vector<Dim> &dims = get_stage_dims(stg.func, stg.stage_num);

    // Get the dimensions that are going to be tiled in this stage.
    // Skipping rvars for now.

    // find which vars we want to  tile
    vector<pair<string, Expr>> extents;
    // cout<<"dims_to_tile for "<<stg.func.name()<<endl;

    vector<string> tile_vars_init;
    vector<string> tile_vars_intr;
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        if (!dims[d].is_rvar()) {
            // cout<<"var "<<dims[d].var<<endl;
            tile_vars_init.push_back(dims[d].var);
        }
    }
    if (dims.size() <= 1)
        return {};
    if (tile_vars_init.size() == 0)
        return {};
    // see if we actually WANT to tile all of these dims
    // first find which have an extent of at least 32
    DimBounds stg_bounds = get_bounds(stg);
    vector<string> tile_vars1 = tile_vars_init;
    // Print the bounds
    Expr max_extent = make_zero(Int(32));
    string max_var;

    for (auto &it1 : tile_vars1) {
        Interval &bound = get_element(stg_bounds, it1);
        Expr extent = get_extent(bound);
        internal_assert(extent.defined());
        extents.push_back(make_pair(it1, extent));
    }
    set<string> tile_vars;
    tile_vars.insert(extents[0].first);
    sort(extents.begin(), extents.end(), sort_by_Expr);
    /*for(const auto &ex:extents){
    cout<<"var "<<ex.first<<" ex "<<ex.second<<endl;
  }*/
    // now make sure we tile the ones with large extents as well
    for (const auto &ex : extents) {
        if (tile_vars.size() >= 3)
            break;
        if (tile_vars.find(ex.first) != tile_vars.end())
            continue;
        if (can_prove(ex.second > 16))
            tile_vars.insert(ex.first);
    }

    if (tile_vars.size() < 2) {
        for (const auto &ex : extents) {
            if (tile_vars.size() >= 3)
                break;
            if (tile_vars.find(ex.first) != tile_vars.end())
                continue;
            if (can_prove(ex.second > 8))
                tile_vars.insert(ex.first);
        }
    }

    set<string> tile_vars_set;
    for (const auto &tt : tile_vars)
        tile_vars_set.emplace(tt);
    /*if(tile_vars_set.size()>2) {
  cout<<"asdasdas error too many tiled vars"<<endl;
  while(true);
  }*/
    return tile_vars_set;
}

vector<Expr> Partitioner::estimate_occupancy(Expr threads, Expr shared_mem,
                                             Expr n_blocks) {
    bool debug_flag = false;
    //  Expr threads=eval_max_threads(g,false);
    if (debug_flag)
        std::cout << "Estimating occupancy..." << std::endl;
    if (debug_flag)
        std::cout << "Threads " << simplify(threads) << " memory " << shared_mem
                  << std::endl;
    /*const Expr &max_regs_per_thread=make_const(Float(64),64);
  const Expr &total_regs_per_SM=make_const(Float(64),65536);
  const Expr &max_regs_per_block=make_const(Float(64),65536);
  const Expr &limit_threads_per_warp=make_const(Float(64),32);
  const Expr &min_shared_mem_unit=make_const(Float(64),256);
            //const Expr &reg_alloc_unit_size=make_const(Float(64),256);
  const Expr &limit_warps_per_SM=make_const(Float(64),64);
  const Expr &max_blocks_per_SM=make_const(Float(64),16);
  const Expr &limit_shared_mem_per_SM=make_const(Float(64),49152);
  const Expr &limit_threads_per_SM=make_const(Float(64),2048);
  const Expr &n_SM=make_const(Int(32),arch_params.parallelism);
  const Expr &warp_alloc_granularity=make_const(Float(32),4);
  const Expr &reg_alloc_unit_size=make_const(Float(32),256);*/
    /*int max_regs_per_thread=255;
  int total_regs_per_SM=65536;
  int max_regs_per_block=65536;
  int limit_threads_per_warp=32;
  int min_shared_mem_unit=256;
  //int warp_alloc_granularity=4;

  int limit_warps_per_SM=64;
  int max_blocks_per_SM=16;
  int limit_shared_mem_per_SM=49152;*/

    // num_reg <= min( max_reg_per_thread,  total_reg/num_thread)

    // estimate the worst case num of regs (should be able to do better)
    Expr num_regs = simplify(cast<int>(
        min(gparams.max_regs_per_thread, gparams.total_regs_per_SM / threads)));
    if (can_prove(num_regs < 1))
        num_regs = 1;
    else if (can_prove(num_regs > 64))
        num_regs = make_const(Float(32), 64);
    if (debug_flag)
        std::cout << "Estimated regs..." << num_regs << std::endl;
    // get the number of warps per block
    Expr warps_per_block =
        simplify(roundUp(threads / gparams.limit_threads_per_warp, 1));
    // if(can_prove(warps_per_block<1))  warps_per_block=1;
    // shmem in bytes
    Expr shared_mem_bytes =
        simplify((max(shared_mem, gparams.min_shared_mem_unit)));

    // regs per block
    // CEILING(CEILING(MyWarpsPerBlock,myWarpAllocationGranularity)*MyRegCount*limitThreadsPerWarp,myAllocationSize),
    // MyWarpsPerBlock)
    // Expr
    // regs_per_block=simplify(max(max(warps_per_block,warp_alloc_granularity)*num_regs*limit_threads_per_warp,reg_alloc_unit_size));
    Expr regs_per_block = warps_per_block;
    if (debug_flag)
        std::cout << "Estimating regs per block..." << regs_per_block << std::endl;
    // group regs_per_SM
    // FLOOR(limitRegsPerBlock/CEILING(MyRegCount*limitThreadsPerWarp,myAllocationSize),myWarpAllocationGranularity)
    Expr group_limit_regs_per_SM =
        roundDown(gparams.max_regs_per_block /
                      roundUp(num_regs * gparams.limit_threads_per_warp,
                              gparams.reg_alloc_unit_size),
                  gparams.warp_alloc_granularity);
    // Expr
    // group_limit_regs_per_SM=simplify((max_regs_per_block/(num_regs*limit_threads_per_warp)));

    if (debug_flag)
        std::cout << "Estimating limit regs per SM..." << regs_per_block
                  << std::endl;
    // std::cout<<"Estimating occupancy3..."<<std::endl;
    // group warps per SM
    // MIN(limitBlocksPerMultiprocessor,FLOOR(limitWarpsPerMultiprocessor/MyWarpsPerBlock,1))
    Expr block_group_warps_per_SM =
        simplify(min(gparams.max_blocks_per_SM,
                     roundDown(gparams.limit_warps_per_SM / warps_per_block, 1)));
    // Expr
    // block_group_warps_per_SM=simplify((min(max_blocks_per_SM,(limit_warps_per_SM/warps_per_block))));
    if (debug_flag)
        std::cout << "Estimating occupancy limit warps per SM..."
                  << block_group_warps_per_SM << std::endl;
    //=IF(MyRegCount>limitRegsPerThread,0,IF(MyRegCount>0,FLOOR(C42/MyRegsPerBlock,
    //1)*FLOOR(limitTotalRegisters/limitRegsPerBlock,1),limitBlocksPerMultiprocessor))
    // FLOOR(C42/MyRegsPerBlock, 1)*FLOOR(limitTotalRegisters/limitRegsPerBlock,1)
    Expr block_group_regs_per_SM = simplify(
        roundDown(group_limit_regs_per_SM / regs_per_block, 1) *
        roundDown(gparams.total_regs_per_SM / gparams.max_regs_per_block, 1));
    // Expr
    // block_group_regs_per_SM=simplify((group_limit_regs_per_SM/regs_per_block)*(total_regs_per_SM/max_regs_per_block));
    if (debug_flag)
        std::cout << "Estimating occupancy limit regs per SM..."
                  << block_group_regs_per_SM << std::endl;
    // FLOOR(limitTotalSharedMemory/MySharedMemPerBlock,1),limitBlocksPerMultiprocessor)
    // Expr
    // block_group_shared_mem_per_SM=simplify((limit_shared_mem_per_SM/shared_mem_bytes));
    Expr block_group_shared_mem_per_SM = simplify(
        roundDown(gparams.limit_shared_mem_per_SM / shared_mem_bytes, 1));
    if (debug_flag)
        std::cout << "Estimating occupancy limit SH mem per SM..."
                  << block_group_shared_mem_per_SM << std::endl;
    // find active blocks per sm
    Expr active_blocks_per_SM = make_one(Int(32));
    if (can_prove(block_group_warps_per_SM <= block_group_regs_per_SM) &&
        can_prove(block_group_warps_per_SM <= block_group_shared_mem_per_SM)) {
        if (debug_flag)
            std::cout << "Limited by Max Warps or Max Blocks per Multiprocessor"
                      << std::endl;
        active_blocks_per_SM = block_group_warps_per_SM;
    } else if (can_prove(block_group_regs_per_SM <= block_group_warps_per_SM) &&
               can_prove(block_group_regs_per_SM <=
                         block_group_shared_mem_per_SM)) {
        if (debug_flag)
            std::cout << "Limited by Registers per Multiprocessor" << std::endl;
        active_blocks_per_SM = block_group_regs_per_SM;
    } else {
        if (debug_flag)
            std::cout << "Limited by Shared Memory per Multiprocessor" << std::endl;
        active_blocks_per_SM = block_group_shared_mem_per_SM;
    }

    // active warps per block
    Expr active_warps_per_SM = simplify(active_blocks_per_SM * warps_per_block);
    if (debug_flag)
        std::cout << "Estimating active warps..." << active_warps_per_SM
                  << std::endl;
    // occupancy
    Expr occupancy = simplify(active_warps_per_SM / gparams.limit_warps_per_SM);
    Expr active_threads = simplify(
        min(active_warps_per_SM * min(threads, gparams.limit_threads_per_warp),
            gparams.limit_threads_per_SM));
    Expr active_blocks = active_blocks_per_SM;

    if (debug_flag)
        cout << "active threads " << simplify(active_threads) << endl;
    active_blocks = min(n_blocks / active_blocks_per_SM, active_blocks_per_SM);
    if (debug_flag)
        cout << "active blocks " << simplify(active_blocks) << endl;
    Expr active_SMs = min(gparams.n_SM, active_blocks * gparams.n_SM);

    // active_threads=simplify(active_threads*active_SMs);
    if (debug_flag)
        cout << "active SMs " << simplify(active_SMs) << endl;
    if (debug_flag)
        std::cout << " occupancy " << simplify(occupancy) << std::endl
                  << std::endl
                  << std::endl;
    return {occupancy, active_threads, active_SMs, num_regs};
}

pair<map<string, Expr>, Partitioner::GroupAnalysis>
Partitioner::find_best_tile_config(const Group &g, bool is_init,
                                   bool is_final) {
    const vector<Dim> &dims = get_stage_dims(g.output.func, g.output.stage_num);
    bool small_extents = true;

    map<string, Expr> out_extents = find_dims(g.output, g.output.stage_num);
    bool all_rvars = true;
    for (int i = 0; i < (int)dims.size() - 1; i++) {
        if (!dims[i].is_rvar())
            all_rvars = false;
    }

    for (const auto &ex : out_extents) {
        if (can_prove(ex.second / 32 >= 8))
            small_extents = false;
    }
    map<string, Expr> no_tile_config;
    GroupAnalysis best_analysis;
    if (all_rvars)
        return make_pair(no_tile_config, best_analysis);
    Group no_tile = g;
    Expr best_n_threads;
    map<string, Expr> best_config;
    Expr best_occupancy;

    //  cout<<"find best for "<<g.output.func.name()<<" dim size
    //  "<<dims.size()<<endl;
    if (dims.size() == 1)
        return make_pair(no_tile_config, best_analysis);
    no_tile.tile_sizes = no_tile_config;
    bool flag_db = true;
    bool test_it = false;
    // if is init just analyze and return ones
    if (is_init) {
        set<string> thread_vars = dims_to_tile(g.output);
        Group init_group = g;
        map<string, Expr> init_config;
        for (const auto &tv : thread_vars)
            init_config.emplace(tv, make_one(Int(32)));
        init_group.tile_sizes = init_config;
        GroupAnalysis new_analysis = analyze_group(init_group, flag_db, true);
        return make_pair(init_config, new_analysis);
    }

    // Generate tiling configurations
    vector<map<string, Expr>> configs;
    ///  cout<<"gen tiles before"<<endl;
    const auto &cached_config = tile_configs_per_stage.find(g.output);
    if (cached_config == tile_configs_per_stage.end() || (is_final)) {
        configs = generate_tile_configs(g.output, is_final);
        tile_configs_per_stage.emplace(g.output, configs);
    } else
        configs = cached_config->second;
    // fcout<<"gen tiles done"<<endl;
    Group best_group = g;
    for (const auto &config : configs) {
        //  if(config.size()==1)  small_extents=true;
        Group new_group = g;
        new_group.tile_sizes = config;
        // p.tiles=config;
        GroupAnalysis new_analysis;
        if (!is_init)
            new_analysis = analyze_group(new_group, flag_db, false);
        else
            new_analysis = analyze_group(new_group, flag_db, true);
        // std::cout<<"BEFORE BENEFIT"<<std::endl;
        if (!new_analysis.defined()) {

            continue;
        }
        if ((!test_it) && (!is_init) && (!small_extents) &&
            can_prove(new_analysis.threads_out >= 16)) {
            best_analysis = new_analysis;
            best_config = config;
            test_it = true;
            // best_n_threads=new_analysis.n_threads;
        } else if ((!test_it) && (small_extents || is_init)) {

            best_analysis = new_analysis;
            best_config = config;
            test_it = true;
            // best_n_threads=new_analysis.n_threads;
        }
        // bool no_redundant_work = false;
        Expr benefit;
        if (!is_final) {
            if (!is_init)
                benefit =
                    estimate_tile_benefit(best_analysis, new_analysis, false, true);
            else
                benefit = estimate_benefit(best_analysis, new_analysis, false, true);
        } else {
            benefit = estimate_tile_benefit(best_analysis, new_analysis, true, true);
        }

        // for(const auto &tts:config) cout<<tts.first<<" "<<tts.second<<endl;
        Expr n_threads = new_analysis.n_threads;
        // if(flag_db)  std::cout<<"NTREADS DONE "<<n_threads<<std::endl;
        Expr occupancy = new_analysis.occupancy;
        ;
        // if(flag_db)  std::cout<<"OCCUPANCY DONE "<<occupancy<<std::endl;
        // if(flag_db)  std::cout<<"benefit DONE "<<benefit<<std::endl;
        // const Expr &base_occupancy= make_const(Float(32),0.1);
        if (test_it && benefit.defined() && can_prove(benefit >= 0)) {
            best_config = config;
            best_analysis = new_analysis;
            best_group = new_group;
            best_occupancy = occupancy;
            best_n_threads = n_threads;
        }
    }
    if (0) {
        std::cout << "TILES for " << g.output.func.name() << std::endl
                  << std::endl;
        for (auto &iti : best_config) {
            std::cout << iti.first << " " << iti.second << std::endl;
        }
        std::cout << "threads out: " << best_analysis.threads_out << " Threads "
                  << best_n_threads << std::endl;
        std::cout << "======================" << std::endl
                  << std::endl;
    }

    return make_pair(best_config, best_analysis);
}

void Partitioner::group(Partitioner::Level level) {
    bool fixpoint = false;
    while (!fixpoint) {
        Cost pre_merge = get_pipeline_cost();

        fixpoint = true;
        vector<pair<string, string>> cand;
        for (const pair<FStage, Group> &g : groups) {
            bool is_output = false;
            for (const Function &f : outputs) {
                if (g.first.func.name() == f.name()) {
                    is_output = true;
                    break;
                }
            }

            // All stages of a function are computed at a single location.
            // The last stage of the function represents the candidate choice
            // of grouping the function into a consumer.

            const Function &prod_f =
                get_element(dep_analysis.env, g.first.func.name());
            bool is_final_stage = (g.first.stage_num == prod_f.updates().size());

            if (is_output || !is_final_stage) {
                continue;
            }

            const auto &iter = children.find(g.first);
            if (iter != children.end()) {
                // All the stages belonging to a function are considered to be a
                // single child.
                set<string> child_groups;
                for (const FStage &s : iter->second) {
                    child_groups.insert(s.func.name());
                }

                int num_children = child_groups.size();
                // Only groups with a single child are considered for grouping
                // when grouping for computing in tiles.
                // TODO: The current scheduling model does not allow functions
                // to be computed at different points.
                if ((num_children == 1) && (level == Partitioner::Level::FastMem)) {
                    const string &prod_name = prod_f.name();
                    const string &cons_name = (*child_groups.begin());
                    cand.push_back(make_pair(prod_name, cons_name));
                } else if ((level == Partitioner::Level::Inline) && prod_f.is_pure()) {
                    const string &prod_name = prod_f.name();
                    cand.push_back(make_pair(prod_name, ""));
                }
            }
        }

        debug(3) << "\n============================" << '\n';
        debug(3) << "Current grouping candidates:" << '\n';
        debug(3) << "============================" << '\n';
        for (size_t i = 0; i < cand.size(); ++i) {
            debug(3) << "{" << cand[i].first << ", " << cand[i].second << "}" << '\n';
        }

        vector<pair<GroupingChoice, GroupConfig>> best =
            choose_candidate_grouping(cand, level);
        //  cout<<"returned best prod"<<endl;
        if (best.empty()) {
            continue;
        } else {
            fixpoint = false;
        }

        // The following code makes the assumption that all the stages of a function
        // will be in the same group. 'choose_candidate_grouping' ensures that the
        // grouping choice being returned adheres to this constraint.
        const string &prod = best[0].first.prod;
        //   cout<<"asdasd best prod"<<endl;
        const Function &prod_f = get_element(dep_analysis.env, prod);
        size_t num_stages = prod_f.updates().size() + 1;

        FStage final_stage(prod_f, num_stages - 1);
        set<FStage> prod_group_children = get_element(children, final_stage);

        // Invalidate entries of the grouping cache
        set<GroupingChoice> invalid_keys;
        for (const auto &c : prod_group_children) {
            for (const auto &entry : grouping_cache) {
                if ((entry.first.prod == c.func.name()) || (entry.first.cons == c)) {
                    invalid_keys.insert(entry.first);
                }
            }
        }
        for (const auto &key : invalid_keys) {
            grouping_cache.erase(key);
        }

        for (const auto &group : best) {
            internal_assert(group.first.prod == prod);
            merge_groups(group.first, group.second, level);
        }

        for (size_t s = 0; s < num_stages; s++) {
            FStage prod_group(prod_f, s);
            groups.erase(prod_group);
            group_costs.erase(prod_group);

            // Update the children mapping
            children.erase(prod_group);
            for (auto &f : children) {
                set<FStage> &cons = f.second;
                auto iter = cons.find(prod_group);
                if (iter != cons.end()) {
                    cons.erase(iter);
                    // For a function with multiple stages, all the stages will
                    // be in the same group and the consumers of the function
                    // only depend on the last stage. Therefore, when the
                    // producer group has multiple stages, parents of the
                    // producers should point to the consumers of the last
                    // stage of the producer.
                    cons.insert(prod_group_children.begin(), prod_group_children.end());
                }
            }
        }

        Cost post_merge = get_pipeline_cost();
        if (debug::debug_level() >= 3) {
            disp_pipeline_costs();
        }
    }
}

DimBounds Partitioner::get_bounds(const FStage &s) {
    DimBounds bounds;

    const vector<string> &args = s.func.args();
    for (size_t d = 0; d < args.size(); d++) {
        bounds[args[d]] = get_element(pipeline_bounds, s.func.name())[d];
    }

    return get_stage_bounds(s.func, s.stage_num, bounds);
}

DimBounds
Partitioner::get_bounds_from_tile_sizes(const FStage &s,
                                        const map<string, Expr> &tile_sizes) {
    map<string, Interval> bounds;

    const map<string, Interval> &def_bounds = get_bounds(s);
    const vector<Dim> &dims = get_stage_dims(s.func, s.stage_num);

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        string var = dims[d].var;
        const Interval &bound = get_element(def_bounds, var);
        const auto &iter = tile_sizes.find(var);
        if (iter != tile_sizes.end()) {
            const Expr &size = iter->second;
            // Check if the bounds allow for tiling with the given tile size,
            // i.e. ensure at least 2 tiles
            Expr extent = get_extent(bound);
            internal_assert(extent.defined());
            if (can_prove(extent >= 2 * size)) {
                // TODO: Maybe shift this to the center of the pipeline bound
                bounds[var] = Interval(0, simplify(size - 1));
            } else {
                // If the dimension is too small, do not tile it and set the
                // extent of the bounds to that of the dimension estimate
                bounds[var] = bound;
            }
        } else {
            bounds[var] = bound;
        }
    }

    return bounds;
}

Partitioner::Group Partitioner::merge_groups(const Group &prod_group,
                                             const Group &cons_group) {
    vector<FStage> group_members;
    for (const auto &s : prod_group.members) {
        group_members.push_back(s);
    }
    for (const auto &s : cons_group.members) {
        group_members.push_back(s);
    }

    Group group(cons_group.output, group_members);

    for (const auto &f : prod_group.inlined) {
        group.inlined.insert(f);
    }
    for (const auto &f : cons_group.inlined) {
        group.inlined.insert(f);
    }

    return group;
}

Expr Partitioner::estimate_threads(const map<string, Expr> thread_blocks) {
    Expr estimated_threads = make_one(Int(64));
    for (const auto &iti : thread_blocks) {
        estimated_threads = simplify(estimated_threads * iti.second);
    }
    // std::cout<<"est threads "<<estimated_threads<<std::endl;
    return estimated_threads;
}

Expr Partitioner::estimate_threads_out(const Group &g, bool show_analysis) {
    // Get the regions of the pipeline required to compute a tile of the group

    bool flag = false;
    //  if(g.output.func.name()==("downsampled$7"))    flag=true;
    if (flag)
        std::cout << "output of group " << g.output.func.name() << std::endl;

    Expr spawned_threads = make_one(UInt(32));

    const map<string, Interval> &def_bounds = get_bounds(g.output);

    //  map<string, Expr> max_threads;
    DimBounds tile_bounds = get_bounds_from_tile_sizes(g.output, g.tile_sizes);
    map<string, Expr> stg_estimates_out = bounds_to_estimates(tile_bounds);
    // Expr max_threads=make_one(UInt(32));

    for (const auto &bs : stg_estimates_out) {
        const Interval &bound = get_element(def_bounds, bs.first);
        Expr extent = get_extent(bound);
        internal_assert(extent.defined());
        if (flag)
            std::cout << "var " << bs.first << " " << bs.second << std::endl;
        auto dim_in_tiles = g.tile_sizes.find(bs.first);
        if ((dim_in_tiles != g.tile_sizes.end() && (can_prove(extent > 4)))) {

            if (can_prove(extent >= 64))
                spawned_threads = simplify(spawned_threads * bs.second);
        }
    }
    // max_threads=spawned_threads_out;

    return spawned_threads;
}

pair<map<string, Expr>, vector<pair<FStage, Expr>>>
Partitioner::eval_max_threads(const Group &g, bool show_analysis) {
    set<string> gmembers;
    for (const auto &st : g.members)
        gmembers.insert(st.func.name());
    // Get the regions of the pipeline required to compute a tile of the group
    set<string> thread_vars = dims_to_tile(g.output);
    bool flag = false;
    //  if(g.output.func.name()==("downsampled$7"))    flag=true;
    if (flag)
        std::cout << "output of group " << g.output.func.name() << std::endl;
    // bool is_singleton=is_singleton_group(g);

    Expr spawned_threads = make_one(UInt(32));

    const map<string, Interval> &def_bounds = get_bounds(g.output);
    vector<pair<FStage, Expr>> threads;

    vector<string> dims_to_tile;
    map<string, Expr> max_threads;
    DimBounds tile_bounds = get_bounds_from_tile_sizes(g.output, g.tile_sizes);
    map<string, Expr> stg_estimates_out = bounds_to_estimates(tile_bounds);
    // Expr max_threads=make_one(UInt(32));

    Expr vec_var = Expr();
    string vec = "";
    vector<Dim> &dims = get_stage_dims(g.output.func, g.output.stage_num);
    if (thread_vars.size() >= 3) {
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            if (!dims[d].is_rvar()) {
                vec_var = dims[d].var;
                //   cout<<"VEC "<<vec_var<<endl;
                vec = get_expr_str(vec_var);
                break;
            }
        }
    }

    for (const auto &bs : stg_estimates_out) {

        const Interval &bound = get_element(def_bounds, bs.first);
        Expr extent = get_extent(bound);
        internal_assert(extent.defined());
        if (flag)
            std::cout << "var " << bs.first << " " << bs.second << "extent " << extent
                      << std::endl;
        auto dim_in_tiles = thread_vars.find(bs.first);

        //        if(dim_in_tiles!=thread_vars.end()&&vec_var.defined()&&(bs.first==vec)){
        //    cout<<"SKIPPING "<<bs.first<<endl;
        //      continue;
        //        }
        if ((dim_in_tiles != thread_vars.end() && (can_prove(extent > 0)))) {
            auto itiles = max_threads.find(bs.first);
            if (can_prove(extent / bs.second >= 2)) {
                if (itiles == max_threads.end())
                    max_threads[bs.first] = bs.second;
                else
                    max_threads[bs.first] = max(max_threads[bs.first], bs.second);
                spawned_threads *= bs.second;
            } else {
                max_threads[bs.first] = extent;
                spawned_threads *= extent;
            }
        }
    }
    threads.push_back(make_pair(g.output, spawned_threads));

    map<FStage, DimBounds> local_bounds = group_solo_bounds(g);
    for (const auto &stg : local_bounds) {
        if (flag)
            std::cout << "mem stg " << stg.first << std::endl;
        auto is_inlined = g.inlined.find(stg.first.func.name());
        if (gmembers.find(stg.first.func.name()) == gmembers.end())
            continue;
        if (is_inlined == g.inlined.end()) {
            const map<string, Interval> &def_bounds_local = get_bounds(stg.first);
            map<string, Expr> stg_estimates = bounds_to_estimates(stg.second);
            Expr spawned_threads_local = make_one(UInt(32));
            for (const auto &iti : stg_estimates) {
                const Interval &bound = get_element(def_bounds_local, iti.first);
                Expr extent = get_extent(bound);
                internal_assert(extent.defined());
                auto dim_in_tiles = thread_vars.find(iti.first);
                //    if(dim_in_tiles!=thread_vars.end()&&vec_var.defined()&&(iti.first==vec)){
                //   cout<<"SKIPPING "<<iti.first<<endl;
                //          continue;
                //         }
                if ((dim_in_tiles != thread_vars.end() && (can_prove(extent > 0)))) {
                    if (can_prove(extent / iti.second >= 2)) {
                        auto itiles = max_threads.find(iti.first);
                        if (itiles == max_threads.end())
                            max_threads[iti.first] = iti.second;
                        else
                            max_threads[iti.first] = max(max_threads[iti.first], iti.second);
                        if (flag)
                            std::cout << stg.first << " " << iti.first << " " << iti.second
                                      << " extent " << extent << std::endl;
                        spawned_threads_local *= iti.second;
                    } else {
                        auto itiles = max_threads.find(iti.first);
                        if (itiles == max_threads.end())
                            max_threads[iti.first] = extent;
                        else
                            max_threads[iti.first] = max(max_threads[iti.first], extent);
                        spawned_threads_local *= extent;
                    }
                }
            }
            threads.push_back(make_pair(stg.first, spawned_threads_local));
        }
    }
    //  cout<<"SDSDSDSDSDSD"<<endl;
    //  cout<<"threads done "<<threads.size()<<endl;
    //          cout<<"======================================"<<endl;
    pair<map<string, Expr>, vector<pair<FStage, Expr>>> out;
    out.first = max_threads;
    out.second = threads;

    return out;
}

Partitioner::GroupAnalysis
Partitioner::analyze_group(const Group &g, bool show_analysis, bool to_inline) {
    set<string> group_inputs;
    set<string> group_members;
    set<string> group_inlines;

    for (const auto &stg : g.members) {
        group_members.insert(stg.func.name());
        const auto &k = g.inlined.find(stg.func.name());
        if (k != g.inlined.end())
            group_inlines.insert(stg.func.name());
        set<string> parents = get_parents(stg.func, stg.stage_num);
        for (const auto &c : parents) {
            bool is_member = false;
            for (const auto &m : g.members) {
                if (m.func.name() == c) {

                    is_member = true;
                    break;
                }
            }
            if (!is_member) {
                group_inputs.insert(c);
            }
        }
    }

    set<string> thread_dims = dims_to_tile(g.output);

    Expr estimate_tiles = make_one(Int(64));
    Expr parallelism = make_one(Int(64));
    Expr estimate_blocks = make_one(Int(64));
    Expr col_tile;
    if (!g.output.func.has_extern_definition()) {
        // Get the definition corresponding to the group output
        Definition def = get_stage_definition(g.output.func, g.output.stage_num);

        const vector<Dim> &dims = def.schedule().dims();
        // find first pure var
        int col_it = 0;
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            if (!dims[d].is_rvar()) {
                col_it = d;
                break;
            }
        }
        DimBounds stg_bounds = get_bounds(g.output);

        for (int d = 0; d < (int)dims.size() - 1; d++) {
            const string &var = dims[d].var;

            const auto &iter = g.tile_sizes.find(var);
            if (iter != g.tile_sizes.end()) {
                const Expr &size = iter->second;

                Expr extent = get_extent(get_element(stg_bounds, var));
                if (!extent.defined()) {
                    return GroupAnalysis();
                }
                if (d == col_it) {
                    col_tile = size;
                }

                Expr dim_tiles = simplify((extent + size - 1) / size);
                if (thread_dims.find(var) != thread_dims.end())
                    estimate_blocks *= dim_tiles;
                estimate_tiles *= dim_tiles;
            }
            if ((d == col_it) && (!col_tile.defined())) {
                col_tile = get_extent(get_element(stg_bounds, var));
                ;
            }
        }
    }
    // Get the regions of the pipeline required to compute a tile of the group
    DimBounds tile_bounds = get_bounds_from_tile_sizes(g.output, g.tile_sizes);

    map<string, Box> alloc_regions = dep_analysis.regions_required(
        g.output.func, g.output.stage_num, tile_bounds, group_members, false,
        &costs.input_estimates);

    map<string, Box> compute_regions = dep_analysis.regions_required(
        g.output.func, g.output.stage_num, tile_bounds, group_members, true,
        &costs.input_estimates);

    map<string, Box> group_reg, prod_reg, input_reg;
    // Separating into regions that computed within the group and regions that
    // are input to the group
    for (const auto &reg : compute_regions) {
        if ((group_members.find(reg.first) != group_members.end()) &&
            (reg.first != g.output.func.name())) {
            group_reg.emplace(reg.first, reg.second);

        } else if (group_inputs.find(reg.first) != group_inputs.end()) {
            if (dep_analysis.env.find(reg.first) != dep_analysis.env.end()) {
                prod_reg.emplace(reg.first, reg.second);
            } else {
                input_reg.emplace(reg.first, reg.second);
            }
        }
    }

    // Aggregate costs for intermediate functions in a tile and the
    // tile output
    Cost tile_cost = costs.region_cost(group_reg, g.inlined);

    // find the inner extents (weak estimate) of each stage (TODO: divide by
    // stride maybe?)
    map<FStage, DimBounds> group_bounds = group_solo_bounds(g);
    map<string, Expr> col_dims;

    for (const auto &dd : group_bounds) {
        Definition def = get_stage_definition(dd.first.func, dd.first.stage_num);
        const vector<Dim> &dimsf = def.schedule().dims();
        // find first pure var
        string col_dim;
        for (int d = 0; d < (int)dimsf.size() - 1; d++) {
            if (!dimsf[d].is_rvar()) {
                col_dim = dimsf[d].var;
                break;
            }
        }
        map<string, Expr> stg_estimates = bounds_to_estimates(dd.second);

        for (const auto &sgs : stg_estimates) {
            if (sgs.first == col_dim) {

                col_dims.emplace(dd.first.func.name(), sgs.second);
            }
        }
    }

    if (!tile_cost.defined()) {
        return GroupAnalysis();
    }

    Cost out_cost = costs.stage_region_cost(
        g.output.func.name(), g.output.stage_num, tile_bounds, g.inlined);

    if (!out_cost.defined()) {
        return GroupAnalysis();
    }

    Cost group_cost(simplify(tile_cost.arith + out_cost.arith),
                    simplify(tile_cost.memory + out_cost.memory));

    // Detailed load costs for all the group intermediates
    map<string, Expr> group_load_costs =
        costs.detailed_load_costs(group_reg, g.inlined);

    map<string, Expr> out_load_costs = costs.stage_detailed_load_costs(
        g.output.func.name(), g.output.stage_num, tile_bounds, g.inlined);
    combine_load_costs(group_load_costs, out_load_costs);

    Box out_tile_extent;
    if (g.output.stage_num == 0) {
        const vector<string> &args = g.output.func.args();
        for (size_t d = 0; d < args.size(); d++) {
            const auto &iter = tile_bounds.find(args[d]);
            if (iter != tile_bounds.end()) {
                out_tile_extent.push_back(iter->second);
            } else {
                out_tile_extent.push_back(Interval());
            }
        }
    }
    // cout<<"DONE3"<<endl;
    Cost per_tile_cost(group_cost.arith, make_zero(Int(64)));

    Expr partial_factor = make_zero(Float(64));
    Expr partial_footprint = make_zero(Float(64));
    Expr shared_mem = make_zero(Float(64));
    map<string, Expr> footprints;
    Expr out_allocation;

    float load_slope;

    std::string param_merge = Internal::get_env_variable("HL_GPU_L2_COST");
    std::string param_inline = Internal::get_env_variable("HL_GPU_GLOBAL_COST");
    std::string param_shared = Internal::get_env_variable("HL_GPU_SHARED_COST");
    float cost_factor_merge = std::atof(param_merge.c_str());
    float cost_factor_shared = std::atof(param_shared.c_str());
    for (const auto &f_load : group_load_costs) {
        // get type bytes

        Expr bytes_per_ele = make_zero(Int(32));
        ;
        const auto &iter = dep_analysis.env.find(f_load.first);
        if (iter != dep_analysis.env.end()) {
            const Function &f = iter->second;
            for (const auto &e : f.values()) {
                bytes_per_ele += e.type().bytes();
            }
        } else {
            bytes_per_ele = get_element(costs.inputs, f_load.first).bytes();
        }
        Expr max_tile = 64 / bytes_per_ele;

        internal_assert(g.inlined.find(f_load.first) == g.inlined.end())
            << "Intermediates of inlined pure fuction \"" << f_load.first
            << "\" should not have been in the group_load_costs\n";

        Expr footprint;
        Expr load_cost;

        bool is_group_member =
            (group_members.find(f_load.first) != group_members.end());
        bool is_output = (f_load.first == g.output.func.name());

        const auto &alloc_reg = get_element(alloc_regions, f_load.first);

        if (!is_output && is_group_member) {

            footprint = costs.region_size(f_load.first, alloc_reg);
            partial_footprint += footprint;
            shared_mem = simplify(shared_mem + footprint);
            load_cost = f_load.second;
            float cost_factor;
            cost_factor = cost_factor_shared;
            load_slope = (cost_factor) / (48 * 1024);
            partial_factor +=
                load_cost * min(1 + footprint * load_slope, cost_factor);
            ;
            ;

        } else {
            Expr initial_footprint;

            const auto &f_load_pipeline_bounds =
                get_element(pipeline_bounds, f_load.first);

            bool is_function =
                (dep_analysis.env.find(f_load.first) != dep_analysis.env.end());
            if (!is_function) {  // It is a load to some input buffer
                initial_footprint =
                    costs.input_region_size(f_load.first, f_load_pipeline_bounds);
                // Subsequent loads
                footprint = costs.input_region_size(f_load.first, alloc_reg);
                float cost_factor = cost_factor_merge;
                load_slope = (cost_factor) / (64 * 1024);

                if ((!to_inline)) {
                    partial_factor +=
                        footprint * min(1 + initial_footprint * load_slope, cost_factor) /
                        min(col_tile, max_tile);
                    partial_factor += f_load.second *
                                      min(1 + footprint * load_slope, cost_factor) /
                                      min(col_tile, max_tile);
                } else {
                    partial_factor +=
                        f_load.second *
                        min(1 + initial_footprint * load_slope, cost_factor);
                }
            } else if (is_output) {  // Load to the output function of the group
                internal_assert(is_group_member)
                    << "Output " << f_load.first
                    << " should have been a group member\n";
                // Initial loads
                initial_footprint =
                    costs.region_size(f_load.first, f_load_pipeline_bounds);
                out_allocation = initial_footprint;
                // Subsequent loads
                footprint = costs.region_size(f_load.first, out_tile_extent);
                float cost_factor = cost_factor_merge;
                load_slope = (cost_factor) / (64 * 1024);
                if (!to_inline && (g.output.stage_num > 0)) {
                    partial_factor +=
                        footprint * min(1 + initial_footprint * load_slope, cost_factor);
                    ;
                    partial_factor +=
                        f_load.second * min(1 + footprint * load_slope, cost_factor);
                    ;
                } else if (!to_inline) {
                    partial_factor +=
                        footprint * min(1 + initial_footprint * load_slope, cost_factor) /
                        min(col_tile, max_tile);
                    ;
                    partial_factor += f_load.second *
                                      min(1 + footprint * load_slope, cost_factor) /
                                      min(col_tile, max_tile);
                    ;
                } else {
                    partial_factor +=
                        f_load.second *
                        min(1 + initial_footprint * load_slope, cost_factor);
                }
                partial_footprint += initial_footprint;
            } else {  // Load to some non-member function (i.e. function from other
                      // groups)
                footprint = costs.region_size(f_load.first, alloc_reg);
                initial_footprint =
                    costs.region_size(f_load.first, f_load_pipeline_bounds);
                float cost_factor;
                cost_factor = cost_factor_merge;
                load_slope = (cost_factor) / (64 * 1024);

                if ((!to_inline)) {
                    Expr col_dim;
                    if (col_dims.find(f_load.first) == col_dims.end())
                        col_dim = col_tile;
                    else
                        col_dim = col_dims[f_load.first];
                    col_dim = min(min(col_dim, col_tile), max_tile);
                    partial_factor +=
                        footprint * min(1 + initial_footprint * load_slope, cost_factor) /
                        col_dim;
                    ;
                    partial_factor += f_load.second *
                                      min(1 + footprint * load_slope, cost_factor) /
                                      col_dim;
                    ;
                } else {
                    partial_factor +=
                        f_load.second *
                        min(1 + initial_footprint * load_slope, cost_factor);
                }
            }

            if (!footprint.defined()) {
                return GroupAnalysis();
            }
        }
    }

    GroupAnalysis g_analysis(Cost(per_tile_cost.arith * estimate_tiles,
                                  partial_factor * estimate_tiles),
                             parallelism);
    if (!to_inline) {
        pair<map<string, Expr>, vector<pair<FStage, Expr>>> threads_estimates =
            eval_max_threads(g, show_analysis);
        g_analysis.thread_blocks = threads_estimates.first;
        g_analysis.n_threads = estimate_threads(g_analysis.thread_blocks);
        g_analysis.allocated_root = out_allocation;
        vector<pair<FStage, Expr>> mem_threads = threads_estimates.second;
        // now do the same thing for the compute costs and update....
        Expr occ = Int(32).max();
        Expr act_thr = Int(32).max();
        Expr par = Int(32).max();
        Expr min_threads = Int(32).max();
        const Expr &base_occupancy = make_const(Float(32), 0.1);
        Expr partial_factor = make_zero(Float(64));
        for (auto &mem : g.members) {
            string f_name = mem.func.name();
            if (g.inlined.find(f_name) != g.inlined.end())
                continue;
            bool is_output = (f_name == g.output.func.name() &&
                              mem.stage_num == g.output.stage_num);
            Cost stage_cost;
            // get its compute cost
            if (is_output)
                stage_cost = out_cost;
            else
                stage_cost = costs.stage_region_cost(
                    f_name, mem.stage_num, compute_regions[mem.func.name()], g.inlined);
            // get its threads
            Expr est_mem_threads;
            for (const auto &thr : mem_threads) {
                if ((thr.first.func.name() == f_name) &&
                    (thr.first.stage_num == mem.stage_num)) {
                    est_mem_threads = thr.second;
                    if (is_output)
                        g_analysis.threads_out = thr.second;
                    break;
                }
            }
            // now get its gpu stats
            vector<Expr> gpu_specs =
                estimate_occupancy(est_mem_threads, shared_mem, estimate_blocks);
            Expr mem_occupancy = gpu_specs[0];
            if ((can_prove(mem_occupancy < base_occupancy)))
                return GroupAnalysis();
            Expr mem_active_threads = gpu_specs[1];
            Expr mem_parallelism = gpu_specs[2];
            Expr nregs = gpu_specs[3];
            if (can_prove(nregs < 64))
                return GroupAnalysis();
            partial_factor += stage_cost.arith / (mem_occupancy * mem_active_threads);
            min_threads = min(est_mem_threads, min_threads);
            occ = min(occ, mem_occupancy);
            act_thr = min(act_thr, mem_active_threads);
            par = min(par, mem_parallelism);
        }
        g_analysis.cost.arith = partial_factor * estimate_tiles;
        g_analysis.threads_out = min_threads;
        g_analysis.occupancy = occ;
        g_analysis.active_threads = act_thr;
        g_analysis.parallelism = par;
        g_analysis.n_blocks = simplify(estimate_blocks);

    } else {
        g_analysis.threads_out = make_one(Int(64));
        g_analysis.n_threads = make_one(Int(64));
        g_analysis.occupancy = make_one(Int(64));
        g_analysis.active_threads = make_one(Int(64));
        g_analysis.parallelism = make_const(Int(32), 1);
        g_analysis.n_blocks = make_one(Int(64));
    }

    if (!g_analysis.n_threads.defined())
        return GroupAnalysis();
    g_analysis.shared_mem = shared_mem;
    g_analysis.simplify();
    return g_analysis;
}

void Partitioner::merge_groups(const GroupingChoice &choice,
                               const GroupConfig &eval,
                               Partitioner::Level level) {
    const Function &prod_f = get_element(dep_analysis.env, choice.prod);
    size_t num_stages = prod_f.updates().size() + 1;

    const FStage &child = choice.cons;
    Group &child_group = get_element(groups, child);

    for (size_t s = 0; s < num_stages; s++) {
        FStage cand(prod_f, s);
        Group &cand_group = get_element(groups, cand);
        child_group.members.insert(child_group.members.end(),
                                   cand_group.members.begin(),
                                   cand_group.members.end());

        if (level == Partitioner::Level::Inline) {
            for (const auto &stg : cand_group.members) {
                child_group.inlined.insert(stg.func.name());
            }
        } else {
            for (const auto &in : cand_group.inlined) {
                child_group.inlined.insert(in);
            }
        }
    }

    child_group.tile_sizes = eval.tile_sizes;

    // Update group costs.
    // We could just reuse the analysis from 'eval' since it was computed
    // by assuming the merge had happened.
    group_costs[child] = eval.analysis;
}

Partitioner::GroupConfig
Partitioner::evaluate_choice(Group &group, Partitioner::Level level) {
    // Create a group that reflects the grouping choice and evaluate the cost
    // of the group.

    GroupAnalysis group_analysis;
    map<string, Expr> best_tile_config;

    if (level == Partitioner::Level::Inline) {
        // Set the tile sizes to one along all dimensions of the consumer group
        map<string, Expr> tile_sizes;
        const Function &cons_f = group.output.func;
        // cout<<"cons f "<<group.output.func.name()<<endl;
        // cout<<"before dims"<<endl;
        const vector<Dim> &dims = get_stage_dims(cons_f, group.output.stage_num);
        //   cout<<"after dims"<<endl;
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            //   cout<<"dims var "<<dims[d].var<<endl;
            tile_sizes[dims[d].var] = 1;
        }

        group.tile_sizes = tile_sizes;
        group_analysis = analyze_group(group, false, true);
        best_tile_config = tile_sizes;

    } else {
        //   Group
        // check if the group is valid (skip boundary conditions)
        bool has_boundary_stages = check_for_boundary(group);
        if (has_boundary_stages) {
            return GroupConfig(best_tile_config, group_analysis);
        }
        pair<map<string, Expr>, GroupAnalysis> config =
            find_best_tile_config(group, false, false);
        best_tile_config = config.first;
        group_analysis = config.second;
    }

    return GroupConfig(best_tile_config, group_analysis);
}

Expr Partitioner::estimate_tile_benefit(const GroupAnalysis &old_grouping,
                                        const GroupAnalysis &new_grouping,
                                        bool final_tiles,
                                        bool ensure_parallelism) {

    if (ensure_parallelism &&
        (!new_grouping.parallelism.defined() ||
         !can_prove(new_grouping.parallelism >= arch_params.parallelism))) {

        return Expr();
    }
    if (ensure_parallelism && (!new_grouping.threads_out.defined() ||
                               !can_prove(new_grouping.threads_out >= 16))) {

        return Expr();
    }
    if (!old_grouping.cost.defined() || !new_grouping.cost.defined()) {
        return Expr();
    }

    if (can_prove(new_grouping.shared_mem > gparams.limit_shared_mem_per_block))
        return Expr();
    if (can_prove(new_grouping.n_threads > gparams.limit_threads_per_block))
        return Expr();

    if (final_tiles && can_prove(new_grouping.n_threads % 32 != 0))
        return Expr();
    // if(can_prove(new_grouping.allocated_root>2147483647)) return Expr();
    Expr mem_benefit, arith_benefit;
    if (final_tiles) {
        arith_benefit = old_grouping.cost.arith - new_grouping.cost.arith;
        mem_benefit = old_grouping.cost.memory /
                          (old_grouping.active_threads * old_grouping.occupancy) -
                      new_grouping.cost.memory /
                          (new_grouping.active_threads * new_grouping.occupancy);
    } else {
        mem_benefit = old_grouping.cost.memory - new_grouping.cost.memory;
        arith_benefit = old_grouping.cost.arith - new_grouping.cost.arith;
    }
    /* cout<<"ANALYSIS RESULTS"<<endl;
   cout<<"old grouping:"<<endl;
   cout<<"arith : "<<old_grouping.cost.arith<<endl;
   cout<<"mem : "<<old_grouping.cost.memory<<endl;
   cout<<"active threads :"<<old_grouping.active_threads<<endl;
   cout<<"occupancy : "<<old_grouping.occupancy<<endl;
   cout<<"===="<<endl;
   cout<<"new_grouping:"<<endl;
   cout<<"arith : "<<new_grouping.cost.arith<<endl;
   cout<<"mem : "<<new_grouping.cost.memory<<endl;
   cout<<"active threads :"<<new_grouping.active_threads<<endl;
   cout<<"occupancy : "<<new_grouping.occupancy<<endl;
   cout<<endl<<endl;*/
    // cout<<"arith benefit"<<simplify(arith_benefit)<<endl;
    // cout<<"mem benefit"<<simplify(mem_benefit)<<endl;

    Expr benefit;
    benefit = simplify(mem_benefit + arith_benefit);

    return (benefit);
}

Expr Partitioner::estimate_benefit(const GroupAnalysis &old_grouping,
                                   const GroupAnalysis &new_grouping,
                                   bool no_redundant_work,
                                   bool ensure_parallelism) {

    if (!old_grouping.cost.defined() || !new_grouping.cost.defined()) {
        return Expr();
    }

    Expr arith_benefit = old_grouping.cost.arith - new_grouping.cost.arith;
    if (no_redundant_work && !can_prove(arith_benefit >= 0)) {
        return Expr();
    }
    Expr mem_benefit = old_grouping.cost.memory - new_grouping.cost.memory;
    return simplify((mem_benefit + arith_benefit));
}

Expr Partitioner::estimate_benefit(
    const vector<pair<GroupingChoice, GroupConfig>> &new_grouping,
    bool no_redundant_work, bool ensure_parallelism, Partitioner::Level level) {

    set<FStage> old_groups;

    GroupAnalysis new_group_analysis(Cost(0, 0), Int(64).max());
    new_group_analysis.shared_mem = make_zero(Int(64));
    new_group_analysis.threads_out = Int(64).max();
    new_group_analysis.n_threads = make_one(Int(64));
    new_group_analysis.active_threads = Int(64).max();
    new_group_analysis.occupancy = make_one(Int(64));
    for (const auto &g : new_grouping) {
        const Function &prod_f = get_element(dep_analysis.env, g.first.prod);
        int num_prod_stages = prod_f.updates().size() + 1;
        for (int s = 0; s < num_prod_stages; s++) {
            FStage prod_s(prod_f, s);
            old_groups.insert(prod_s);
        }

        old_groups.insert(g.first.cons);

        GroupAnalysis analysisg = g.second.analysis;
        if (analysisg.defined()) {
            new_group_analysis.cost.arith += analysisg.cost.arith;
            new_group_analysis.shared_mem += new_group_analysis.shared_mem;
            new_group_analysis.threads_out =
                min(new_group_analysis.threads_out, analysisg.threads_out);
            new_group_analysis.active_threads =
                min(new_group_analysis.active_threads, analysisg.active_threads);
            new_group_analysis.n_threads =
                max(new_group_analysis.n_threads, analysisg.n_threads);
            new_group_analysis.cost.memory += analysisg.cost.memory;
            new_group_analysis.parallelism =
                min(new_group_analysis.parallelism, analysisg.parallelism);
            new_group_analysis.occupancy =
                min(new_group_analysis.occupancy, analysisg.occupancy);
        } else {
            new_group_analysis.cost = Cost();
            new_group_analysis.parallelism = Expr();
            break;
        }
    }
    new_group_analysis.simplify();

    GroupAnalysis old_group_analysis(Cost(0, 0), Int(64).max());
    old_group_analysis.shared_mem = make_zero(Int(64));
    old_group_analysis.threads_out = Int(64).max();
    old_group_analysis.n_threads = make_zero(Int(64));
    old_group_analysis.occupancy = make_one(Int(64));
    old_group_analysis.active_threads = Int(64).max();
    for (const auto &g : old_groups) {
        const auto &iter = group_costs.find(g);
        internal_assert(iter != group_costs.end());
        GroupAnalysis analysisg = iter->second;
        if (analysisg.defined()) {
            old_group_analysis.cost.arith += analysisg.cost.arith;
            old_group_analysis.cost.memory += analysisg.cost.memory;
            old_group_analysis.shared_mem += analysisg.shared_mem;
            old_group_analysis.parallelism =
                min(old_group_analysis.parallelism, analysisg.parallelism);
            old_group_analysis.threads_out =
                min(old_group_analysis.threads_out, analysisg.threads_out);
            old_group_analysis.active_threads =
                min(old_group_analysis.active_threads, analysisg.active_threads);

            old_group_analysis.occupancy =
                min(old_group_analysis.occupancy, analysisg.occupancy);
            old_group_analysis.n_threads =
                max(old_group_analysis.n_threads, analysisg.n_threads);
        } else {
            old_group_analysis.cost = Cost();
            old_group_analysis.parallelism = Expr();
            break;
        }
    }
    old_group_analysis.simplify();
    if (level == Partitioner::Level::Inline)
        return estimate_benefit(old_group_analysis, new_group_analysis,
                                no_redundant_work, ensure_parallelism);
    else
        return estimate_tile_benefit(old_group_analysis, new_group_analysis,
                                     no_redundant_work, ensure_parallelism);
}

map<string, Expr> Partitioner::bounds_to_estimates(const DimBounds &bounds) {
    map<string, Expr> estimates;
    for (const auto &bound : bounds) {
        estimates.emplace(bound.first, get_extent(bound.second));
    }
    return estimates;
}

map<FStage, map<string, Box>> Partitioner::group_storage_bounds() {
    map<FStage, map<string, Box>> group_storage_bounds;
    for (const pair<const FStage, Group> &gpair : groups) {
        const Group &g = gpair.second;
        DimBounds bounds = get_bounds_from_tile_sizes(g.output, g.tile_sizes);

        set<string> prods;
        for (const FStage &s : g.members) {
            prods.insert(s.func.name());
        }

        map<string, Box> reg_alloc =
            dep_analysis.regions_required(g.output.func, g.output.stage_num, bounds,
                                          prods, false, &costs.input_estimates);
        map<string, Box> group_alloc;
        for (const FStage &s : g.members) {
            const auto &iter = reg_alloc.find(s.func.name());
            if ((iter != reg_alloc.end()) &&
                (s.func.name() != g.output.func.name())) {
                group_alloc[s.func.name()] = iter->second;
            }
        }

        group_storage_bounds[gpair.first] = group_alloc;
    }

    return group_storage_bounds;
}

map<FStage, DimBounds> Partitioner::group_solo_bounds(const Group &groups) {
    map<FStage, DimBounds> group_bounds;
    //  for (const pair<const FStage, Group> &gpair : groups) {
    Group g = groups;
    map<FStage, DimBounds> mem_bounds;

    DimBounds bounds = get_bounds_from_tile_sizes(g.output, g.tile_sizes);

    set<string> prods;
    for (const FStage &s : g.members) {
        prods.insert(s.func.name());
    }

    map<string, Box> reg_computed =
        dep_analysis.regions_required(g.output.func, g.output.stage_num, bounds,
                                      prods, false, &costs.input_estimates);

    for (const FStage &s : all_stages) {
        // cout<<s.func.name()<<endl;
        const auto &iter = reg_computed.find(s.func.name());
        if (iter != reg_computed.end()) {
            map<string, Expr> tile_sizes;

            const vector<string> &args = s.func.args();
            for (size_t arg = 0; arg < args.size(); arg++) {
                tile_sizes[args[arg]] = get_extent(iter->second[arg]);
            }
            mem_bounds[s] = get_bounds_from_tile_sizes(s, tile_sizes);
        }
    }

    group_bounds = mem_bounds;

    return group_bounds;
}

map<FStage, map<FStage, DimBounds>> Partitioner::group_loop_bounds() {
    map<FStage, map<FStage, DimBounds>> group_bounds;
    for (const pair<const FStage, Group> &gpair : groups) {
        Group g = gpair.second;
        map<FStage, DimBounds> mem_bounds;

        DimBounds bounds = get_bounds_from_tile_sizes(g.output, g.tile_sizes);

        set<string> prods;
        for (const FStage &s : g.members) {
            prods.insert(s.func.name());
        }

        map<string, Box> reg_computed =
            dep_analysis.regions_required(g.output.func, g.output.stage_num, bounds,
                                          prods, true, &costs.input_estimates);

        for (const FStage &s : g.members) {
            const auto &iter = reg_computed.find(s.func.name());
            if (iter != reg_computed.end()) {
                map<string, Expr> tile_sizes;
                const vector<string> &args = s.func.args();
                for (size_t arg = 0; arg < args.size(); arg++) {
                    tile_sizes[args[arg]] = get_extent(iter->second[arg]);
                }
                mem_bounds[s] = get_bounds_from_tile_sizes(s, tile_sizes);
            }
        }

        group_bounds[gpair.first] = mem_bounds;
    }

    return group_bounds;
}

// We need to get the base name of the dimension for scheduling (i.e. it
// can't have any dots). For example, in split case, if "x" is the starting
// dimension name, after split(x, x0, xi, ...), we will end up with something
// like "x.x0" and  "x.xi". If we want to later schedule "x.x0", we need to
// pass "x0" instead of "x.x0".
string get_base_name(string name) {
    size_t dot_pos = name.rfind('.');
    if (dot_pos != string::npos) {
        return name.substr(dot_pos + 1);
    }
    return name;
}

// Return true if any of the values or args in 'def' refers to any of
// the inputs or outputs, with access function which depends on 'var'.
bool access_inputs_or_outputs(Definition def, VarOrRVar var,
                              const map<string, Type> &inputs,
                              const vector<Function> &outputs) {
    FindAllCalls find;
    def.accept(&find);

    for (size_t i = 0; i < find.call_args.size(); ++i) {
        const string &func = find.call_args[i].first;
        const vector<Expr> &args = find.call_args[i].second;

        if (inputs.find(func) == inputs.end()) {
            // Check if 'func' is an output
            bool is_output = std::find_if(outputs.begin(), outputs.end(),
                                          [&func](const Function &f) {
                                              return (f.name() == func);
                                          }) != outputs.end();
            if (!is_output) {
                // 'func' is neither an input or an output
                continue;
            }
        }

        // Check if any of the accesses to 'func' depends on 'var'
        for (const auto &arg : args) {
            if (expr_uses_var(arg, var.name())) {
                return true;
            }
        }
    }

    return false;
}

pair<VarOrRVar, VarOrRVar>
Partitioner::split_dim(const Group &g, Stage f_handle, int stage_num,
                       Definition def, bool is_group_output, VarOrRVar v,
                       const Expr &factor, string in_suffix, string out_suffix,
                       map<string, Expr> &estimates, AutoSchedule &sched) {
    // Create new variables for the split dimensions
    string arg_name = v.name();
    string inner_name = arg_name + in_suffix;
    string outer_name = arg_name + out_suffix;
    VarOrRVar inner(inner_name, v.is_rvar), outer(outer_name, v.is_rvar);

    {
        const auto &iter = sched.internal_vars.find(inner.name());
        if (iter == sched.internal_vars.end()) {
            sched.internal_vars.emplace(inner.name(), inner);
        } else {
            internal_assert(iter->second.is_rvar == inner.is_rvar);
        }
    }
    {
        const auto &iter = sched.internal_vars.find(outer.name());
        if (iter == sched.internal_vars.end()) {
            sched.internal_vars.emplace(outer.name(), outer);
        } else {
            internal_assert(iter->second.is_rvar == outer.is_rvar);
        }
    }

    // The default tail strategy is good enough for most use cases (see docs on
    // TailStrategy::Auto). However, the default of pure vars in update
    // definitions is RoundUp, which may introduces an out-of-bound error if it is
    // an access to inputs or outputs.
    //
    // We could have just used GuardWithIf when splitting pure vars in update
    // definition to ensure no out-of-bounds error. However, this is only
    // necessary, if the update definition involves accesses to inputs or outputs.
    // For other accesses, we could potentially use a more aggressive tail
    // strategy such as RoundUp or ShiftInwards. Note that if we use RoundUp or
    // ShiftInwards, any nested loops (generated by compute_at) will be affected
    // as well. However, since in the current auto-scheduler model, we always
    // compute_at at the group output, if the update definition is not the group
    // output, we do not need to care for the nested loops. If it is the update
    // definition of the group output however, we'd better make sure that no other
    // member of the groups accesses the inputs or outputs.

    TailStrategy strategy = TailStrategy::Auto;
    if ((stage_num > 0) && !v.is_rvar) {
        // TODO: It's safe to use RoundUp here if we know there are no
        // loads from any input, but at this point we've lost track of
        // loads from inputs that happen inside inlined Funcs.
        strategy = TailStrategy::RoundUp;
    }

    f_handle.split(v, outer, inner, factor, strategy);

    std::ostringstream oss;
    oss << "split(" << arg_name << ", " << outer_name << ", " << inner_name
        << ", " << factor;
    switch (strategy) {
    case TailStrategy::RoundUp:
        oss << ", TailStrategy::RoundUp)";
        break;
    case TailStrategy::GuardWithIf:
        oss << ", TailStrategy::GuardWithIf)";
        break;
    case TailStrategy::ShiftInwards:
        oss << ", TailStrategy::ShiftInwards)";
        break;
    case TailStrategy::Auto:
        oss << ")";
        break;
    default:
        internal_assert(false);
    }
    sched.push_schedule(f_handle.name(), stage_num, oss.str(),
                        {arg_name, outer_name, inner_name});

    const Expr &est = get_element(estimates, arg_name);
    internal_assert(est.defined());

    estimates[inner_name] = factor;
    estimates[outer_name] = simplify((est + factor - 1) / factor);
    estimates.erase(arg_name);

    return make_pair(inner, outer);
}

void Partitioner::vectorize_stage(const Group &g, Stage f_handle, int stage_num,
                                  Definition def, Function func,
                                  bool is_group_output, bool is_singleton,
                                  const Target &t, set<string> &rvars,
                                  map<string, Expr> &estimates,
                                  AutoSchedule &sched,
                                  vector<string> thread_dims) {
    vector<Dim> &dims = def.schedule().dims();
    int vec_dim_index = -1;
    // Set the vector length as the maximum of the natural vector size of all
    // values produced by the function.
    // int vec_len = 0;
    vector<int> vec_dim_indices;
    /*for (const auto &type : func.output_types()) {
      vec_len = std::max(vec_len, t.natural_vector_size(type));
  }*/
    bool flag_lane = false;
    int n_threads = 0;
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        string dim_name = get_base_name(dims[d].var);
        bool can_vectorize = true;
        if (rvars.find(dim_name) != rvars.end()) {
            can_vectorize = can_parallelize_rvar(dim_name, func.name(), def);
        }
        const auto &can_thread =
            find(thread_dims.begin(), thread_dims.end(), dim_name);
        const auto &iter = estimates.find(dim_name);
        if ((iter != estimates.end()) && iter->second.defined()) {
            if (can_vectorize) {
                if (can_thread != thread_dims.end() && can_prove(iter->second < 64) &&
                    vec_dim_indices.size() == 0) {
                    flag_lane = true;
                    for (int dd = 0; dd < d; dd++) {
                        if (!dims[dd].is_rvar())
                            flag_lane = false;
                    }
                    vec_dim_indices.push_back(d);
                    n_threads++;
                    if (n_threads >= 3)
                        break;
                } else if (can_thread != thread_dims.end()) {
                    vec_dim_indices.push_back(d);
                    n_threads++;
                    if (n_threads >= 3)
                        break;
                }
            }
        }
    }
    for (int d = 0; d < (int)vec_dim_indices.size(); d++) {
        if (vec_dim_indices[d] >= 0) {
            string vec_dim_name = get_base_name(dims[vec_dim_indices[d]].var);
            bool is_rvar = (rvars.find(vec_dim_name) != rvars.end());
            internal_assert(is_rvar == dims[vec_dim_indices[d]].is_rvar());

            VarOrRVar vec_var(vec_dim_name, is_rvar);

            if (flag_lane && d == 0) {

                // if(is_singleton&&(is_group_output)&&(vec_dim_indices.size()>=3)){
                if ((is_group_output) && (vec_dim_indices.size() >= 3)) {
                    sched.push_schedule(f_handle.name(), stage_num,
                                        "gpu_threads(" + vec_var.name() + ")",
                                        {vec_var.name()});
                    f_handle.gpu_threads(vec_var);
                } else {
                    sched.push_schedule(f_handle.name(), stage_num,
                                        "gpu_threads(" + vec_var.name() + ")",
                                        {vec_var.name()});

                    f_handle.gpu_threads(vec_var);
                }
            } else {
                f_handle.gpu_threads(vec_var);
                sched.push_schedule(f_handle.name(), stage_num,
                                    "gpu_threads(" + vec_var.name() + ")",
                                    {vec_var.name()});
            }
            if (vec_dim_index > 0) {
                user_warning << "Outer dim vectorization of var \"" << vec_dim_name
                             << "\" in function \"" << f_handle.name() << "\"\n";
            }
        }
    }

    if ((vec_dim_indices.size() == 0) && (is_group_output)) {
        f_handle.gpu_single_thread();
        sched.push_schedule(f_handle.name(), g.output.stage_num,
                            "gpu_single_thread()", {});
    }
}

void Partitioner::unroll_group_inner_stage(
    const Group &g, Stage f_handle, int stage_num, Definition def,
    Function func, bool is_group_output, const Target &t, set<string> &rvars,
    map<string, Expr> &estimates, AutoSchedule &sched,
    vector<string> thread_dims, vector<string> inner_non_threads) {
    vector<Dim> &dims = def.schedule().dims();
    int vec_dim_index = -1;
    vector<int> vec_dim_indices;
    bool flag_vec = true;
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        if (dims[d].for_type == ForType::Vectorized) {
            flag_vec = false;
            break;
        }
    }
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        if (!flag_vec)
            break;
        if (dims[d].for_type == ForType::GPUThread) {
            break;
        }
        string dim_name = get_base_name(dims[d].var);
        const auto &can_thread =
            find(thread_dims.begin(), thread_dims.end(), dim_name);
        const auto &non_blocked =
            find(inner_non_threads.begin(), inner_non_threads.end(), dim_name);
        const auto &iter = estimates.find(dim_name);
        bool is_bounded = false;
        if ((iter != estimates.end()) && iter->second.defined()) {
            if (!dims[d].is_rvar()) {
                for (const auto &bbs : g.output.func.schedule().bounds()) {
                    if (bbs.var == dim_name)
                        is_bounded = true;
                }
                if (!is_bounded)
                    continue;
            }

            if (can_thread == thread_dims.end() &&
                non_blocked == inner_non_threads.end() &&
                can_prove(iter->second <= 4)) {
                //         cout<<"Will unroll "<<iter->first<<endl;
                vec_dim_indices.push_back(d);
            }
        }
    }
    for (int d = 0; d < (int)vec_dim_indices.size(); d++) {
        if (vec_dim_indices[d] >= 0) {
            string vec_dim_name = get_base_name(dims[vec_dim_indices[d]].var);
            bool is_rvar = (rvars.find(vec_dim_name) != rvars.end());
            internal_assert(is_rvar == dims[vec_dim_indices[d]].is_rvar());

            VarOrRVar vec_var(vec_dim_name, is_rvar);

            sched.push_schedule(f_handle.name(), stage_num,
                                "unroll(" + vec_var.name() + ")", {vec_var.name()});
            f_handle.unroll(vec_var);

            if (vec_dim_index > 0) {
                user_warning << "Outer dim unrolling of var \"" << vec_dim_name
                             << "\" in function \"" << f_handle.name() << "\"\n";
            }
        }
    }
}

// Return true if the vars/rvars in 'ordering' are in the same order as the
// dim list.
inline bool operator==(const vector<Dim> &dims,
                       const vector<VarOrRVar> &ordering) {
    if (dims.size() !=
        ordering.size() + 1) {  // The dim list also contains '__outermost'
        return false;
    }
    for (size_t i = 0; i < ordering.size(); ++i) {
        if (dims[i].var != ordering[i].name()) {
            return false;
        }
    }
    return true;
}

// Return true if the vars/rvars in 'ordering' are not in the same order as the
// dim list.
inline bool operator!=(const vector<Dim> &dims,
                       const vector<VarOrRVar> &ordering) {
    return !(dims == ordering);
}
void Partitioner::reorder_dims(Stage f_handle, int stage_num, Definition def,
                               map<string, Expr> strides, AutoSchedule &sched,
                               map<string, Expr> sbounds,
                               vector<string> threads) {
    vector<Dim> &dims = def.schedule().dims();
    // cout<<f_handle.name()<<endl;
    internal_assert(dims.size() > 1);
    vector<pair<string, int>> order;
    // for(const auto &sb:sbounds) cout<<sb.first<<" "<<sb.second<<endl;
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        internal_assert(strides.find(dims[d].var) != strides.end());
    }
    // put the small extent rdoms first

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        string var_name = get_base_name(dims[d].var);

        const auto &iter = sbounds.find(var_name);
        bool is_thread =
            find(threads.begin(), threads.end(), var_name) != threads.end();
        if (iter != sbounds.end() && (!is_thread)) {
            if (can_prove(iter->second <= 4)) {
                pair<string, int> lord(iter->first, d);

                order.push_back(lord);
                strides.erase(iter->first);
            }
        }
    }
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        string var_name = get_base_name(dims[d].var);

        const auto &iter = sbounds.find(var_name);
        bool is_thread =
            find(threads.begin(), threads.end(), var_name) != threads.end();

        if (iter != sbounds.end() && (!is_thread)) {
            pair<string, int> lord(iter->first, d);
            const auto &already_set =
                (find(order.begin(), order.end(), lord) != order.end());
            if (!already_set) {
                order.push_back(lord);
                strides.erase(iter->first);
                // cout<<" order "<<iter->first<<" "<<oo<<endl;
            }
        }
        //  else if(iter!=sbounds.end()) cout<<iter->first<<" "<<iter->second<<endl;
    }

    // Iterate until all the dimensions have been assigned an order
    while (strides.size() > 0) {
        // Find the pure dimension (can be vars or rvars) with the smallest stride
        bool found_pure_dim = false;
        Expr min_pure_stride = Int(64).max();
        string min_pure_var;
        int min_pure_index = -1;
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            string var_name = get_base_name(dims[d].var);
            const auto &iter = strides.find(var_name);
            if ((iter != strides.end()) && dims[d].is_pure()) {
                const Expr &dim_stride = iter->second;
                internal_assert(dim_stride.defined());
                if (can_prove(dim_stride < min_pure_stride)) {
                    min_pure_stride = dim_stride;
                    min_pure_var = var_name;
                    min_pure_index = d;
                }
                found_pure_dim = true;
            }
        }
        if (found_pure_dim && min_pure_var.empty()) {
            // Since none of the pure strides can be proven as the minimum, we
            // should break here otherwise it may cause infinite loop.
            return;
        }

        // Check if the stride of the pure dimension is smaller than
        // the first impure dimension that has not yet been assigned
        // an order
        Expr min_impure_stride = Int(64).max();
        string min_impure_var;
        int min_impure_index = -1;
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            string var_name = get_base_name(dims[d].var);
            const auto &iter = strides.find(var_name);
            if ((iter != strides.end()) && !dims[d].is_pure()) {
                const Expr &dim_stride = iter->second;
                internal_assert(dim_stride.defined());
                if (can_prove(dim_stride < min_impure_stride)) {
                    min_impure_stride = dim_stride;
                    min_impure_var = var_name;
                    min_impure_index = d;
                    // Impure dimensions cannot be reordered relative to
                    // each other. Stop after encountering the first impure
                    // dimension.
                    break;
                }
            }
        }

        if (min_pure_var.empty() && min_impure_var.empty()) {
            // Since none of the pure and impure strides can be proven as the
            // minimum, we should break here otherwise it may cause infinite loop.
            return;
        }

        pair<string, int> curr_min_var;
        if (!min_impure_var.empty() &&
            can_prove(min_impure_stride < min_pure_stride)) {
            curr_min_var.first = min_impure_var;
            curr_min_var.second = min_impure_index;
            internal_assert(dims[min_impure_index].is_rvar());
        } else {
            curr_min_var.first = min_pure_var;
            curr_min_var.second = min_pure_index;
        }
        bool already_set = false;
        for (const auto &iti : order) {
            if (iti.first == curr_min_var.first)
                already_set = true;
        }
        if (!already_set) {
            order.push_back(curr_min_var);
            strides.erase(curr_min_var.first);
        }
    }

    vector<VarOrRVar> ordering;
    for (const auto &o : order) {
        VarOrRVar o_var(o.first, dims[o.second].is_rvar());
        //   cout<<o.first<<endl;
        ordering.push_back(o_var);
    }

    internal_assert(!ordering.empty());
    set<string> var_list = {ordering[0].name()};
    string var_order = ordering[0].name();
    for (size_t o = 1; o < ordering.size(); o++) {
        var_order += ", " + ordering[o].name();
        var_list.insert(ordering[o].name());
    }

    //  if (dims != ordering) {
    f_handle.reorder(ordering);
    sched.push_schedule(f_handle.name(), stage_num, "reorder(" + var_order + ")",
                        var_list);
    // }
}

// Visitor to find all the variables the depend on a variable.
class FindVarsUsingVar : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Let *let) override {
        if (expr_uses_vars(let->value, vars)) {
            vars.push(let->name);
        }
        let->value.accept(this);
        let->body.accept(this);
    }

public:
    Scope<> vars;

    FindVarsUsingVar(string var) {
        vars.push(var);
    }
};

void Partitioner::generate_group_cpu_schedule(
    const Group &og_group, const Target &t,
    const map<FStage, DimBounds> &group_loop_bounds,
    const map<string, Box> &group_storage_bounds, const set<string> &inlines,
    AutoSchedule &sched, bool will_fold) {
    std::string folded_fusion =
        Internal::get_env_variable("HL_AUTO_FOLDED_FUSION");
    bool use_folded_fusion_analysis = std::atoi(folded_fusion.c_str());
    Group g = og_group;
    if ((use_folded_fusion_analysis) && (will_fold))
        g = optimize_granularity(og_group, sched);

    string out_f_name = g.output.func.name();
    Function g_out = g.output.func;

    if (g.output.func.has_extern_definition()) {
        internal_assert(g.members.size() == 1);
        Func(g_out).compute_root();
        sched.push_schedule(g_out.name(), g.output.stage_num, "compute_root()", {});
        return;
    }

    // Get the estimates for stage bounds
    DimBounds stg_bounds = get_bounds(g.output);
    map<string, Expr> stg_estimates = bounds_to_estimates(stg_bounds);

    Stage f_handle = Stage(Func(g_out));

    // Get a function handle for scheduling the stage
    if (g.output.stage_num > 0) {
        //  //cout<<"asdasdas"<<endl;
        int stage_num = g.output.stage_num;
        int stage_pure = 0;
        Func(g_out).compute_root();
        sched.push_schedule(f_handle.name(), stage_pure, "compute_root()", {});
        // cout<<"num stage "<<stage_num<<endl;
        f_handle = Func(g_out).update(stage_num - 1);
    } else {
        Func(g_out).compute_root();
        sched.push_schedule(f_handle.name(), g.output.stage_num, "compute_root()",
                            {});
    }

    // Realize tiling and update the dimension estimates
    vector<VarOrRVar> outer_dims;
    vector<VarOrRVar> outer_dims_non_blocked;
    vector<VarOrRVar> inner_dims;
    vector<VarOrRVar> inner_dims_non_threads;
    vector<string> thread_dims;
    vector<string> thread_dims_out;
    vector<string> block_dims;
    Expr def_par = 1;
    // Get the definition corresponding to the stage
    Definition def = get_stage_definition(g_out, g.output.stage_num);

    // 'dims' will get modified since we are going to apply the schedules
    // (e.g. tiling, reordering, etc.)
    vector<Dim> &dims = def.schedule().dims();

    // Keep track of the rvars
    set<string> rvars;
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        if (dims[d].is_rvar()) {
            rvars.insert(get_base_name(dims[d].var));
        }
    }

    set<string> thread_ests = dims_to_tile(g.output);
    vector<string> dim_vars(dims.size() - 1);
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        dim_vars[d] = get_base_name(dims[d].var);
    }
    for (const auto &var : dim_vars) {
        bool is_rvar = (rvars.find(var) != rvars.end());
        VarOrRVar v(var, is_rvar);

        const auto &iter = g.tile_sizes.find(var);
        if ((iter != g.tile_sizes.end()) &&
            get_element(stg_estimates, var).defined() &&
            can_prove(get_element(stg_estimates, var) > iter->second)) {
            const Expr &tile_size = iter->second;
            if (can_prove(tile_size == 1)) {

                if (thread_ests.size() >= 2) {
                    outer_dims.push_back(v);
                    outer_dims_non_blocked.push_back(v);
                } else {
                    outer_dims.push_back(v);
                    block_dims.push_back(v.name());
                }
            } else {

                pair<VarOrRVar, VarOrRVar> tile_vars =
                    split_dim(g, f_handle, g.output.stage_num, def, true, v, tile_size,
                              "_i", "_o", stg_estimates, sched);

                inner_dims.push_back(tile_vars.first);
                if (thread_ests.find(var) != thread_ests.end()) {
                    thread_dims.push_back(v.name());
                    thread_dims_out.push_back(tile_vars.first.name());
                    block_dims.push_back(tile_vars.second.name());
                }
                outer_dims.push_back(tile_vars.second);

                if (is_rvar) {
                    rvars.erase(var);
                    rvars.insert(tile_vars.first.name());
                    rvars.insert(tile_vars.second.name());
                }
            }
        } else {

            if ((thread_ests.size() <= 1) && (!v.is_rvar)) {

                outer_dims.push_back(v);
                block_dims.push_back(v.name());
            } else {

                inner_dims_non_threads.push_back(v);
            }
        }
    }

    if (!outer_dims.empty()) {

        vector<VarOrRVar> ordering;
        for (const auto &v : inner_dims_non_threads) {
            ordering.push_back(v);
        }
        for (const auto &v : inner_dims) {
            ordering.push_back(v);
        }
        for (const auto &v : outer_dims) {
            ordering.push_back(v);
        }
        set<string> var_list;
        string var_order = ordering[0].name();
        for (size_t o = 1; o < ordering.size(); o++) {
            var_order += ", " + ordering[o].name();
            var_list.insert(ordering[o].name());
        }

        if (dims != ordering) {
            f_handle.reorder(ordering);
            sched.push_schedule(f_handle.name(), g.output.stage_num,
                                "reorder(" + var_order + ")", var_list);
        }
    }
    bool is_singleton = is_singleton_group(g);
    // cout<<"is singleton"<<g.members.size()<<" output
    // "<<g.output.stage_num<<endl;
    vectorize_stage(g, f_handle, g.output.stage_num, def, g_out, true,
                    is_singleton, t, rvars, stg_estimates, sched,
                    thread_dims_out);
    unroll_group_inner_stage(g, f_handle, g.output.stage_num, def, g_out, true, t,
                             rvars, stg_estimates, sched, thread_dims_out,
                             block_dims);
    bool nested_parallelism = true;
    if (nested_parallelism) {
        int dim_start = dims.size() - 2;
        string seq_var = "";
        int n_blocks = 0;
        for (int d = dim_start; d >= 0; d--) {
            if (dims[d].for_type == ForType::GPUThread) {
                break;
            }

            string var = get_base_name(dims[d].var);
            bool is_rvar = (rvars.find(var) != rvars.end());
            internal_assert(is_rvar == dims[d].is_rvar());
            VarOrRVar v(var, is_rvar);

            if (is_rvar && !can_parallelize_rvar(var, g_out.name(), def)) {
                if (seq_var == "") {
                    seq_var = var;
                }
                continue;
            }

            const auto &iter = stg_estimates.find(var);

            const auto &iter2 = find(block_dims.begin(), block_dims.end(), var);
            if ((iter != stg_estimates.end()) && iter->second.defined() &&
                (iter2 != block_dims.end())) {
                if (seq_var != "") {
                    VarOrRVar seq(seq_var, (rvars.find(seq_var) != rvars.end()));
                    f_handle.reorder(seq, v);
                    sched.push_schedule(f_handle.name(), g.output.stage_num,
                                        "reorder(" + seq_var + ", " + var + ")",
                                        {seq_var, var});
                }
                if (n_blocks < 3) {
                    f_handle.gpu_blocks(v);
                    n_blocks++;
                    sched.push_schedule(f_handle.name(), g.output.stage_num,
                                        "gpu_blocks(" + var + ")", {var});
                }
                def_par = simplify(def_par * iter->second);
            } else {
                continue;
                //   break;
            }
        }
    }

    if (can_prove(def_par < arch_params.parallelism)) {
        user_warning << "Insufficient parallelism for " << f_handle.name() << '\n';
    }

    int tile_inner_index = dims.size() - outer_dims.size() - 1;
    VarOrRVar tile_inner_var("", false);
    if (!outer_dims.empty()) {
        string var_name = get_base_name(dims[tile_inner_index].var);
        bool is_rvar = (rvars.find(var_name) != rvars.end());
        tile_inner_var = VarOrRVar(var_name, is_rvar);
    }
    VarOrRVar intra_tile_var = tile_inner_var;
    // for luts just use gpu_thread;
    if (g.tile_sizes.begin() == g.tile_sizes.end()) {
        f_handle.gpu_single_thread();
        sched.push_schedule(f_handle.name(), g.output.stage_num,
                            "gpu_single_thread()", {});
    }
    for (const FStage &mem : g.members) {
        // Skip member stages that have been inlined or stage that is the
        // output stage of the group

        bool is_function =
            (dep_analysis.env.find(mem.func.name()) != dep_analysis.env.end());
        if (!is_function)
            continue;

        if ((inlines.find(mem.func.name()) != inlines.end()) ||
            (mem.func.name() == g_out.name())) {

            continue;
        }

        // Get the definition corresponding to the stage
        // cout<<"before def"<<endl;
        Definition mem_def = get_stage_definition(mem.func, mem.stage_num);

        // Get the estimates for the dimensions of the member stage
        // cout<<"before bounds"<<endl;
        // this is a bug with imparams that at the end get scheduled for some weird
        // reason
        if (group_loop_bounds.find(mem) == group_loop_bounds.end())
            continue;
        map<string, Expr> mem_estimates =
            bounds_to_estimates(get_element(group_loop_bounds, mem));
        //        cout<<"failed bounds"<<endl;
        set<string> mem_rvars;
        vector<Dim> &mem_dims = mem_def.schedule().dims();
        for (int d = 0; d < (int)mem_dims.size() - 1; d++) {
            if (mem_dims[d].is_rvar()) {
                mem_rvars.insert(get_base_name(mem_dims[d].var));
            }
        }

        Stage mem_handle = Stage(Func(mem.func));
        if (mem.stage_num > 0)
            mem_handle = Func(mem.func).update(mem.stage_num - 1);
        if (dims.size() > 2) {
            map<string, Expr> mem_strides =
                analyze_spatial_locality(mem, group_storage_bounds, inlines);
            if (!mem_strides.empty()) {

                // DimBounds stgbounds =
                //  cout<<"before dims"<<endl;
                map<string, Expr> sbounds = find_dims(mem, mem.stage_num);
                //   cout<<"after dims"<<endl;
                reorder_dims(mem_handle, mem.stage_num, mem_def, mem_strides, sched,
                             sbounds, thread_dims);
                //  cout<<"after reorder"<<endl;
            }
        }
        string sanitized_g_out;
        string clevel;
        Function comp_at;
        comp_at = g.output.func;
        sanitized_g_out = g_out.name();
        bool found = false;
        if (use_folded_fusion_analysis) {
            // we need to find the stages where the member is computed_at
            // we need to find the level where the member is computed_at
            sanitized_g_out = get_expr_str(mem.compute_stage);
            clevel = get_expr_str(mem.compute_level);
            for (const auto &memb : g.members) {
                if (sanitized_g_out == memb.func.name()) {
                    //  cout<<"sanitized_g_out"<<memb.func.name()<<endl;
                    //    cout<<" mem func "<<mem.func.name()<<endl;
                    comp_at = memb.func;
                    if (memb.stage_num != memb.func.updates().size())
                        continue;
                    // now find the var/rvar
                    const vector<Dim> &dims = get_stage_dims(memb.func, memb.stage_num);
                    for (int i = 0; i < (int)dims.size(); i++) {
                        //      cout<<"dims[ ] "<<dims[i].var<<endl;
                        if (dims[i].var == clevel) {
                            found = true;
                            //            cout<<"THIS WAS VAR "<<clevel<<endl;
                            //            cout<<"found at i "<<i<<"
                            //"<<dims[i].var<<endl;
                            if (i + 1 < (int)dims.size() - 1) {
                                tile_inner_var =
                                    VarOrRVar(dims[i + 1].var, dims[i + 1].is_rvar());
                                clevel = get_base_name(dims[i + 1].var);
                                //                cout<<" now is "<<clevel<<endl;
                                break;
                            } else if ((i + 1) == ((int)dims.size() - 1)) {
                                tile_inner_var = VarOrRVar(dims[i].var, dims[i].is_rvar());
                                clevel = get_base_name(dims[i].var);
                                //                      cout<<" still is "<<clevel<<endl;
                                break;
                            }
                        }
                    }
                }
            }
            if (!found) {
                // cout<<"not found so setting to intra tile"<<endl;
                tile_inner_var = intra_tile_var;
                clevel = tile_inner_var.name();
                sanitized_g_out = g_out.name();
            }
        } else {
            tile_inner_var = intra_tile_var;
            clevel = tile_inner_var.name();
            sanitized_g_out = g_out.name();
        }
        if (mem.stage_num > 0) {
            mem_handle = Func(mem.func).update(mem.stage_num - 1);
        } else {
            if (!outer_dims.empty()) {
                if (tile_inner_var.is_rvar) {
                    Func(mem.func).compute_at(Func(comp_at), tile_inner_var.rvar);
                } else {
                    Func(mem.func).compute_at(Func(comp_at), tile_inner_var.var);
                    //     if((will_fold)&&(use_folded_fusion_analysis))
                    //     Func(mem.func).store_in(MemoryType::Register);
                }
                // string sanitized_g_out = get_sanitized_name(g_out.name());
                //  string sanitized_g_out=get_expr_str(mem.compute_stage);
                sanitized_g_out = get_sanitized_name(sanitized_g_out);
                clevel = get_sanitized_name(clevel);
                sched.push_schedule(mem_handle.name(), mem.stage_num,
                                    "compute_at(" + sanitized_g_out + ", " + clevel +
                                        ")",
                                    {sanitized_g_out, clevel});
                //  sched.push_schedule(mem_handle.name(), mem.stage_num,
                // "store_in(MemoryType::Register)", {});
            } else {
                user_warning << "Degenerate tiling. No dimensions are tiled" << '\n';
                user_warning << "Computing \"" << mem.func.name() << "\" at root"
                             << '\n';
                Func(mem.func).compute_root();
                sched.push_schedule(mem_handle.name(), mem.stage_num, "compute_root()",
                                    {});
            }
        }
        // cout<<"after spatial"<<endl;
        bool is_singleton = is_singleton_group(g);
        bool is_output_st = g.output.func.name() == mem.func.name();
        if (!found)
            vectorize_stage(g, mem_handle, mem.stage_num, mem_def, mem.func,
                            is_output_st, is_singleton, t, mem_rvars, mem_estimates,
                            sched, thread_dims);
        unroll_group_inner_stage(g, mem_handle, mem.stage_num, mem_def, mem.func,
                                 false, t, mem_rvars, mem_estimates, sched,
                                 thread_dims, block_dims);
        //  unroll_group_outer_stage(g, mem_handle, mem.stage_num, mem_def,
        //  mem.func, false,
        //   t, mem_rvars, mem_estimates, sched,thread_dims,block_dims);
        // cout<<"finished vector"<<endl;
    }
    /* if((g.output.stage_num > 0)&&(g.tile_sizes.size()!=0)) {
     FStage g_pure(g.output.func,g.output.stage_num-1);
     DimBounds stg_bounds = get_bounds(g_pure);
     map<string, Expr> stg_estimates = bounds_to_estimates(stg_bounds);

     f_handle = Func(g_out);
     vector<VarOrRVar> outer_dims;
     vector<VarOrRVar> outer_dims_non_blocked;
     vector<VarOrRVar> inner_dims;
     vector<VarOrRVar> inner_dims_non_threads;
     vector<string> thread_dims;
     vector<string> thread_dims_out;
     vector<string> block_dims;
     Expr def_par = 1;
     Definition def = get_stage_definition(g_out, g.output.stage_num-1);

       // 'dims' will get modified since we are going to apply the schedules
       // (e.g. tiling, reordering, etc.)
     vector<Dim> &dims = def.schedule().dims();

       // Keep track of the rvars
     set<string> rvars;
     for (int d = 0; d < (int)dims.size() - 1; d++) {
       if (dims[d].is_rvar()) {
         rvars.insert(get_base_name(dims[d].var));
       }
     }
     vector<string> dim_vars(dims.size() - 1);
     for (int d = 0; d < (int)dims.size() - 1; d++) {
       dim_vars[d] = get_base_name(dims[d].var);
     }
     vector<string> pure_tile_dims=dims_to_tile(g_pure);
     map<string,Expr> root_tile_sizes;
     const Expr &tile_size8 =make_const(Int(32),8);
     const Expr &tile_size32 =make_const(Int(32),32);
     for(const auto &ptd:pure_tile_dims){
      if(pure_tile_dims.size()<=2) root_tile_sizes.emplace(ptd,tile_size32);
      else root_tile_sizes.emplace(ptd,tile_size8);

    }
    for (const auto &var : dim_vars) {
     bool is_rvar = (rvars.find(var) != rvars.end());
     VarOrRVar v(var, is_rvar);
       //    Expr stg_QQ=get_element(stg_estimates, var);
       //const Expr &root_tile_size=generate_root_tile(stg_QQ);
       //const Expr &root_tile_size=make_const(Int(32),2);

     const auto &iter = root_tile_sizes.find(var);
     if ((iter != root_tile_sizes.end())) {
       const Expr tile_size=iter->second;

       if(var=="dx")  std::cout<<"in stage "<<f_handle.name()<<" "<<var<<" tile
 is "<<tile_size<<"but extent is "<<get_element(stg_estimates, var)<<std::endl;
       pair<VarOrRVar, VarOrRVar> tile_vars =
       split_dim(g, f_handle, g.output.stage_num-1, def, true, v,
         tile_size, "_i", "_o", stg_estimates, sched);

       inner_dims.push_back(tile_vars.first);
       thread_dims.push_back(v.name());
       thread_dims_out.push_back(tile_vars.first.name());
       outer_dims.push_back(tile_vars.second);
       block_dims.push_back(tile_vars.second.name());
       if (is_rvar) {
         rvars.erase(var);
         rvars.insert(tile_vars.first.name());
         rvars.insert(tile_vars.second.name());
       }

     } else {
       const auto &iter = root_tile_sizes.find(var);
       if ((iter == root_tile_sizes.end())) cout<<"was not tiled"<<endl;
       else cout<<iter->second<<endl;
       std::cout<<"in stage "<<f_handle.name()<<" "<<var<<"  not tiled but
 extent is "<<get_element(stg_estimates, var)<<std::endl;
       inner_dims_non_threads.push_back(v);


     }

   }

        // Reorder the tile dimensions
   if (!outer_dims.empty()) {

     vector<VarOrRVar> ordering;
     for (const auto &v : inner_dims_non_threads) {
       ordering.push_back(v);
     }
     for (const auto &v : inner_dims) {
       ordering.push_back(v);
     }
     for (const auto &v : outer_dims_non_blocked) {
       ordering.push_back(v);
     }
     for (const auto &v : outer_dims) {
       ordering.push_back(v);
     }
     set<string> var_list;
     string var_order = ordering[0].name();
     for (size_t o = 1; o < ordering.size(); o++) {
       var_order += ", " + ordering[o].name();
       var_list.insert(ordering[o].name());
     }

     if (dims != ordering) {
       f_handle.reorder(ordering);
       sched.push_schedule(f_handle.name(), g.output.stage_num-1,
         "reorder(" + var_order + ")", var_list);
     }
   }

   vectorize_stage(g, f_handle, g.output.stage_num-1, def, g_out, true, t,
     rvars, stg_estimates, sched,thread_dims_out);
   unroll_stage(g, f_handle, g.output.stage_num-1, def, g_out, true, t,
     rvars, stg_estimates, sched,thread_dims_out,block_dims);

   bool nested_parallelism = true;
   if (nested_parallelism) {
     int dim_start = dims.size() - 2;
     string seq_var = "";
     int n_blocks=0;
     for (int d = dim_start; d >= 0; d--) {
       if (dims[d].for_type == ForType::GPUThread) {
         break;
       }

       string var = get_base_name(dims[d].var);
       bool is_rvar = (rvars.find(var) != rvars.end());
       internal_assert(is_rvar == dims[d].is_rvar());
       VarOrRVar v(var, is_rvar);

       if (is_rvar && !can_parallelize_rvar(var, g_out.name(), def)) {
         if (seq_var == "") {
           seq_var = var;
         }
         continue;
       }
       const auto &iter = stg_estimates.find(var);
               //const auto &iter2 = block_dims.find(var);
       const auto &iter2 = find(block_dims.begin(),block_dims.end(),var);
       if ((iter != stg_estimates.end()) &&
 iter->second.defined()&&(iter2!=block_dims.end()))  { if (seq_var != "") {
           VarOrRVar seq(seq_var, (rvars.find(seq_var) != rvars.end()));
           f_handle.reorder(seq, v);
           sched.push_schedule(f_handle.name(), g.output.stage_num-1,
             "reorder(" + seq_var + ", " + var + ")",
             {seq_var, var});
         }
         if(n_blocks<3)
         {
           f_handle.gpu_blocks(v);
           n_blocks++;
           sched.push_schedule(f_handle.name(), g.output.stage_num-1,
             "gpu_blocks(" + var + ")", {var});
         }
         def_par = simplify(def_par * iter->second);
       } else {
         continue;
                //   break;
       }
     }
   }
 }*/
    if ((g.output.stage_num > 0) && (g.tile_sizes.size() == 0)) {
        FStage g_pure(g.output.func, g.output.stage_num - 1);
        f_handle.gpu_single_thread();

        sched.push_schedule(f_handle.name(), g.output.stage_num - 1,
                            "gpu_single_thread()", {});
    }

    // cout<<"finished scheduling"<<endl;
    // cout<<"==================="<<endl<<endl;
}
// map<string, Expr> bounds_to_estimates(const DimBounds &bounds);

void Partitioner::generate_cpu_schedule(const Target &t, AutoSchedule &sched) {
    // Grab the group bounds early as they rely on the dimensions of the group
    // outputs which will be altered by modifying schedules.
    map<FStage, map<FStage, DimBounds>> loop_bounds = group_loop_bounds();
    map<FStage, map<string, Box>> storage_bounds = group_storage_bounds();
    set<string> will_fold;
    set<string> inlines;
    // Mark all functions that are inlined.
    for (const pair<FStage, Group> &g : groups) {
        cout << "g name " << g.first.func.name() << endl;
        if (g.second.members.size() - g.second.inlined.size() > 1) {
            GroupAnalysis asda = analyze_group(g.second, false, false);
            // GroupAnalysis asda=analyze_group(g,false,false);
            cout << "GROUP OF " << g.second.output.func.name() << endl;
            // if(g.members.size()-g.inlined.size()-g.output.stage_num>1){
            //   for(const auto &gm:g.members)  cout<<gm.func.name()<<endl;
            //  cout<<"GROUP OF "<<g.output.func.name()<<endl;
            // cout<<"MEM "<<g.members.size()<<" num "<<g.output.stage_num<<endl;
            // GroupAnalysis asda=analyze_group(g,false,false);
            cout << "SH MEM " << asda.shared_mem << endl;
            cout << " ACT THR " << asda.active_threads << endl;
            cout << " OCC " << asda.occupancy << endl;
            Expr thresh = make_const(Float(32), 0.3);
            if (can_prove(asda.shared_mem > 2 * 16384) ||
                can_prove(asda.occupancy < thresh) ||
                can_prove(asda.active_threads > 900))
                will_fold.insert(g.first.func.name());
            // }
        }

        for (const string &inline_func : g.second.inlined) {
            inlines.insert(inline_func);
            total_inlines++;
            cout << "inlined " << inline_func << endl;
        }
    }
    /* cout<<"================================="<<endl;

   for(const auto &db:loop_bounds){

       cout<<"GROUP OF "<<db.first<<endl;
       for(const auto &mb:db.second){
            cout<<"member "<<mb.first<<" bounds "<<endl;
           map<string, Expr> ests=bounds_to_estimates(mb.second);
           for(const auto &es:ests) cout<<"var "<<es.first <<" est
   "<<es.second<<endl;
       }
   }
   cout<<"EXTENTS"<<endl;
   for(const auto &db:loop_bounds){

       cout<<"GROUP OF "<<db.first<<endl;
       for(const auto &mb:db.second){
           DimBounds bb=get_bounds(mb.first);
            cout<<"member "<<mb.first<<" bounds "<<endl;
           map<string, Expr> ests=bounds_to_estimates(bb);
           for(const auto &es:ests) cout<<"var "<<es.first <<" est
   "<<es.second<<endl;
       }

   }
   cout<<"====== STORAGE =========="<<endl;
   for(const auto &db:storage_bounds){

       cout<<"GROUP OF "<<db.first<<endl;
       for(const auto &mb:db.second){
            cout<<"member "<<mb.first<<" bounds "<<mb.second<<endl;
       }
   }
   cout<<"===================="<<endl;*/
    // TODO: Inlining functions with update definitions has different
    // behavior than pure functions. They may need to be computed above
    // the innermost vector loop to avoid complications with varying
    // extents across different vector lanes.
    //
    // Since the default schedule is compute inline, we don't need to
    // explicitly call compute_inline() on the function.

    // Realize schedule for each group in the pipeline.
    for (const auto &g : groups) {
        generate_group_cpu_schedule(
            g.second, t, get_element(loop_bounds, g.first),
            get_element(storage_bounds, g.first), inlines, sched,
            will_fold.find(g.first.func.name()) != will_fold.end());
    }
}

Expr Partitioner::find_max_access_stride(const Scope<> &vars,
                                         const string &func_acc,
                                         const vector<Expr> &acc_exprs,
                                         const Box &buffer_bounds) {
    size_t num_storage_dims = 0;
    Expr bytes_per_ele = make_zero(Int(64));

    // Get the number of dimensions of the allocated storage and the
    // number of bytes required to store a single value of func_acc.
    const auto &iter = dep_analysis.env.find(func_acc);
    if (iter != dep_analysis.env.end()) {
        const Function &f = iter->second;
        for (const auto &e : f.values()) {
            bytes_per_ele += e.type().bytes();
        }
        num_storage_dims = f.schedule().storage_dims().size();
    } else {
        bytes_per_ele = get_element(costs.inputs, func_acc).bytes();
        num_storage_dims = buffer_bounds.size();
    }

    Expr curr_stride = bytes_per_ele;
    Expr stride = make_zero(Int(64));

    internal_assert(num_storage_dims <= acc_exprs.size());
    for (size_t sdim = 0; sdim < num_storage_dims; sdim++) {
        // Check if the access expression depends on any of the loop variables
        // in 'vars'. Expressions that do not involve the variable have stride 0.
        if (expr_uses_vars(acc_exprs[sdim], vars)) {
            stride = max(stride, curr_stride);
        }

        const Interval &dim_range = buffer_bounds[sdim];
        Expr dim_extent = get_extent(dim_range);
        if (!dim_extent.defined()) {
            return Expr();
        }
        curr_stride *= dim_extent;
    }

    return simplify(stride);
}

map<string, Expr>
Partitioner::analyze_spatial_locality(const FStage &stg,
                                      const map<string, Box> &allocation_bounds,
                                      const set<string> &inlines) {
    internal_assert(!stg.func.has_extern_definition());
    // Handle inlining. When a function is inlined into another, the stride of
    // the accesses should be computed on the expression post inlining.
    // For example:
    // f(x, y) = ...;
    // g(x, y) = f(y, x); // transpose
    // h(x, y) = g(y, x); // transpose
    //
    // If both g and f are inlined into h, then the resulting expression for h
    // will look like:
    // h(x, y) = f(x, y);
    //
    // Computing the stride of a loop over x in the function h will be incorrect
    // if inlining is not taken into account.

    // Get all the allocations accessed in the definition corresponding to 'stg'.
    FindAllCalls find;
    Definition def = get_stage_definition(stg.func, stg.stage_num);
    // Perform inlining on the all the values and the args in the stage.
    for (auto &val : def.values()) {
        val = perform_inline(val, dep_analysis.env, inlines, dep_analysis.order);
    }
    for (auto &arg : def.args()) {
        arg = perform_inline(arg, dep_analysis.env, inlines, dep_analysis.order);
    }
    def.accept(&find);

    // Arguments on the left hand side might themselves involve accesses
    // to allocations and thus need to be accounted for when computing the
    // strides along each dimension.
    vector<pair<string, vector<Expr>>> &call_args = find.call_args;
    // Account for the spatial locality of the store. Add the access on the
    // left hand side to call_args.
    call_args.push_back(make_pair(stg.func.name(), def.args()));

    // Map for holding the strides across each dimension
    map<string, Expr> var_strides;
    const vector<Dim> &dims = def.schedule().dims();

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        // Get all the variables involving the dimension in the definition.
        FindVarsUsingVar dep_vars(dims[d].var);
        def.accept(&dep_vars);

        // Accumulate the stride of each access to a loop dimension.
        Expr total_stride = 0;
        for (const pair<string, vector<Expr>> &call : call_args) {
            Box call_alloc_reg;
            const auto &iter = allocation_bounds.find(call.first);
            if (iter != allocation_bounds.end()) {
                call_alloc_reg = iter->second;
            } else {
                call_alloc_reg = get_element(pipeline_bounds, call.first);
            }
            Expr current_stride = find_max_access_stride(dep_vars.vars, call.first,
                                                         call.second, call_alloc_reg);
            if (!current_stride.defined()) {
                return map<string, Expr>();
            }
            total_stride += current_stride;
        }
        var_strides.emplace(dims[d].var, simplify(total_stride));
    }

    return var_strides;
}

// Verify that function 'f' does not have partially specified schedules/bounds.
// The current auto scheduler cannots handle such cases.
void validate_no_partial_schedules(const Function &f) {
    if (f.has_extern_definition()) {
        return;
    }

    // Verify no compute_root or bounds are specified
    user_assert(f.schedule().compute_level().is_inlined())
        << "AutoSchedule: cannot auto-schedule function \"" << f.name()
        << "\" since it is scheduled to be computed at root\n";
    /*  user_assert(f.schedule().bounds().empty())
        << "AutoSchedule: cannot auto-schedule function \"" << f.name()
        << "\" since it has partially specified bounds\n";
*/
    int num_stages = f.updates().size() + 1;
    for (int stage = 0; stage < num_stages; ++stage) {
        const Definition &def = get_stage_definition(f, stage);
        const StageSchedule &schedule = def.schedule();

        // Verify no splits are specified
        user_assert(schedule.splits().empty())
            << "AutoSchedule: cannot auto-schedule function \"" << f.name()
            << "\" since it has partially specified schedules at stage " << stage
            << "\n";

        // Verify that none of the dimensions are scheduled to be parallelized or
        // vectorized, or unrolled.
        for (const auto &d : schedule.dims()) {
            user_assert(d.for_type == ForType::Serial)
                << "AutoSchedule: cannot auto-schedule function \"" << f.name()
                << "\" since stage " << stage << " is not serial at dim " << d.var
                << "\n";
        }

        if (stage == 0) {
            // Since we can only specialize on a Func, we only need to check for no
            // specializations for the initial stage.
            user_assert(def.specializations().empty())
                << "AutoSchedule: cannot auto-schedule function \"" << f.name()
                << "\" since it has specializations\n";

            // Verify that there is no loop reordering on the initial definition
            // (i.e. the Vars in the dim list should be in the same order as
            // the args in the LHS of the definition).
            internal_assert(schedule.dims().size() - 1 == def.args().size());
            for (size_t i = 0; i < def.args().size(); ++i) {
                const Variable *arg = def.args()[i].as<Variable>();
                internal_assert(arg);
                user_assert(arg->name == schedule.dims()[i].var)
                    << "AutoSchedule: cannot auto-schedule function \"" << f.name()
                    << "\" since dim \"" << arg->name << "\" at stage " << stage
                    << " has been reordered\n";
            }
        } else {
            // Verify that there is no loop reordering on the update definition
            // (i.e. the Vars in the dim list should be in the same order as
            // the args in the LHS of the definition, the RVars in the dim list
            // should be in the same order as the RVars in the rvar list, and
            // all RVars should come before all Vars).

            const vector<Dim> &dims = schedule.dims();
            const vector<ReductionVariable> &rvars = schedule.rvars();
            const vector<Expr> &args = f.definition().args();
            internal_assert(dims.size() - 1 >= rvars.size());

            for (size_t i = 0; i < rvars.size(); ++i) {
                const Dim &d = dims[i];
                user_assert(d.is_rvar() && (d.var == rvars[i].var))
                    << "AutoSchedule: cannot auto-schedule function \"" << f.name()
                    << "\" since dim \"" << i << "\" at stage " << stage
                    << " has been reordered\n";
            }

            internal_assert(dims.size() - rvars.size() - 1 <= args.size());
            int last_index = -1;
            for (int i = rvars.size(); i < (int)dims.size() - 1; ++i) {
                const Dim &d = dims[i];
                user_assert(!d.is_rvar())
                    << "AutoSchedule: cannot auto-schedule function \"" << f.name()
                    << "\" since dim \"" << i << "\" at stage " << stage
                    << " has been reordered\n";

                const auto &iter =
                    std::find_if(args.begin(), args.end(), [&d](const Expr &arg) {
                        const Variable *v = arg.as<Variable>();
                        return (d.var == v->name);
                    });
                internal_assert(iter != args.end());
                int current_index = iter - args.begin();
                user_assert(current_index > last_index)
                    << "AutoSchedule: cannot auto-schedule function \"" << f.name()
                    << "\" since dim \"" << i << "\" at stage " << stage
                    << " has been reordered\n";
                last_index = current_index;
            }
        }
    }
}
/*
// If the cost of computing a Func is about the same as calling the Func,
// inline the Func. Return true of any of the Funcs is inlined.
bool inline_all_trivial_functions(const vector<Function> &outputs,
                                  const vector<string> &order,
                                  const map<string, Function> &env) {
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
            inlined = true;
            debug(4) << "Function \"" << order[i] << "\" is trivial to inline\n";
            for (int j = i + 1; j < (int)order.size() - (int)outputs.size(); ++j) {
                internal_assert(order[i] != order[j]);
                Function f2 = env.at(order[j]);

                if (f2.has_extern_definition() && !f1.is_wrapper()) {
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
string is_func_called_element_wise(const vector<string> &order, size_t index,
                                   const map<string, Function> &env) {
    Function f1 = env.at(order[index]);
    if (f1.has_extern_definition() || !f1.can_be_inlined()) {
        return "";
    }
    internal_assert(index < order.size());

    string caller = "";
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
bool inline_all_element_wise_functions(const vector<Function> &outputs,
                                       const vector<string> &order,
                                       const map<string, Function> &env) {
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
        string caller = is_func_called_element_wise(order, i, env);
        if (!caller.empty()) {
            inlined = true;
            debug(4) << "Inline function \"" << order[i]
                     << "\" since it is called only by " << caller
                     << " in element-wise manner\n";
            internal_assert(order[i] != caller);
            inline_function(env.at(caller), get_element(env, order[i]));
        }
    }
    return inlined;
}
*/

// Return true if 'f' is used by some extern Func.
bool used_by_extern_func(const map<string, Function> &env, const Function &f) {
    for (const auto &iter : env) {
        for (const ExternFuncArgument &arg : iter.second.extern_arguments()) {
            if (arg.is_func()) {
                if (Function(arg.func).name() == f.name()) {
                    return true;
                }
            }
        }
    }
    return false;
}

// If the bounds of a Func are undefined, then we should just inline the Func
// as long as it is legal to inline or used by some extern Func.
set<string> get_unbounded_functions(const map<string, Box> &pipeline_bounds,
                                    const map<string, Function> &env) {
    set<string> unbounded;
    for (const auto &iter : env) {
        if (!pipeline_bounds.count(iter.first)) {
            debug(5) << "...Skip checking function \"" << iter.first
                     << "\" since it does not have pipeline bounds\n";
            continue;
        }
        const Function &f = iter.second;
        if (!f.can_be_inlined() || used_by_extern_func(env, f)) {
            continue;
        }
        const Box &bound = get_element(pipeline_bounds, iter.first);
        if (is_box_unbounded(bound)) {
            unbounded.insert(iter.first);
        }
    }
    return unbounded;
}

bool inline_unbounded(const vector<Function> &outputs,
                      const vector<string> &order,
                      const map<string, Function> &env,
                      const set<string> &unbounded) {
    bool inlined = false;
    // The very last few functions in 'order' are the last to be realized in the
    // pipeline (the final producers) so there is no point in checking it.
    for (int i = 0; i < (int)order.size() - (int)outputs.size(); ++i) {
        Function f1 = env.at(order[i]);
        if (!unbounded.count(f1.name())) {
            continue;
        }
        inlined = true;
        debug(4) << "Function \"" << order[i] << "\" is unbounded\n";
        for (int j = i + 1; j < (int)order.size(); ++j) {
            internal_assert(order[i] != order[j]);
            Function f2 = env.at(order[j]);
            debug(5) << "Inline unbounded function \"" << f1.name() << "\" inside \""
                     << f2.name() << "\"\n";
            inline_function(f2, f1);
        }
    }
    return inlined;
}

}  // anonymous namespace

// Generate schedules for all functions in the pipeline required to compute the
// outputs. This applies the schedules and returns a string representation of
// the schedules. The target architecture is specified by 'target'.
string my_generate_schedules(const vector<Function> &outputs, const Target &target,
                             const MachineParams &arch_params) {
    // Make an environment map which is used throughout the auto scheduling
    // process.
    map<string, Function> env;
    for (Function f : outputs) {
        map<string, Function> more_funcs = find_transitive_calls(f);
        env.insert(more_funcs.begin(), more_funcs.end());
    }

    // Finalize all the LoopLevels
    for (auto &iter : env) {
        iter.second.lock_loop_levels();
    }

    // Compute the topological order, before any trivial inlining (i.e. before
    // we remove any functions from 'env'). We need the full topological
    // order to pass to get_func() when generating the string representation
    // of the schedule.
    debug(2) << "Computing topological order...\n";
    vector<string> top_order = topological_order(outputs, env);

    // Validate that none of the functions in the pipeline have partial schedules.
    debug(2) << "Validating no partial schedules...\n";
    for (const auto &iter : env) {
        validate_no_partial_schedules(iter.second);
    }

    // The auto scheduling algorithm requires estimates on the outputs of the
    // pipeline to get quantitative estimates of costs for computing functions
    // in the pipeline.
    debug(2) << "Checking estimates on outputs...\n";
    check_estimates_on_outputs(outputs);

    // Run a pre-pass that inline all trivial Funcs (i.e. if the cost of
    // computing a Func is about the same as calling that Func, we should
    // just inline it).
    debug(2) << "Inlining all trivial functions...\n";
    if (inline_all_trivial_functions(outputs, top_order, env)) {
        // If any of the Funcs is inlined, we need to recompute 'env', since some
        // of the Funcs are no longer used and need to be removed from 'env'.
        //
        // Instead of recomputing 'env', we could also remove the inlined Func
        // within inline_all_trivial_functions(); however, it is a bit tricky
        // to do when dealing with inlined tuple. Consider the following case:
        //   f(x, y) = x + y;
        //   g(x, y) = {x, f(x, y)};
        //   h(x, y) = g(x, y)[0];
        // When 'g' is inlined in 'h', no one uses 'f' anymore and it can
        // be removed from 'env'. However, to know this, we need to trace
        // all the function calls within the pipeline. Thus, we might as well
        // recompute the 'env' from scratch.
        env.clear();
        for (Function f : outputs) {
            map<string, Function> more_funcs = find_transitive_calls(f);
            env.insert(more_funcs.begin(), more_funcs.end());
        }
    }

    // Compute the realization order of the functions within the pipeline.
    vector<string> order = realization_order(outputs, env).first;

    // Run a pre-pass that inline all Funcs which values are accessed by
    // another single Func in element-wise manner. We need to do this
    // repeatedly since some inlining decisions may enable further inlining
    // that previously not possible. Consider the following case:
    //   f1(x) = x;
    //   f2(x) = f1(x) + 2;
    //   f3(x) = f1(x) * 2;
    //   f4(x) = f2(x) + f3(x);
    //   f5(x) = f4(x) + 3;
    // In the first iteration, we cannot inline 'f1' since it is used by two
    // functions: 'f2' and 'f3'. If 'f2' and 'f4' get inlined and 'f3' is only
    // used by 'f4', then 'f1' can now also be inlined.
    debug(2) << "Inlining all element-wise functions...\n";
    while (inline_all_element_wise_functions(outputs, order, env)) {
        // We need to recompute 'env' for the same reason as with
        // inline_all_trivial_functions
        env.clear();
        for (Function f : outputs) {
            map<string, Function> more_funcs = find_transitive_calls(f);
            env.insert(more_funcs.begin(), more_funcs.end());
        }
        order = realization_order(outputs, env).first;
    }

    // Compute the bounds of function values which are used for dependence
    // analysis.
    debug(2) << "Computing function value bounds...\n";
    FuncValueBounds func_val_bounds = compute_function_value_bounds(order, env);

    // Initialize the cost model.
    // Compute the expression costs for each function in the pipeline.
    debug(2) << "Initializing region costs...\n";
    RegionCosts costs(env, order);
    if (debug::debug_level() >= 3) {
        costs.disp_func_costs();
    }

    debug(2) << "Initializing dependence analysis...\n";
    DependenceAnalysis dep_analysis(env, order, func_val_bounds);

    // Compute bounds of all functions in the pipeline given estimates on
    // outputs. Also report functions which bounds could not be inferred.
    debug(2) << "Computing pipeline bounds...\n";
    map<string, Box> pipeline_bounds =
        get_pipeline_bounds(dep_analysis, outputs, &costs.input_estimates);

    // Determine all unbounded functions that are not extern Func or
    // used by some extern Funcs.
    debug(2) << "Determining all unbounded functions...\n";
    set<string> unbounded = get_unbounded_functions(pipeline_bounds, env);
    if (!unbounded.empty()) {
        // If some functions are unbounded, we should inline those directly.
        // Also, we need to recompute 'env' and re-initialize 'costs' and
        // 'dep_analysis'
        debug(2) << "Inlining all unbounded functions...\n";
        internal_assert(inline_unbounded(outputs, order, env, unbounded));

        env.clear();
        for (Function f : outputs) {
            map<string, Function> more_funcs = find_transitive_calls(f);
            env.insert(more_funcs.begin(), more_funcs.end());
        }
        order = realization_order(outputs, env).first;

        debug(2) << "Re-computing function value bounds...\n";
        func_val_bounds = compute_function_value_bounds(order, env);
        debug(2) << "Re-initializing region costs...\n";
        RegionCosts costs(env, order);
        debug(2) << "Re-initializing dependence analysis...\n";
        dep_analysis = DependenceAnalysis(env, order, func_val_bounds);
        debug(2) << "Re-computing pipeline bounds...\n";
        pipeline_bounds =
            get_pipeline_bounds(dep_analysis, outputs, &costs.input_estimates);
    }

    debug(2) << "Initializing partitioner...\n";
    map<FStage, map<string, map<string, Expr>>> reuse_map;
    Partitioner part(pipeline_bounds, arch_params, outputs, dep_analysis, costs);
    part.global_children = part.children;
    for (const auto &f : env) {
        FindAllCalls find;
        f.second.accept(&find);
        int num_stages = f.second.updates().size() + 1;
        for (int s = 0; s < num_stages; s++) {
            FStage curr_s(f.second, s);
            map<string, map<string, Expr>> reuse =
                part.evaluate_reuse(curr_s, find.funcs_called);
            string func_name = curr_s.func.name();
            curr_s.re = reuse;
            reuse_map.emplace(curr_s, reuse);
        }
    }
    part.reuse_per_stage = reuse_map;
    part.get_gpu_params(target);
    // Compute and display reuse
    /* TODO: Use the reuse estimates to reorder loops
  for (const auto &f : env) {
      FindAllCalls find;
      f.second.accept(&find);
      int num_stages = f.second.updates().size() + 1;
      for (int s = 0; s < num_stages; s++) {
          FStage curr_s(f.second, s);
          map<string, Expr> reuse = part.evaluate_reuse(curr_s,
  find.funcs_called); debug(0) << curr_s << '\n'; for (const auto &dir : reuse)
  { debug(0) << dir.first << " " << dir.second << ',';
          }
          debug(0) << '\n';
      }
  }*/

    // Display the current pipeline graph.
    // TODO: Output the graph in dot format.
    //   if (debug::debug_level() >= 3) {
    part.disp_pipeline_graph();
    part.disp_pipeline_bounds();
    // }

    debug(2) << "Partitioner initializing groups...\n";
    part.total_inlines = 0;
    part.initialize_groups();
    if (debug::debug_level() >= 3) {
        part.disp_pipeline_costs();
    }

    debug(2) << "Partitioner computing inline group...\n";
    part.group(Partitioner::Level::Inline);
    if (debug::debug_level() >= 3) {
        part.disp_grouping();
    }
    std::string disable_fusion = Internal::get_env_variable("HL_GPU_NO_FUS");
    bool no_fus = std::atoi(disable_fusion.c_str());

    if (!no_fus) {
        part.evaluate_new_tiles();
        debug(2) << "Partitioner computing fast-mem group...\n";
        part.grouping_cache.clear();
        part.group(Partitioner::Level::FastMem);
        if (debug::debug_level() >= 3) {
            part.disp_pipeline_costs();
            part.disp_grouping();
            part.disp_pipeline_graph();
        }
    }
    part.evaluate_final_tiles();
    debug(2) << "Initializing AutoSchedule...\n";
    AutoSchedule sched(env, top_order);
    debug(2) << "Generating CPU schedule...\n";
    part.generate_cpu_schedule(target, sched);

    std::ostringstream oss;
    oss << "// Target: " << target.to_string() << "\n";
    oss << "// MachineParams: " << arch_params.to_string() << "\n";
    oss << "\n";
    oss << sched;
    string sched_string = oss.str();
    cout << sched_string << endl;
    cout << "TOTAL INLINES " << part.total_inlines << endl;
    debug(3) << "\n\n*******************************\nSchedule:\n"
             << "*******************************\n"
             << sched_string << "\n\n";

    // TODO: Unify both inlining and grouping for fast mem
    // TODO: GPU scheduling
    // TODO: Hierarchical tiling

    return sched_string;
}

MachineParams MachineParams::generic() {
    std::string params = Internal::get_env_variable("HL_MACHINE_PARAMS");
    if (params.empty()) {
        return MachineParams(32, 16 * 1024 * 1024, 4);
    } else {
        return MachineParams(params);
    }
}

std::string MachineParams::to_string() const {
    std::ostringstream o;
    o << parallelism << "," << last_level_cache_size << "," << balance;
    return o.str();
}

MachineParams::MachineParams(const std::string &s) {
    std::vector<std::string> v = Internal::split_string(s, ",");
    user_assert(v.size() == 3) << "Unable to parse MachineParams: " << s;
    parallelism = std::atoi(v[0].c_str());
    last_level_cache_size = std::atoll(v[1].c_str());
    balance = std::atof(v[2].c_str());
}

// Halide uses a plugin architecture for registering custom
// autoschedulers. We register our autoscheduler using a static
// constructor.
struct RegisterAutoscheduler {
    RegisterAutoscheduler() {
        std::cout << "Registering autoscheduler 'Sioutas2020'...\n";
        Pipeline::add_autoscheduler("Sioutas2020", *this);
    }

    void operator()(const Pipeline &p, const Target &target, const MachineParams &params, AutoSchedulerResults *results) {
        std::vector<Function> outputs;
        for (Func f : p.outputs()) {
            outputs.push_back(f.function());
        }
        results->schedule_source = my_generate_schedules(outputs, target, params);
    }
} register_auto_scheduler;
