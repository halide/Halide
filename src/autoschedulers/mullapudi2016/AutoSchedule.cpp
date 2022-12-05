#include "HalidePlugin.h"

#include <algorithm>
#include <map>
#include <regex>
#include <set>
#include <utility>

#include "Halide.h"
#include "ParamParser.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace {

struct ArchParams {
    /** Maximum level of parallelism avalaible. */
    int parallelism = 16;

    /** Size of the last-level cache (in bytes). */
    uint64_t last_level_cache_size = 16 * 1024 * 1024;

    /** Indicates how much more expensive is the cost of a load compared to
     * the cost of an arithmetic operation at last level cache. */
    float balance = 40;
};

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

// Return true if any of the box dimension is unbounded.
bool is_box_unbounded(const Box &b) {
    for (size_t i = 0; i < b.size(); i++) {
        if (!b[i].is_bounded()) {
            return true;
        }
    }
    return false;
}

// Helper function to simplify the upper and lower bounds of each dimension of a box.
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
    for (char &c : name) {
        if (!isalnum(c)) {
            c = '_';
        }
    }
    return name;
}

// Representation of a function stage in the pipeline.
struct FStage {
    Function func;
    uint32_t stage_num;
    FStage(Function func, uint32_t stage_num)
        : func(std::move(func)), stage_num(stage_num) {
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
            user_assert(found && est.min.type().is_int() && est.extent.type().is_int())
                << "Please provide a valid estimate for dimension "
                << arg << " of output \"" << out.name() << "\"\n";
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
            return (f == other.f) && (stage == other.stage) && (prods == other.prods) &&
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

    DependenceAnalysis(const map<string, Function> &env, const vector<string> &order,
                       const FuncValueBounds &func_val_bounds)
        : env(env), order(order), func_val_bounds(func_val_bounds) {
    }

    // Return the regions of the producers ('prods') required to compute the region
    // of the function stage ('f', 'stage_num') specified by 'bounds'. When
    // 'only_regions_computed' is set to true, this only returns the computed
    // regions and not the total allocated regions.
    map<string, Box> regions_required(const Function &f, int stage_num,
                                      const DimBounds &bounds,
                                      const set<string> &prods,
                                      bool only_regions_computed,
                                      const Scope<Interval> *input_estimates);

    // Return the regions of the producers ('prods') required to compute the region
    // of the function specified by 'pure_bounds'. When 'only_regions_computed'
    // is set to true, this only returns the computed regions and not the total
    // allocated regions.
    map<string, Box> regions_required(const Function &f,
                                      const DimBounds &pure_bounds,
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
    map<string, Box> redundant_regions(const Function &f, int stage_num, const string &var,
                                       const DimBounds &bounds,
                                       const set<string> &prods,
                                       bool only_regions_computed,
                                       const Scope<Interval> *input_estimates);

    // Return overlapping regions of producers ('prods') while computing a function
    // stage along each of the dimensions.
    vector<map<string, Box>>
    overlap_regions(const Function &f, int stage_num, const DimBounds &bounds,
                    const set<string> &prods, bool only_regions_computed,
                    const Scope<Interval> *input_estimates);
};

// Return the regions of the producers ('prods') required to compute the region
// of the function specified by 'pure_bounds'.
map<string, Box>
DependenceAnalysis::regions_required(const Function &f, const DimBounds &pure_bounds,
                                     const set<string> &prods,
                                     bool only_regions_computed,
                                     const Scope<Interval> *input_estimates) {
    // Find the regions required for each stage and merge them.
    map<string, Box> regions;
    int num_stages = f.updates().size() + 1;
    for (int s = 0; s < num_stages; s++) {
        DimBounds bounds = get_stage_bounds(f, s, pure_bounds);
        map<string, Box> stage_regions =
            regions_required(f, s, bounds, prods, only_regions_computed, input_estimates);

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
        : f_stage(FStage(std::move(func), stage_num)), bounds(b) {
    }

    bool operator==(const StageBounds &other) const {
        return (f_stage == other.f_stage) && (bounds == other.bounds);
    }
    bool operator<(const StageBounds &other) const {
        return (f_stage < other.f_stage) ||
               ((f_stage == other.f_stage) && (bounds.size() < other.bounds.size()));
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
                        if (b_iter->second.has_lower_bound() && b.second.has_lower_bound()) {
                            b_iter->second.min = simplify(Interval::make_min(b_iter->second.min, b.second.min));
                        } else {
                            b_iter->second.min = Interval::neg_inf();
                        }

                        if (b_iter->second.has_upper_bound() && b.second.has_upper_bound()) {
                            b_iter->second.max = simplify(Interval::make_max(b_iter->second.max, b.second.max));
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
                             bool only_regions_computed,
                             const string &curr_func_name,
                             const set<StageBounds> &visited) {
    for (const auto &reg : curr_regions) {
        // Merge region with an existing region of a function in the
        // global map. Do not merge the parent function itself to the region
        // when querying only for the values computed.
        if (!only_regions_computed || (only_regions_computed && (reg.first != curr_func_name))) {
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
map<string, Box>
DependenceAnalysis::regions_required(const Function &f, int stage_num,
                                     const DimBounds &bounds,
                                     const set<string> &prods,
                                     bool only_regions_computed,
                                     const Scope<Interval> *input_estimates) {
    // Iteratively compute the required regions by traversing the chain
    // of dependencies.

    // Check the cache if we've already computed this previously.
    RegionsRequiredQuery query(f.name(), stage_num, prods, only_regions_computed);
    const auto &iter = regions_required_cache.find(query);
    if (iter != regions_required_cache.end()) {
        const auto &it = std::find_if(iter->second.begin(), iter->second.end(),
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
                                                    only_regions_computed, s.func.name(), visited);
                        } else if (arg.is_expr()) {
                            // Find the boxes required for the expression and add the regions
                            // to the queue.
                            Expr subs_arg = substitute_var_estimates(arg.expr);
                            map<string, Box> arg_regions = boxes_required(subs_arg, curr_scope, func_val_bounds);
                            substitute_estimates_region(arg_regions);
                            merge_and_queue_regions(fs_bounds, regions, arg_regions, prods, env,
                                                    only_regions_computed, s.func.name(), visited);
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

                    // Find the regions required for each value of the current function stage,
                    // update the region map, and add them to the queue.
                    for (const auto &val : def.values()) {
                        // Substitute the parameter estimates into the expression and get
                        // the regions required for the expression.
                        Expr subs_val = substitute_var_estimates(val);
                        map<string, Box> curr_regions = boxes_required(subs_val, curr_scope, func_val_bounds);
                        substitute_estimates_region(curr_regions);

                        // Arguments to the definition may require regions of functions.
                        // For example, update definitions in histograms where the bin is
                        // based on the value of a function.
                        Box left_reg;
                        for (const Expr &arg : def.args()) {
                            Expr subs_arg = substitute_var_estimates(arg);
                            map<string, Box> arg_regions = boxes_required(subs_arg, curr_scope, func_val_bounds);
                            substitute_estimates_region(arg_regions);

                            // Merge the regions with the regions found while looking at
                            // the values.
                            merge_regions(curr_regions, arg_regions);

                            Interval arg_bounds = bounds_of_expr_in_scope(arg, curr_scope, func_val_bounds);
                            left_reg.push_back(arg_bounds);
                        }

                        auto iter_curr = curr_regions.find(s.func.name());
                        if (iter_curr == curr_regions.end()) {
                            curr_regions.emplace(s.func.name(), left_reg);
                        } else {
                            merge_boxes(iter_curr->second, left_reg);
                        }

                        // Update the region map, and add 'curr_regions' to the queue.
                        merge_and_queue_regions(fs_bounds, regions, curr_regions, prods, env,
                                                only_regions_computed, s.func.name(), visited);
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

    regions_required_cache[query].push_back(RegionsRequired(bounds, concrete_regions));
    return concrete_regions;
}

// Return redundantly computed regions of producers ('prods') while computing a
// region of the function stage ('f', 'stage_num') specified by 'bounds'. 'var'
// is the dimension along which redundant computation is accounted for.
map<string, Box>
DependenceAnalysis::redundant_regions(const Function &f, int stage_num, const string &var,
                                      const DimBounds &bounds,
                                      const set<string> &prods,
                                      bool only_regions_computed,
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
    map<string, Box> regions_shifted = regions_required(
        f, stage_num, shifted_bounds, prods, only_regions_computed, input_estimates);

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
vector<map<string, Box>>
DependenceAnalysis::overlap_regions(const Function &f, int stage_num,
                                    const DimBounds &bounds,
                                    const set<string> &prods,
                                    bool only_regions_computed,
                                    const Scope<Interval> *input_estimates) {
    vector<map<string, Box>> conc_overlaps;

    const vector<Dim> &dims = get_stage_dims(f, stage_num);

    // Get the redundant regions along each dimension of f.
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        map<string, Box> conc_reg = redundant_regions(f, stage_num, dims[d].var, bounds,
                                                      prods, only_regions_computed, input_estimates);
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
        for (const pair<const string, Function> &fpair : analysis.env) {
            prods.insert(fpair.first);
        }

        map<string, Box> regions = analysis.regions_required(out, pure_bounds, prods,
                                                             false, input_estimates);

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
            return (function < other.function) || ((function == other.function) && (stage < other.stage));
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

    // Given a function name, return a string representation of getting the
    // function handle
    string get_func_handle(const string &name) const {
        size_t index = get_element(topological_order, name);
        return "pipeline.get_func(" + std::to_string(index) + ")";
    }

    friend std::ostream &operator<<(std::ostream &stream, const AutoSchedule &sched) {
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
            func_ss << "Func " << fname << " = " << sched.get_func_handle(f.first) << ";\n";

            schedule_ss << "{\n";

            // Declare all the Vars and RVars that are actually used in the schedule
            const Function &func = get_element(sched.env, f.first);
            for (size_t i = 0; i < func.args().size(); ++i) {
                if (sched.used_vars.at(func.name()).at(0).find(func.args()[i]) != sched.used_vars.at(func.name()).at(0).end()) {
                    schedule_ss << "    Var " << func.args()[i] << " = "
                                << fname << ".args()[" << i << "];\n";
                }
            }
            set<string> declared_rvars;
            for (size_t i = 0; i < func.updates().size(); ++i) {
                const vector<ReductionVariable> &rvars = func.updates()[i].schedule().rvars();
                const set<string> &var_list = sched.used_vars.at(func.name()).at(i + 1);
                for (size_t j = 0; j < rvars.size(); ++j) {
                    if ((var_list.find(rvars[j].var) == var_list.end()) ||
                        (declared_rvars.find(rvars[j].var) != declared_rvars.end())) {
                        continue;
                    }
                    declared_rvars.insert(rvars[j].var);
                    schedule_ss << "    RVar " << rvars[j].var << "("
                                << fname << ".update(" << i << ").get_schedule().rvars()[" << j << "].var);\n";
                }
            }

            for (const auto &m : f.second) {
                const int stage = m.first;
                const vector<string> &schedules = m.second;
                internal_assert(!schedules.empty());
                schedule_ss << "    " << fname;
                if (stage > 0) {
                    schedule_ss << ".update(" << std::to_string(stage - 1) << ")";
                }
                for (const std::string &s : schedules) {
                    schedule_ss << "\n        ." << s;
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
    // GroupingChoice encodes the grouping of the 'prod' function into the 'cons' stage.
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
            return (prod < other.prod) || ((prod == other.prod) && (cons < other.cons));
        }

        friend std::ostream &operator<<(std::ostream &stream, const GroupingChoice &choice) {
            stream << "Choice: " << choice.prod << " -> " << choice.cons << "\n";
            return stream;
        }
    };

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
            stream << "Output FStage: " << g.output << "\n"
                   << "Members: {";
            for (size_t i = 0; i < g.members.size(); ++i) {
                if (i > 0) {
                    stream << ", ";
                }
                stream << g.members[i];
            }
            stream << "}\n";

            stream << "Inlined: {";
            for (auto iter = g.inlined.begin(); iter != g.inlined.end(); ++iter) {
                if (std::distance(g.inlined.begin(), iter) > 0) {
                    stream << ", ";
                }
                stream << *iter;
            }
            stream << "}\n";

            stream << "Tile sizes: {";
            for (auto iter = g.tile_sizes.begin(); iter != g.tile_sizes.end(); ++iter) {
                if (std::distance(g.tile_sizes.begin(), iter) > 0) {
                    stream << ", ";
                }
                stream << "(" << iter->first << ", " << iter->second << ")";
            }
            stream << "}\n";

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

        GroupAnalysis()
            : cost(Cost()), parallelism(Expr()) {
        }
        GroupAnalysis(const Cost &c, Expr p)
            : cost(c), parallelism(std::move(p)) {
        }

        inline bool defined() const {
            return cost.defined() && parallelism.defined();
        }

        void simplify() {
            cost.simplify();
            if (parallelism.defined()) {
                parallelism = Internal::simplify(parallelism);
            }
        }

        friend std::ostream &operator<<(std::ostream &stream, const GroupAnalysis &analysis) {
            stream << "[arith cost:" << analysis.cost.arith << ", "
                   << "memory cost:" << analysis.cost.memory << ", "
                   << "parallelism:" << analysis.parallelism << "]\n";
            return stream;
        }
    };

    // Configuration of a group and the corresponding analysis. A group is the
    // set of functions that are computed together in tiles and the group config
    // specifies at what granularity they are computed together ('tile_sizes').
    struct GroupConfig {
        map<string, Expr> tile_sizes;
        GroupAnalysis analysis;
        GroupConfig(const map<string, Expr> &tile_sizes, const GroupAnalysis &analysis)
            : tile_sizes(tile_sizes), analysis(analysis) {
        }
        GroupConfig()
            : tile_sizes(map<string, Expr>()), analysis(GroupAnalysis()) {
        }
    };

    // Cache for storing the best configuration for the grouping choice. During
    // the grouping process, the impact of grouping two groups together is only
    // limited to the producers and consumers of the groups that are being grouped
    // together. The best grouping choices for the rest of the pipeline need not be
    // re-evaluated and caching them improves performance significantly.
    map<GroupingChoice, GroupConfig> grouping_cache;

    // Each group in the pipeline has a single output stage. A group is comprised
    // of function stages that are computed together in tiles (stages of a function
    // are always grouped together). 'groups' is the mapping from the output stage
    // of the group to the group.
    map<FStage, Group> groups;
    // The child stages of each stage (i.e. stages that depend on or use the values
    // computed by a particular stage) in the pipeline.
    map<FStage, set<FStage>> children;
    // Map from the output stage of the group to the analysis of the group. The mapping
    // needs to be updated whenever the grouping changes.
    map<FStage, GroupAnalysis> group_costs;

    // Levels that are targeted by the grouping algorithm. In the 'Inline' mode, the grouping
    // algorithm groups the functions by inlining the expression for the producer function
    // into the consumer stage. In the 'FastMem' mode, the grouping is done at the level of
    // tiles of the group output stage.
    enum class Level { Inline,
                       FastMem };

    // Bounds of each function stage in the pipeline. These bounds are inferred from the
    // estimates of the outputs and other functions in the pipeline.
    const map<string, Box> &pipeline_bounds;
    // Parameters of the machine model that is used for estimating the cost of each
    // group in the pipeline.
    const ArchParams &arch_params;
    // Dependency analysis of the pipeline. This support queries on regions
    // accessed and computed for producing some regions of some functions.
    DependenceAnalysis &dep_analysis;
    // The arithmetic and memory costs of evaluating the expressions which define
    // each function in the pipeline.
    RegionCosts &costs;
    // Output functions of the pipeline.
    const vector<Function> &outputs;

    Partitioner(const map<string, Box> &_pipeline_bounds,
                const ArchParams &_arch_params,
                const vector<Function> &_outputs,
                DependenceAnalysis &_dep_analysis,
                RegionCosts &_costs);

    void initialize_groups();

    // Merge 'prod_group' into 'cons_group'. The output stage of 'cons_group'
    // will be the output stage of the merged group.
    Group merge_groups(const Group &prod_group, const Group &cons_group);

    // Merge 'prods' in 'choice' into 'cons'. Set the tile size of the new group
    // to the one specified by 'eval'. If 'level' is set to Inline, all members
    // of 'prods' will be inlined in the new group.
    void merge_groups(const GroupingChoice &choice, const GroupConfig &eval,
                      Partitioner::Level level);

    // Given a grouping 'g', compute the estimated cost (arithmetic + memory) and
    // parallelism that can be potentially exploited when computing that group.
    GroupAnalysis analyze_group(const Group &g, bool show_analysis);

    // For each group in the partition, return the regions of the producers
    // need to be allocated to compute a tile of the group's output.
    map<FStage, map<string, Box>> group_storage_bounds();

    // For each group in the partition, return the regions of the producers
    // required to compute a tile of the group's output.
    map<FStage, map<FStage, DimBounds>> group_loop_bounds();

    // Partition the pipeline by iteratively merging groups until a fixpoint is
    // reached.
    void group(Partitioner::Level level);

    // Given a grouping choice, return a configuration for the group that gives
    // the highest estimated benefits.
    GroupConfig evaluate_choice(const GroupingChoice &group, Partitioner::Level level);

    // Pick the best choice among all the grouping options currently available. Uses
    // the cost model to estimate the benefit of each choice. This returns a vector of
    // choice and configuration pairs which describe the best grouping choice.
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
    vector<map<string, Expr>> generate_tile_configs(const FStage &stg);

    // Find the best tiling configuration for a group 'g' among a set of tile
    // configurations. This returns a pair of configuration with the highest
    // estimated benefit and the estimated benefit.
    pair<map<string, Expr>, GroupAnalysis> find_best_tile_config(const Group &g);

    // Estimate the benefit (arithmetic + memory) of 'new_grouping' over 'old_grouping'.
    // Positive values indicates that 'new_grouping' may be preferrable over 'old_grouping'.
    // When 'ensure_parallelism' is set to true, this will return an undefined cost
    // if the estimated parallelism is smaller than the machine parameters.
    // If 'no_redundant_work' is set, we only consider the arithmetic cost, i.e. if
    // the arithmetic benefit is negative, we will treat it as no benefits and we
    // should not perform the new grouping.
    Expr estimate_benefit(const GroupAnalysis &old_grouping, const GroupAnalysis &new_grouping,
                          bool no_redundant_work, bool ensure_parallelism) const;

    // Same as above; however, 'new_grouping' is a vector of function pairs that
    // are to be grouped together.
    Expr estimate_benefit(const vector<pair<GroupingChoice, GroupConfig>> &new_grouping,
                          bool no_redundant_work, bool ensure_parallelism) const;

    // Return the total estimate on arithmetic and memory costs of computing all
    // groups within the pipeline.
    Cost get_pipeline_cost();

    // Return the maximum access stride to allocation of 'func_acc' along any
    // loop variable specified in 'vars'. Access expressions along each dimension
    // of the allocation are specified by 'acc_exprs'. The dimension bounds of the
    // allocation are specified by 'buffer_bounds'.
    Expr find_max_access_stride(const Scope<> &vars, const string &func_acc,
                                const vector<Expr> &acc_exprs, const Box &buffer_bounds);

    // Return the sum of access strides along each of the loop variables in
    // a function stage. The bounds of all the allocations accessed are specified
    // in 'allocation_bounds'. Return an empty map if it can't figure out any of
    // the stride dimension.
    map<string, Expr> analyze_spatial_locality(
        const FStage &stg, const map<string, Box> &parent_bounds,
        const set<string> &inlines = set<string>());

    map<string, Expr> evaluate_reuse(const FStage &stg, const set<string> &prods);

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
    // user visible. More thought needs to go into interaction between the user and
    // auto scheduling.
    void generate_cpu_schedule(const Target &t, AutoSchedule &sched);

    // Same as \ref Partitioner::generate_cpu_schedule, but this generates and
    // applies schedules for a group of function stages.

    void generate_group_cpu_schedule(const Group &g, const Target &t,
                                     const map<FStage, DimBounds> &group_loop_bounds,
                                     const map<string, Box> &group_storage_bounds,
                                     const set<string> &inlines,
                                     AutoSchedule &sched);

    // Split the dimension of stage 'f_handle' along 'v' into inner and outer
    // dimensions. Modify 'estimates' according to the split and append the split
    // schedule to 'sched'.
    pair<VarOrRVar, VarOrRVar> split_dim(
        const Group &g, Stage f_handle, int stage_num, const Definition &def,
        bool is_group_output, const VarOrRVar &v, const Expr &factor, const string &in_suffix,
        const string &out_suffix, map<string, Expr> &estimates, AutoSchedule &sched);

    // Loop over the dimensions of function stage 'f_handle' starting from innermost
    // and vectorize the first pure dimension encountered.
    void vectorize_stage(
        const Group &g, Stage f_handle, int stage_num, Definition def,
        const Function &func, bool is_group_output, const Target &t, set<string> &rvars,
        map<string, Expr> &estimates, AutoSchedule &sched);

    // Reorder the dimensions to preserve spatial locality. This function
    // checks the stride of each access. The dimensions of the loop are reordered
    // such that the dimension with the smallest access stride is innermost.
    // This takes the strides along each dimension as input.
    void reorder_dims(Stage f_handle, int stage_num, Definition def,
                      map<string, Expr> strides, AutoSchedule &sched);

    // Helper functions to display partition information of the pipeline.
    void disp_pipeline_costs();
    void disp_pipeline_bounds();
    void disp_pipeline_graph();
    void disp_grouping();
};

void Partitioner::disp_grouping() {
    debug(0) << "\n=========\n"
             << "Grouping:\n"
             << "=========\n";
    for (const auto &g : groups) {
        debug(0) << g.second << "\n";
    }
    debug(0) << "=========\n";
}

void Partitioner::disp_pipeline_graph() {
    debug(0) << "\n================\n"
             << "Pipeline graph:\n"
             << "================\n";
    for (const auto &f : children) {
        debug(0) << f.first << ": {";
        for (auto iter = f.second.begin(); iter != f.second.end(); ++iter) {
            if (std::distance(f.second.begin(), iter) > 0) {
                debug(0) << ", ";
            }
            debug(0) << *iter;
        }
        debug(0) << "}\n";
    }
    debug(0) << "================\n";
}

void Partitioner::disp_pipeline_bounds() {
    debug(0) << "\n================\n"
             << "Pipeline bounds:\n"
             << "================\n";
    disp_regions(pipeline_bounds);
    debug(0) << "===============\n";
}

Cost Partitioner::get_pipeline_cost() {
    internal_assert(!group_costs.empty());

    Cost total_cost(0, 0);
    for (const pair<const FStage, Group> &g : groups) {
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
    debug(0) << "\n===============\n"
             << "Pipeline costs:\n"
             << "===============\n"
             << "Group: (name) [arith cost, mem cost, parallelism]\n";
    for (const pair<const FStage, Group> &g : groups) {
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
        debug(0) << analysis.cost.arith << ", " << analysis.cost.memory
                 << ", " << analysis.parallelism << "]\n";
    }
    total_cost.simplify();
    debug(0) << "Total arithmetic cost: " << total_cost.arith << "\n"
             << "Total memory cost: " << total_cost.memory << "\n"
             << "===============\n";
}

// Construct a partitioner and build the pipeline graph on which the grouping
// algorithm operates.
Partitioner::Partitioner(const map<string, Box> &_pipeline_bounds,
                         const ArchParams &_arch_params,
                         const vector<Function> &_outputs,
                         DependenceAnalysis &_dep_analysis,
                         RegionCosts &_costs)
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

    // Find the consumers of each function and use it to populate the children map.
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

void Partitioner::initialize_groups() {
    for (pair<const FStage, Group> &g : groups) {
        pair<map<string, Expr>, GroupAnalysis> best = find_best_tile_config(g.second);
        g.second.tile_sizes = best.first;
        group_costs.emplace(g.second.output, best.second);
    }
    grouping_cache.clear();
}

map<string, Expr> Partitioner::evaluate_reuse(const FStage &stg,
                                              const set<string> &prods) {
    map<string, Expr> reuse;
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
        tile_sizes[dims[d].var] = 1;
    }

    DimBounds bounds = get_bounds_from_tile_sizes(stg, tile_sizes);

    vector<map<string, Box>> reuse_regions =
        dep_analysis.overlap_regions(stg.func, stg.stage_num, bounds, prods,
                                     false, &costs.input_estimates);

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        Expr total_reuse = make_zero(Int(64));
        if (debug::debug_level() >= 3) {
            disp_regions(reuse_regions[d]);
        }
        for (const auto &reg : reuse_regions[d]) {
            Expr size = box_size(reg.second);
            if (!size.defined()) {
                total_reuse = Expr();
                break;
            } else {
                total_reuse += size;
            }
        }
        reuse.emplace(dims[d].var, simplify(total_reuse));
    }

    return reuse;
}

vector<pair<Partitioner::GroupingChoice, Partitioner::GroupConfig>>
Partitioner::choose_candidate_grouping(const vector<pair<string, string>> &cands,
                                       Partitioner::Level level) {
    vector<pair<GroupingChoice, GroupConfig>> best_grouping;
    Expr best_benefit = make_zero(Int(64));
    for (const auto &p : cands) {
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
                best_config = evaluate_choice(cand_choice, level);
                // Cache the result of the evaluation for the pair
                grouping_cache.emplace(cand_choice, best_config);
            }

            grouping.emplace_back(cand_choice, best_config);
        }

        bool no_redundant_work = false;
        Expr overall_benefit = estimate_benefit(grouping, no_redundant_work, true);

        debug(3) << "Candidate grouping:\n";
        for (const auto &g : grouping) {
            debug(3) << "  " << g.first;
        }
        debug(3) << "Candidate benefit: " << overall_benefit << "\n";
        // TODO: The grouping process can be non-deterministic when the costs
        // of two choices are equal
        if (overall_benefit.defined() && can_prove(best_benefit < overall_benefit)) {
            best_grouping = grouping;
            best_benefit = overall_benefit;
        }
    }

    debug(3) << "\nBest grouping:\n";
    for (const auto &g : best_grouping) {
        debug(3) << "  " << g.first;
    }
    if (!best_grouping.empty()) {
        debug(3) << "Best benefit: " << best_benefit << "\n";
    }

    return best_grouping;
}

inline bool operator==(const map<string, Expr> &m1, const map<string, Expr> &m2) {
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

vector<map<string, Expr>> Partitioner::generate_tile_configs(const FStage &stg) {
    // TODO: This is a wart due to the cost model not taking vectorization
    // and pre-fetching into account. Ensuring the innermost dimension has
    // at least size of 64 gives enough values for vectorization and can help
    // with prefetching. This also interacts with the number of parallel tasks
    // that are generated.
    int min_inner_dim_size = 64;

    const vector<Dim> &dims = get_stage_dims(stg.func, stg.stage_num);

    // Get the dimensions that are going to be tiled in this stage.
    // Skipping rvars for now.
    vector<string> tile_vars;
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        if (!dims[d].is_rvar()) {
            tile_vars.push_back(dims[d].var);
        }
    }

    vector<int> size_variants = {1, 4, 8, 16, 32, 64, 128, 256};
    vector<map<string, Expr>> tile_configs;

    // For all the tile configurations generated, we force the innermost dimension
    // to be at least of size 64 to ensure enough values for vectorization.

    // Skewed tile configurations
    for (size_t i = 0; i < tile_vars.size(); i++) {
        for (const auto &dim_size : size_variants) {
            map<string, Expr> tiling;
            tiling.emplace(tile_vars[i],
                           (i == 0) ? std::max(dim_size, min_inner_dim_size) : dim_size);
            for (size_t j = 0; j < tile_vars.size(); j++) {
                if (j < i) {
                    tiling.emplace(tile_vars[j], size_variants[size_variants.size() - 1]);
                } else if (j > i) {
                    tiling.emplace(tile_vars[j], size_variants[0]);
                }
            }
            if (!tiling.empty()) {
                bool is_duplicate =
                    std::find_if(tile_configs.begin(), tile_configs.end(),
                                 [&tiling](const map<string, Expr> &m) { return (tiling == m); }) != tile_configs.end();
                if (!is_duplicate) {
                    tile_configs.push_back(tiling);
                }
            }
        }
    }

    // Almost square tile configurations
    for (const auto &dim_size : size_variants) {
        map<string, Expr> tiling;
        for (size_t j = 0; j < tile_vars.size(); j++) {
            tiling.emplace(tile_vars[j],
                           (j == 0) ? std::max(dim_size, min_inner_dim_size) : dim_size);
        }
        if (!tiling.empty()) {
            bool is_duplicate =
                std::find_if(tile_configs.begin(), tile_configs.end(),
                             [&tiling](const map<string, Expr> &m) { return (tiling == m); }) != tile_configs.end();
            if (!is_duplicate) {
                tile_configs.push_back(tiling);
            }
        }
    }

    // Reorder tile configurations
    for (int i = 0; i < (1 << (tile_vars.size())); i++) {
        map<string, Expr> tiling;
        for (size_t j = 0; j < tile_vars.size(); j++) {
            if (((i >> (j)) & 1) == 1) {
                if (j == 0) {
                    tiling.emplace(tile_vars[j], min_inner_dim_size);
                } else {
                    tiling.emplace(tile_vars[j], 1);
                }
            }
        }
        if (!tiling.empty()) {
            bool is_duplicate =
                std::find_if(tile_configs.begin(), tile_configs.end(),
                             [&tiling](const map<string, Expr> &m) { return (tiling == m); }) != tile_configs.end();
            if (!is_duplicate) {
                tile_configs.push_back(tiling);
            }
        }
    }

    return tile_configs;
}

pair<map<string, Expr>, Partitioner::GroupAnalysis>
Partitioner::find_best_tile_config(const Group &g) {
    // Initialize to no tiling
    map<string, Expr> no_tile_config;
    Group no_tile = g;
    no_tile.tile_sizes = no_tile_config;

    bool show_analysis = false;
    GroupAnalysis no_tile_analysis = analyze_group(no_tile, show_analysis);

    GroupAnalysis best_analysis = no_tile_analysis;
    map<string, Expr> best_config = no_tile_config;
    if (!best_analysis.cost.defined()) {
        return make_pair(best_config, best_analysis);
    }

    // Generate tiling configurations
    vector<map<string, Expr>> configs = generate_tile_configs(g.output);

    Group best_group = g;
    for (const auto &config : configs) {
        Group new_group = g;
        new_group.tile_sizes = config;

        GroupAnalysis new_analysis = analyze_group(new_group, show_analysis);

        bool no_redundant_work = false;
        Expr benefit = estimate_benefit(best_analysis, new_analysis,
                                        no_redundant_work, true);

        if (show_analysis) {
            debug(0) << "Benefit relative to not tiling:" << benefit << "\n";
            debug(0) << "Best analysis:" << new_analysis;
            debug(0) << "No tile analysis:" << no_tile_analysis;
            debug(0)
                << "arith cost:" << cast<float>(new_analysis.cost.arith / no_tile_analysis.cost.arith)
                << ", mem cost:" << cast<float>(new_analysis.cost.memory / no_tile_analysis.cost.memory) << "\n";
        }

        if (benefit.defined() && can_prove(benefit > 0)) {
            best_config = config;
            best_analysis = new_analysis;
            best_group = new_group;
        }
    }

    return make_pair(best_config, best_analysis);
}

void Partitioner::group(Partitioner::Level level) {
    bool fixpoint = false;
    while (!fixpoint) {
        Cost pre_merge = get_pipeline_cost();

        fixpoint = true;
        vector<pair<string, string>> candidates;
        for (const pair<const FStage, Group> &g : groups) {
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

            const Function &prod_f = get_element(dep_analysis.env, g.first.func.name());
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
                    candidates.emplace_back(prod_name, cons_name);
                } else if ((level == Partitioner::Level::Inline) && prod_f.is_pure()) {
                    const string &prod_name = prod_f.name();
                    candidates.emplace_back(prod_name, "");
                }
            }
        }

        debug(3) << "\n============================\n"
                 << "Current grouping candidates:\n"
                 << "============================\n";
        for (auto &c : candidates) {
            debug(3) << "{" << c.first << ", " << c.second << "}\n";
        }

        vector<pair<GroupingChoice, GroupConfig>> best = choose_candidate_grouping(candidates, level);
        if (best.empty()) {
            continue;
        } else {
            fixpoint = false;
        }

        // The following code makes the assumption that all the stages of a function
        // will be in the same group. 'choose_candidate_grouping' ensures that the
        // grouping choice being returned adheres to this constraint.
        const string &prod = best[0].first.prod;

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

DimBounds Partitioner::get_bounds_from_tile_sizes(const FStage &s,
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

Partitioner::GroupAnalysis Partitioner::analyze_group(const Group &g, bool show_analysis) {
    set<string> group_inputs;
    set<string> group_members;

    for (const auto &stg : g.members) {
        group_members.insert(stg.func.name());
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

    // Count the number of tiles
    Expr estimate_tiles = make_one(Int(64));
    Expr parallelism = make_one(Int(64));

    if (!g.output.func.has_extern_definition()) {
        // Get the definition corresponding to the group output
        Definition def = get_stage_definition(g.output.func, g.output.stage_num);
        const vector<Dim> &dims = def.schedule().dims();

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

                Expr dim_tiles = simplify((extent + size - 1) / size);
                estimate_tiles *= dim_tiles;
                // Since all Vars are inherently parallelizable by construct, we
                // only need to take RVars into account for the analysis.
                if (can_parallelize_rvar(var, g.output.func.name(), def)) {
                    parallelism *= dim_tiles;
                }
            }
        }
    }

    // Get the regions of the pipeline required to compute a tile of the group
    DimBounds tile_bounds = get_bounds_from_tile_sizes(g.output, g.tile_sizes);

    map<string, Box> alloc_regions = dep_analysis.regions_required(
        g.output.func, g.output.stage_num, tile_bounds, group_members, false, &costs.input_estimates);

    map<string, Box> compute_regions = dep_analysis.regions_required(
        g.output.func, g.output.stage_num, tile_bounds, group_members, true, &costs.input_estimates);

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
    if (!tile_cost.defined()) {
        return GroupAnalysis();
    }

    Cost out_cost = costs.stage_region_cost(g.output.func.name(),
                                            g.output.stage_num,
                                            tile_bounds, g.inlined);
    if (!out_cost.defined()) {
        return GroupAnalysis();
    }

    for (const auto &reg : alloc_regions) {
        if (!box_size(reg.second).defined()) {
            return GroupAnalysis();
        }
    }

    Cost group_cost(simplify(tile_cost.arith + out_cost.arith),
                    simplify(tile_cost.memory + out_cost.memory));

    // Detailed load costs for all the group intermediates
    map<string, Expr> group_load_costs =
        costs.detailed_load_costs(group_reg, g.inlined);

    map<string, Expr> out_load_costs =
        costs.stage_detailed_load_costs(g.output.func.name(),
                                        g.output.stage_num,
                                        tile_bounds, g.inlined);

    combine_load_costs(group_load_costs, out_load_costs);

    Box out_tile_extent;
    if (g.output.stage_num == 0) {
        const vector<string> &args = g.output.func.args();
        for (const auto &arg : args) {
            const auto &iter = tile_bounds.find(arg);
            if (iter != tile_bounds.end()) {
                out_tile_extent.push_back(iter->second);
            } else {
                out_tile_extent.push_back(Interval());
            }
        }
    }

    Cost per_tile_cost(group_cost.arith, make_zero(Int(64)));

    // This is the old cost model; keeping it here for reference, for now.
    /*
    if (tile_inter_size > arch_params.l1_size) {
        // Conservative estimate of accesses to memory
        //per_tile_mem_cost = tile_inter_size;
        // Aggressive estimate of accesses to memory
        per_tile_mem_cost = tile_cost.second;
    } else {
        // The tile_input_size captures the region of the input
        // required to compute the tile. However, all of it many not be
        // accessed during the computation of the tile when the access
        // is sparse. A better estimate is given by the smaller of
        // the number of memory accesses and the region size
        per_tile_mem_cost = std::min(tile_input_size + tile_output_size,
                                     tile_cost.second);
    }*/

    // TODO: Use smooth step curve from Jon to better model cache behavior,
    // where each step corresponds to different cache level.
    //
    // The current cost model drops off linearly. Larger memory footprint is
    // penalized more than smaller memory footprint (since smaller one can fit
    // more in the cache). The cost is clamped at 'balance', which is roughly at
    // memory footprint equal to or larger than the last level cache size.

    // If 'model_reuse' is set, the cost model should take into account memory
    // reuse within the tile, e.g. matrix multiply reuses inputs multiple times.
    // TODO: Implement a better reuse model.
    bool model_reuse = false;

    // Linear dropoff
    float load_slope = arch_params.balance / arch_params.last_level_cache_size;
    for (const auto &f_load : group_load_costs) {
        internal_assert(g.inlined.find(f_load.first) == g.inlined.end())
            << "Intermediates of inlined pure function \"" << f_load.first
            << "\" should not have been in the group_load_costs\n";

        const auto &alloc_reg = get_element(alloc_regions, f_load.first);

        Expr footprint;
        bool is_group_member = (group_members.find(f_load.first) != group_members.end());
        bool is_output = (f_load.first == g.output.func.name());

        // We use allocated region as conservative estimate of the footprint since
        // the loads could be from any random locations of the allocated regions.

        if (!is_output && is_group_member) {
            footprint = costs.region_size(f_load.first, alloc_reg);
        } else {
            Expr initial_footprint;
            const auto &f_load_pipeline_bounds = get_element(pipeline_bounds, f_load.first);

            bool is_function = (dep_analysis.env.find(f_load.first) != dep_analysis.env.end());
            if (!is_function) {  // It is a load to some input buffer
                // Initial loads
                initial_footprint = costs.input_region_size(f_load.first, f_load_pipeline_bounds);
                // Subsequent loads
                footprint = costs.input_region_size(f_load.first, alloc_reg);
            } else if (is_output) {  // Load to the output function of the group
                internal_assert(is_group_member)
                    << "Output " << f_load.first << " should have been a group member\n";
                // Initial loads
                initial_footprint = costs.region_size(f_load.first, f_load_pipeline_bounds);
                // Subsequent loads
                footprint = costs.region_size(f_load.first, out_tile_extent);
            } else {  // Load to some non-member function (i.e. function from other groups)
                // Initial loads
                initial_footprint = costs.region_size(f_load.first, f_load_pipeline_bounds);
                // Subsequent loads
                footprint = costs.region_size(f_load.first, alloc_reg);
            }

            if (model_reuse) {
                Expr initial_factor =
                    cast<int64_t>(min(1 + initial_footprint * load_slope, arch_params.balance));
                per_tile_cost.memory += initial_factor * footprint;
            } else {
                footprint = initial_footprint;
            }

            if (!footprint.defined()) {
                return GroupAnalysis();
            }
        }

        Expr cost_factor = cast<int64_t>(min(1 + footprint * load_slope, arch_params.balance));
        per_tile_cost.memory += cost_factor * f_load.second;
    }

    if (show_analysis) {
        debug(0) << "\nDetailed loads:\n";
        for (const auto &f_load : group_load_costs) {
            debug(0) << "(" << f_load.first << "," << f_load.second << ")";
        }
        debug(0) << "\n";

        debug(0) << "\nPer tile memory cost:" << per_tile_cost.memory << "\n";
        debug(0) << "Per tile arith cost:" << per_tile_cost.arith << "\n";
    }

    GroupAnalysis g_analysis(
        Cost(per_tile_cost.arith * estimate_tiles, per_tile_cost.memory * estimate_tiles),
        parallelism);
    g_analysis.simplify();

    return g_analysis;
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

void Partitioner::merge_groups(const GroupingChoice &choice, const GroupConfig &eval,
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

Partitioner::GroupConfig Partitioner::evaluate_choice(const GroupingChoice &choice,
                                                      Partitioner::Level level) {
    // Create a group that reflects the grouping choice and evaluate the cost
    // of the group.
    const Function &prod_f = get_element(dep_analysis.env, choice.prod);
    int num_prod_stages = prod_f.updates().size() + 1;
    vector<Group> prod_groups;

    for (int s = 0; s < num_prod_stages; s++) {
        FStage prod_s(prod_f, s);
        prod_groups.push_back(get_element(groups, prod_s));
    }

    Group cons = get_element(groups, choice.cons);
    Group group = cons;
    for (const auto &prod_g : prod_groups) {
        group = merge_groups(prod_g, group);
    }

    GroupAnalysis group_analysis;
    map<string, Expr> best_tile_config;

    if (level == Partitioner::Level::Inline) {
        // Set the tile sizes to one along all dimensions of the consumer group
        map<string, Expr> tile_sizes;

        const Function &cons_f = cons.output.func;

        const vector<Dim> &dims = get_stage_dims(cons_f, cons.output.stage_num);
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            tile_sizes[dims[d].var] = 1;
        }

        group.tile_sizes = tile_sizes;

        for (const auto &prod_g : prod_groups) {
            for (const FStage &s : prod_g.members) {
                group.inlined.insert(s.func.name());
            }
        }

        for (const string &f : cons.inlined) {
            group.inlined.insert(f);
        }

        group_analysis = analyze_group(group, false);
        best_tile_config = tile_sizes;

    } else {
        pair<map<string, Expr>, GroupAnalysis> config = find_best_tile_config(group);
        best_tile_config = config.first;
        group_analysis = config.second;
    }

    return GroupConfig(best_tile_config, group_analysis);
}

Expr Partitioner::estimate_benefit(const GroupAnalysis &old_grouping,
                                   const GroupAnalysis &new_grouping,
                                   bool no_redundant_work,
                                   bool ensure_parallelism) const {
    // TODO: Instead of having a hard parallelism constraint, it may be better
    // to consider other metric, such as arith_cost/parallelism
    if (ensure_parallelism &&
        (!new_grouping.parallelism.defined() ||
         !can_prove(new_grouping.parallelism >= arch_params.parallelism))) {
        return Expr();
    }

    if (!old_grouping.cost.defined() || !new_grouping.cost.defined()) {
        return Expr();
    }

    Expr arith_benefit = old_grouping.cost.arith - new_grouping.cost.arith;
    if (no_redundant_work && !can_prove(arith_benefit >= 0)) {
        return Expr();
    }
    Expr mem_benefit = old_grouping.cost.memory - new_grouping.cost.memory;
    return simplify(mem_benefit + arith_benefit);
}

Expr Partitioner::estimate_benefit(
    const vector<pair<GroupingChoice, GroupConfig>> &new_grouping,
    bool no_redundant_work, bool ensure_parallelism) const {

    set<FStage> old_groups;

    GroupAnalysis new_group_analysis(Cost(0, 0), Int(64).max());
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
            new_group_analysis.cost.memory += analysisg.cost.memory;
            new_group_analysis.parallelism = min(new_group_analysis.parallelism,
                                                 analysisg.parallelism);
        } else {
            new_group_analysis.cost = Cost();
            new_group_analysis.parallelism = Expr();
            break;
        }
    }
    new_group_analysis.simplify();

    GroupAnalysis old_group_analysis(Cost(0, 0), Int(64).max());
    for (const auto &g : old_groups) {
        const auto &iter = group_costs.find(g);
        internal_assert(iter != group_costs.end());
        GroupAnalysis analysisg = iter->second;
        if (analysisg.defined()) {
            old_group_analysis.cost.arith += analysisg.cost.arith;
            old_group_analysis.cost.memory += analysisg.cost.memory;
            old_group_analysis.parallelism = min(old_group_analysis.parallelism,
                                                 analysisg.parallelism);
        } else {
            old_group_analysis.cost = Cost();
            old_group_analysis.parallelism = Expr();
            break;
        }
    }
    old_group_analysis.simplify();

    return estimate_benefit(old_group_analysis, new_group_analysis,
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
            dep_analysis.regions_required(g.output.func, g.output.stage_num,
                                          bounds, prods, false, &costs.input_estimates);
        map<string, Box> group_alloc;
        for (const FStage &s : g.members) {
            const auto &iter = reg_alloc.find(s.func.name());
            if ((iter != reg_alloc.end()) && (s.func.name() != g.output.func.name())) {
                group_alloc[s.func.name()] = iter->second;
            }
        }

        group_storage_bounds[gpair.first] = group_alloc;
    }

    return group_storage_bounds;
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
            dep_analysis.regions_required(g.output.func, g.output.stage_num,
                                          bounds, prods, true, &costs.input_estimates);

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

pair<VarOrRVar, VarOrRVar> Partitioner::split_dim(
    const Group &g, Stage f_handle, int stage_num, const Definition &def,
    bool is_group_output, const VarOrRVar &v, const Expr &factor, const string &in_suffix,
    const string &out_suffix, map<string, Expr> &estimates, AutoSchedule &sched) {
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
    // TailStrategy::Auto). However, the default of pure vars in update definitions
    // is RoundUp, which may introduces an out-of-bound error if it is an access
    // to inputs or outputs.
    //
    // We could have just used GuardWithIf when splitting pure vars in update
    // definition to ensure no out-of-bounds error. However, this is only
    // necessary, if the update definition involves accesses to inputs or outputs.
    // For other accesses, we could potentially use a more aggressive tail strategy
    // such as RoundUp or ShiftInwards. Note that if we use RoundUp or ShiftInwards,
    // any nested loops (generated by compute_at) will be affected as well. However,
    // since in the current auto-scheduler model, we always compute_at at the group
    // output, if the update definition is not the group output, we do not need to
    // care for the nested loops. If it is the update definition of the group output
    // however, we'd better make sure that no other member of the groups accesses
    // the inputs or outputs.

    TailStrategy strategy = TailStrategy::Auto;
    if ((stage_num > 0) && !v.is_rvar) {
        // TODO: It's safe to use RoundUp here if we know there are no
        // loads from any input, but at this point we've lost track of
        // loads from inputs that happen inside inlined Funcs.
        strategy = TailStrategy::GuardWithIf;
    }

    f_handle.split(v, outer, inner, factor, strategy);

    std::ostringstream oss;
    oss << "split(" << arg_name << ", " << outer_name << ", " << inner_name << ", " << factor;
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
        internal_error;
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
                                  Definition def, const Function &func, bool is_group_output,
                                  const Target &t, set<string> &rvars,
                                  map<string, Expr> &estimates, AutoSchedule &sched) {
    vector<Dim> &dims = def.schedule().dims();
    int vec_dim_index = -1;

    // Set the vector length as the maximum of the natural vector size of all
    // values produced by the function.
    int vec_len = 0;
    for (const auto &type : func.output_types()) {
        vec_len = std::max(vec_len, t.natural_vector_size(type));
    }

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        string dim_name = get_base_name(dims[d].var);
        bool can_vectorize = true;
        if (rvars.find(dim_name) != rvars.end()) {
            can_vectorize = can_parallelize_rvar(dim_name, func.name(), def);
        }
        const auto &iter = estimates.find(dim_name);
        if ((iter != estimates.end()) && iter->second.defined()) {
            if (can_vectorize && can_prove(iter->second >= vec_len)) {
                vec_dim_index = d;
                break;
            }
        }
    }

    if (vec_dim_index >= 0) {
        string vec_dim_name = get_base_name(dims[vec_dim_index].var);
        bool is_rvar = (rvars.find(vec_dim_name) != rvars.end());
        internal_assert(is_rvar == dims[vec_dim_index].is_rvar());

        VarOrRVar vec_var(vec_dim_name, is_rvar);
        pair<VarOrRVar, VarOrRVar> split_vars =
            split_dim(g, f_handle, stage_num, def, is_group_output, vec_var, vec_len,
                      "_vi", "_vo", estimates, sched);

        f_handle.vectorize(split_vars.first);
        sched.push_schedule(f_handle.name(), stage_num,
                            "vectorize(" + split_vars.first.name() + ")",
                            {split_vars.first.name()});

        if (is_rvar) {
            rvars.erase(vec_dim_name);
            rvars.insert(split_vars.first.name());
            rvars.insert(split_vars.second.name());
        }

        // TODO: Reorder vector dim to innermost if it is the innermost
        // storage dimension of the func.
        //
        // TODO: Check if the warning is necessary.
        if (vec_dim_index > 0) {
            user_warning << "Outer dim vectorization of var \"" << vec_dim_name
                         << "\" in function \"" << f_handle.name() << "\"\n";
        }
    }
}

// Return true if the vars/rvars in 'ordering' are in the same order as the
// dim list.
inline bool operator==(const vector<Dim> &dims, const vector<VarOrRVar> &ordering) {
    if (dims.size() != ordering.size() + 1) {  // The dim list also contains '__outermost'
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
inline bool operator!=(const vector<Dim> &dims, const vector<VarOrRVar> &ordering) {
    return !(dims == ordering);
}

void Partitioner::reorder_dims(Stage f_handle, int stage_num, Definition def,
                               map<string, Expr> strides, AutoSchedule &sched) {
    vector<Dim> &dims = def.schedule().dims();
    internal_assert(dims.size() > 1);
    vector<pair<string, int>> order;

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        internal_assert(strides.find(dims[d].var) != strides.end());
    }

    // Iterate until all the dimensions have been assigned an order
    while (!strides.empty()) {
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
        if (!min_impure_var.empty() && can_prove(min_impure_stride < min_pure_stride)) {
            curr_min_var.first = min_impure_var;
            curr_min_var.second = min_impure_index;
            internal_assert(dims[min_impure_index].is_rvar());
        } else {
            curr_min_var.first = min_pure_var;
            curr_min_var.second = min_pure_index;
        }

        order.push_back(curr_min_var);
        strides.erase(curr_min_var.first);
    }

    vector<VarOrRVar> ordering;
    for (const auto &o : order) {
        VarOrRVar o_var(o.first, dims[o.second].is_rvar());
        ordering.push_back(o_var);
    }

    internal_assert(!ordering.empty());
    set<string> var_list = {ordering[0].name()};
    string var_order = ordering[0].name();
    for (size_t o = 1; o < ordering.size(); o++) {
        var_order += ", " + ordering[o].name();
        var_list.insert(ordering[o].name());
    }

    if (dims != ordering) {
        f_handle.reorder(ordering);
        sched.push_schedule(f_handle.name(), stage_num, "reorder(" + var_order + ")", var_list);
    }
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

    FindVarsUsingVar(const string &var) {
        vars.push(var);
    }
};

void Partitioner::generate_group_cpu_schedule(
    const Group &g, const Target &t,
    const map<FStage, DimBounds> &group_loop_bounds,
    const map<string, Box> &group_storage_bounds,
    const set<string> &inlines,
    AutoSchedule &sched) {
    string out_f_name = g.output.func.name();
    Function g_out = g.output.func;

    debug(3) << "\n================\n"
             << "Scheduling group:\n"
             << "================\n"
             << g;

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
        int stage_num = g.output.stage_num;
        f_handle = Func(g_out).update(stage_num - 1);
    } else {
        Func(g_out).compute_root();
        sched.push_schedule(f_handle.name(), g.output.stage_num, "compute_root()", {});
    }

    // Realize tiling and update the dimension estimates
    vector<VarOrRVar> outer_dims;
    vector<VarOrRVar> inner_dims;

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

    // Reorder the dimensions for better spatial locality (i.e. smallest stride
    // is innermost). If we only have one dimension (excluding __outermost),
    // there is nothing to reorder.
    if (dims.size() > 2) {
        map<string, Expr> strides =
            analyze_spatial_locality(g.output, group_storage_bounds, inlines);
        if (!strides.empty()) {
            reorder_dims(f_handle, g.output.stage_num, def, strides, sched);
        }
    }

    vector<string> dim_vars(dims.size() - 1);
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        dim_vars[d] = get_base_name(dims[d].var);
    }

    // Apply tiling to output of the group
    for (const auto &var : dim_vars) {
        bool is_rvar = (rvars.find(var) != rvars.end());
        VarOrRVar v(var, is_rvar);

        const auto &iter = g.tile_sizes.find(var);
        if ((iter != g.tile_sizes.end()) &&
            get_element(stg_estimates, var).defined() &&
            can_prove(get_element(stg_estimates, var) > iter->second)) {
            const Expr &tile_size = iter->second;
            if (can_prove(tile_size == 1)) {
                outer_dims.push_back(v);
            } else {
                pair<VarOrRVar, VarOrRVar> tile_vars =
                    split_dim(g, f_handle, g.output.stage_num, def, true, v,
                              tile_size, "_i", "_o", stg_estimates, sched);

                inner_dims.push_back(tile_vars.first);
                outer_dims.push_back(tile_vars.second);

                if (is_rvar) {
                    rvars.erase(var);
                    rvars.insert(tile_vars.first.name());
                    rvars.insert(tile_vars.second.name());
                }
            }
        } else {
            inner_dims.push_back(v);
        }
    }

    // Reorder the tile dimensions
    if (!outer_dims.empty()) {

        vector<VarOrRVar> ordering;
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

    vectorize_stage(g, f_handle, g.output.stage_num, def, g_out, true, t,
                    rvars, stg_estimates, sched);

    // Parallelize definition
    Expr def_par = 1;
    // TODO: Investigate if it is better to pull one large dimension and
    // parallelize over it or to generate nested parallelism.
    //
    // Go from the outer to the innermost loop until sufficient parallelism
    // is achieved. Stop the search once we find a vectorized dimension since
    // it doesn't make any sense to have a parallelized inner loop within a
    // vectorized outer loop.
    bool nested_parallelism = true;
    if (nested_parallelism) {
        int dim_start = dims.size() - 2;
        string seq_var;
        for (int d = dim_start; d >= 0; d--) {
            if (dims[d].for_type == ForType::Vectorized) {
                break;
            }

            string var = get_base_name(dims[d].var);
            bool is_rvar = (rvars.find(var) != rvars.end());
            internal_assert(is_rvar == dims[d].is_rvar());
            VarOrRVar v(var, is_rvar);

            if (is_rvar && !can_parallelize_rvar(var, g_out.name(), def)) {
                if (seq_var.empty()) {
                    seq_var = var;
                }
                continue;
            }

            if (can_prove(def_par >= arch_params.parallelism)) {
                // Enough parallelism to saturate target machine
                break;
            }

            const auto &iter = stg_estimates.find(var);
            if ((iter != stg_estimates.end()) && iter->second.defined()) {
                if (!seq_var.empty()) {
                    VarOrRVar seq(seq_var, (rvars.find(seq_var) != rvars.end()));
                    f_handle.reorder(seq, v);
                    sched.push_schedule(f_handle.name(), g.output.stage_num,
                                        "reorder(" + seq_var + ", " + var + ")",
                                        {seq_var, var});
                }
                f_handle.parallel(v);
                sched.push_schedule(f_handle.name(), g.output.stage_num,
                                    "parallel(" + var + ")", {var});
                def_par = simplify(def_par * iter->second);
            } else {
                break;
            }
        }
    }

    if (can_prove(def_par < arch_params.parallelism)) {
        user_warning << "Insufficient parallelism for " << f_handle.name() << "\n";
    }

    // Find the level at which group members will be computed.
    int tile_inner_index = dims.size() - outer_dims.size() - 1;
    VarOrRVar tile_inner_var(Var::outermost());
    if (!outer_dims.empty()) {
        string var_name = get_base_name(dims[tile_inner_index].var);
        bool is_rvar = (rvars.find(var_name) != rvars.end());
        tile_inner_var = VarOrRVar(var_name, is_rvar);
    }

    for (const FStage &mem : g.members) {
        // Skip member stages that have been inlined or stage that is the
        // output stage of the group
        if ((g.inlined.find(mem.func.name()) != g.inlined.end()) ||
            (mem.func.name() == g_out.name())) {
            continue;
        }

        // Get the definition corresponding to the stage
        Definition mem_def = get_stage_definition(mem.func, mem.stage_num);

        // Get the estimates for the dimensions of the member stage
        map<string, Expr> mem_estimates =
            bounds_to_estimates(get_element(group_loop_bounds, mem));

        set<string> mem_rvars;
        vector<Dim> &mem_dims = mem_def.schedule().dims();
        for (int d = 0; d < (int)mem_dims.size() - 1; d++) {
            if (mem_dims[d].is_rvar()) {
                mem_rvars.insert(get_base_name(mem_dims[d].var));
            }
        }

        // Get a function handle for scheduling the stage
        Stage mem_handle = Stage(Func(mem.func));

        if (mem.stage_num > 0) {
            mem_handle = Func(mem.func).update(mem.stage_num - 1);
        } else {
            if (!outer_dims.empty()) {
                if (tile_inner_var.is_rvar) {
                    Func(mem.func).compute_at(Func(g_out), tile_inner_var.rvar);
                } else {
                    Func(mem.func).compute_at(Func(g_out), tile_inner_var.var);
                }
                string sanitized_g_out = get_sanitized_name(g_out.name());
                sched.push_schedule(mem_handle.name(), mem.stage_num,
                                    "compute_at(" + sanitized_g_out + ", " + tile_inner_var.name() + ")",
                                    {sanitized_g_out, tile_inner_var.name()});
            } else {
                user_warning << "Degenerate tiling. No dimensions are tiled"
                             << "\n";
                user_warning << "Computing \"" << mem.func.name() << "\" at root"
                             << "\n";
                Func(mem.func).compute_root();
                sched.push_schedule(mem_handle.name(), mem.stage_num, "compute_root()", {});
            }
        }

        // Reorder the dimensions for better spatial locality. If we only have
        // one dimension (excluding __outermost), there is nothing to reorder.
        if (dims.size() > 2) {
            map<string, Expr> mem_strides =
                analyze_spatial_locality(mem, group_storage_bounds, inlines);
            if (!mem_strides.empty()) {
                reorder_dims(mem_handle, mem.stage_num, mem_def, mem_strides, sched);
            }
        }

        vectorize_stage(g, mem_handle, mem.stage_num, mem_def, mem.func, false,
                        t, mem_rvars, mem_estimates, sched);
    }
}

void Partitioner::generate_cpu_schedule(const Target &t, AutoSchedule &sched) {
    // Grab the group bounds early as they rely on the dimensions of the group
    // outputs which will be altered by modifying schedules.
    map<FStage, map<FStage, DimBounds>> loop_bounds = group_loop_bounds();
    map<FStage, map<string, Box>> storage_bounds = group_storage_bounds();

    set<string> inlines;
    // Mark all functions that are inlined.
    for (const pair<const FStage, Group> &g : groups) {
        for (const string &inline_func : g.second.inlined) {
            inlines.insert(inline_func);
        }
    }

    // TODO: Inlining functions with update definitions has different
    // behavior than pure functions. They may need to be computed above
    // the innermost vector loop to avoid complications with varying
    // extents across different vector lanes.
    //
    // Since the default schedule is compute inline, we don't need to
    // explicitly call compute_inline() on the function.

    // Realize schedule for each group in the pipeline.
    for (const auto &g : groups) {
        generate_group_cpu_schedule(g.second, t, get_element(loop_bounds, g.first),
                                    get_element(storage_bounds, g.first), inlines, sched);
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
    call_args.emplace_back(stg.func.name(), def.args());

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
void validate_no_partial_schedules(const Function &f, bool is_output) {
    if (f.has_extern_definition()) {
        return;
    }

    // Verify no compute_root or bounds are specified
    user_assert(f.schedule().compute_level().is_inlined())
        << "AutoSchedule: cannot auto-schedule function \"" << f.name()
        << "\" since it is scheduled to be computed at root\n";

    user_assert(is_output || f.schedule().bounds().empty())
        << "AutoSchedule: cannot auto-schedule function \"" << f.name()
        << "\" since it has partially specified bounds\n";

    int num_stages = f.updates().size() + 1;
    for (int stage = 0; stage < num_stages; ++stage) {
        const Definition &def = get_stage_definition(f, stage);
        const StageSchedule &schedule = def.schedule();

        // Verify no splits are specified
        user_assert(schedule.splits().empty())
            << "AutoSchedule: cannot auto-schedule function \"" << f.name()
            << "\" since it has partially specified schedules at stage " << stage << "\n";

        // Verify that none of the dimensions are scheduled to be parallelized or
        // vectorized, or unrolled.
        for (const auto &d : schedule.dims()) {
            user_assert(d.for_type == ForType::Serial)
                << "AutoSchedule: cannot auto-schedule function \"" << f.name()
                << "\" since stage " << stage << " is not serial at dim " << d.var << "\n";
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
                    std::find_if(args.begin(), args.end(),
                                 [&d](const Expr &arg) {
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
        const Function &f1 = env.at(order[i]);
        if (!unbounded.count(f1.name())) {
            continue;
        }
        inlined = true;
        debug(4) << "Function \"" << order[i] << "\" is unbounded\n";
        for (int j = i + 1; j < (int)order.size(); ++j) {
            internal_assert(order[i] != order[j]);
            const Function &f2 = env.at(order[j]);
            debug(5) << "Inline unbounded function \"" << f1.name()
                     << "\" inside \"" << f2.name() << "\"\n";
            inline_function(f2, f1);
        }
    }
    return inlined;
}

}  // anonymous namespace

// Generate schedules for all functions in the pipeline required to compute the
// outputs. This applies the schedules and returns a string representation of
// the schedules. The target architecture is specified by 'target'.
string generate_schedules(const vector<Function> &outputs, const Target &target,
                          const ArchParams &arch_params) {
    // Make an environment map which is used throughout the auto scheduling process.
    map<string, Function> env;
    for (const Function &f : outputs) {
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
        bool is_output = false;
        for (const auto &o : outputs) {
            is_output |= iter.second.same_as(o);
        }
        validate_no_partial_schedules(iter.second, is_output);
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
        for (const Function &f : outputs) {
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
        for (const Function &f : outputs) {
            map<string, Function> more_funcs = find_transitive_calls(f);
            env.insert(more_funcs.begin(), more_funcs.end());
        }
        order = realization_order(outputs, env).first;
    }

    // Compute the bounds of function values which are used for dependence analysis.
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
        for (const Function &f : outputs) {
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
        pipeline_bounds = get_pipeline_bounds(dep_analysis, outputs, &costs.input_estimates);
    }

    debug(2) << "Initializing partitioner...\n";
    Partitioner part(pipeline_bounds, arch_params, outputs, dep_analysis, costs);

    // Compute and display reuse
    /* TODO: Use the reuse estimates to reorder loops
    for (const auto &f : env) {
        FindAllCalls find;
        f.second.accept(&find);
        int num_stages = f.second.updates().size() + 1;
        for (int s = 0; s < num_stages; s++) {
            FStage curr_s(f.second, s);
            map<string, Expr> reuse = part.evaluate_reuse(curr_s, find.funcs_called);
            debug(0) << curr_s << "\n";
            for (const auto &dir : reuse) {
                debug(0) << dir.first << " " << dir.second << ",";
            }
            debug(0) << "\n";
        }
    }*/

    // Display the current pipeline graph.
    // TODO: Output the graph in dot format.
    if (debug::debug_level() >= 3) {
        part.disp_pipeline_graph();
        part.disp_pipeline_bounds();
    }

    debug(2) << "Partitioner initializing groups...\n";
    part.initialize_groups();
    if (debug::debug_level() >= 3) {
        part.disp_pipeline_costs();
    }

    debug(2) << "Partitioner computing inline group...\n";
    part.group(Partitioner::Level::Inline);
    if (debug::debug_level() >= 3) {
        part.disp_grouping();
    }

    debug(2) << "Partitioner computing fast-mem group...\n";
    part.grouping_cache.clear();
    part.group(Partitioner::Level::FastMem);
    if (debug::debug_level() >= 3) {
        part.disp_pipeline_costs();
        part.disp_grouping();
        part.disp_pipeline_graph();
    }

    debug(2) << "Initializing AutoSchedule...\n";
    AutoSchedule sched(env, top_order);
    debug(2) << "Generating CPU schedule...\n";
    part.generate_cpu_schedule(target, sched);

    std::ostringstream oss;
    oss << sched;
    string sched_string = oss.str();

    debug(3) << "\n\n*******************************\nSchedule:\n"
             << "*******************************\n"
             << sched_string << "\n\n";

    // TODO: Unify both inlining and grouping for fast mem
    // TODO: GPU scheduling
    // TODO: Hierarchical tiling

    return sched_string;
}

struct Mullapudi2016 {
    void operator()(const Pipeline &pipeline, const Target &target, const AutoschedulerParams &params_in, AutoSchedulerResults *outputs) {
        internal_assert(params_in.name == "Mullapudi2016");

        AutoSchedulerResults results;
        results.target = target;
        results.autoscheduler_params = params_in;

        std::vector<Function> pipeline_outputs;
        for (const Func &f : pipeline.outputs()) {
            pipeline_outputs.push_back(f.function());
        }

        ArchParams arch_params;
        {
            ParamParser parser(params_in.extra);
            parser.parse("parallelism", &arch_params.parallelism);
            parser.parse("last_level_cache_size", &arch_params.last_level_cache_size);
            parser.parse("balance", &arch_params.balance);
            parser.finish();
        }
        results.schedule_source = generate_schedules(pipeline_outputs, target, arch_params);
        results.autoscheduler_params = params_in;
        // this autoscheduler has no featurization
        *outputs = std::move(results);
    }
};

REGISTER_AUTOSCHEDULER(Mullapudi2016)

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide
