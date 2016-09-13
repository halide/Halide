#include<algorithm>

#include "AutoSchedule.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::map;
using std::set;
using std::deque;
using std::pair;
using std::make_pair;

/* Helper function to simplify the upper and lower bounds
 * of each dimension of a box.*/
void simplify_box(Box &b) {
    for (size_t i = 0; i < b.size(); i++) {
        b[i].min = simplify(b[i].min);
        b[i].max = simplify(b[i].max);
    }
}

/* Helper function to merge the partial region map into the result
 * region map. */
void merge_regions(map<string, Box> &result, map<string, Box> &partial) {
    // Merge regions from partial with an existing region if any in the result.
    for (auto &reg : partial) {
        if (result.find(reg.first) == result.end()) {
            result[reg.first] = reg.second;
        } else {
            merge_boxes(result[reg.first], reg.second);
        }
    }
}

/* Representation of a function stage in the pipeline. */
struct FStage {
    Function func;
    uint32_t stage_num;
    FStage(Function func, uint32_t stage_num) :
          func(func), stage_num(stage_num) {}

    bool operator==(const FStage &other_stage) const {
        return (func.name() == other_stage.func.name()) &&
               (stage_num == other_stage.stage_num);
    }

    bool operator<(const FStage &other_stage) const {
        return func.name() < other_stage.func.name() ||
                (func.name() == other_stage.func.name() &&
                 stage_num < other_stage.stage_num) ;
    }

    friend std::ostream& operator<<(std::ostream &stream, const FStage &s) {
        stream << "(" << s.func.name() << "," << s.stage_num << ")";
        return stream;
    }
};

/* Helper function to set the compute and store level of all the function
 * stages in the environment as root. */
void set_schedule_defaults(map<string, Function> &env) {
    for (auto &kv : env) {
        kv.second.schedule().store_level() = LoopLevel::root();
        kv.second.schedule().compute_level() = LoopLevel::root();

        // Set the schedule for each update definitions.
        for (size_t u = 0; u < kv.second.updates().size(); u++) {
            kv.second.update_schedule(u).store_level() = LoopLevel::root();
            kv.second.update_schedule(u).compute_level() = LoopLevel::root();
        }
    }
}

/* Returns true if all the pipeline outputs have estimates specified
 * on each of their dimensions. */
bool check_estimates_on_outputs(const vector<Function> &outputs) {
    bool estimates_avail = true;
    for (auto &out : outputs) {
        const vector<Bound> &estimates = out.schedule().estimates();
        if (estimates.size() != out.args().size()) {
            estimates_avail = false;
            break;
        }
        const vector<string> &vars = out.args();
	// Check if the estimate for each dimension is available and it is an integer.
        for (uint32_t i = 0; i < estimates.size(); i++) {
            if (std::find(vars.begin(), vars.end(), estimates[i].var) == vars.end() ||
                !((estimates[i].min.as<IntImm>()) && (estimates[i].extent.as<IntImm>()))) {
                estimates_avail = false;
                break;
            }
        }
    }
    return estimates_avail;
}

struct DependenceAnalysis {
    // Map containing all the functions in the pipeline.
    const map<string, Function> &env;
    const FuncValueBounds &func_val_bounds;

    /* TODO: Auto scheduling for large benchmarks is bottlenecked by the bounds inference.
       The bounds queries with the same parameters are common during the grouping process
       it might be beneficial to build a cache for bounds queries. */

    DependenceAnalysis(map<string, Function> &env,
                       const FuncValueBounds &func_val_bounds):
                       env(env), func_val_bounds(func_val_bounds) {}

    /* Returns the regions of the producers (prods) required to compute the region
     * of the function stage (f, stage_num) specified by bounds.*/
    map<string, Box> regions_required(Function f, int stage_num,
                                      const DimBounds &bounds,
                                      const set<string> &prods,
                                      bool only_regions_computed);

    /* Returns the regions of the producers (prods) required to compute the region
     * of the function specified by pure_bounds.*/
    map<string, Box> regions_required(Function f,
                                      const DimBounds &pure_bounds,
                                      const set<string> &prods,
                                      bool only_regions_computed);

    /* Returns redundantly computed regions of producers (prods) while computing a
     * region of the function stage (f, stage_num) specified by bounds. var is the
     * dimension along which redundant computation is accounted for.*/
    map<string, Box> redundant_regions(Function f, int stage_num, string var,
                                       const DimBounds &bounds,
                                       const set<string> &prods,
                                       bool only_regions_computed);

    /* Returns overlapping regions of producers (prods) while computing a function
     * stage along each of the dimensions.*/
    vector<map<string, Box>>
    overlap_regions(Function f, int stage_num, const DimBounds &bounds,
                    const set<string> &prods, bool only_regions_computed);
};

/* Returns the regions of the producers (prods) required to compute the region
 * of the function specified by pure_bounds.*/
map<string, Box>
DependenceAnalysis::regions_required(Function f, const DimBounds &pure_bounds,
                                     const set<string> &prods,
                                     bool only_regions_computed) {
    // Find the regions required for each stage and merge them.
    map<string, Box> regions;
    int num_stages = f.updates().size() + 1;
    for (int s = 0; s < num_stages; s++) {
        DimBounds bounds = get_stage_bounds(f, s, pure_bounds);
        map<string, Box> stage_regions =
                regions_required(f, s, bounds, prods, only_regions_computed);

        merge_regions(regions, stage_regions);
    }
    return regions;
}

/* Helper function to queue regions that need to be traversed.
 * f_queue is the queue into which the regions specified by
 * prod_func and region will be added.*/
void queue_func_regions(deque<pair<FStage, DimBounds>> &f_queue,
                        const Function &prod_func, Box &region) {
    DimBounds prod_pure_bounds;
    const vector<string> &args = prod_func.args();

    internal_assert(region.size() == args.size());

    // The region only specifies by the extent of each dimension
    // by position. Populating a map which is keyed by name.
    for (size_t v = 0; v < args.size(); v++) {
        prod_pure_bounds[args[v]] = region[v];
    }

    // Get the bounds for all the stages in a function from the
    // bounds on the pure dimensions.
    vector<DimBounds> prod_bounds =
            get_stage_bounds(prod_func, prod_pure_bounds);

    size_t num_stages = prod_func.updates().size() + 1;

    internal_assert(prod_bounds.size() == num_stages);

    // Add all the stages of a function into the queue.
    for (size_t prod_s = 0; prod_s < num_stages; prod_s++) {
        FStage prod_stage(prod_func, prod_s);
        f_queue.push_back(make_pair(prod_stage, prod_bounds[prod_s]));
    }
}

/* Helper function for merging curr_regions to the global map of regions
 * and adding them to the queue of regions that need to be traversed.
 * prods is the set of producer functions that are under consideration.*/
void merge_and_queue_regions(deque<pair<FStage, DimBounds>> &f_queue,
                             map<string, Box> &regions,
                             map<string, Box> &curr_regions,
                             const set<string> &prods,
                             const map<string, Function> &env,
                             bool only_regions_computed, string curr_func_name) {
    for (auto &reg : curr_regions) {
        // Merge region with an existing region for the function in the
        // global map. Do not merge the parent function itself to the region
        // when querying only for the values computed.
        if (!only_regions_computed || (only_regions_computed && reg.first != curr_func_name)) {
            if (regions.find(reg.first) == regions.end()) {
                regions[reg.first] = reg.second;
            } else {
                merge_boxes(regions[reg.first], reg.second);
            }
        }

        // Skip adding the current region into to the queue if the function
	    // is not in prods.
        if (prods.find(reg.first) == prods.end()) {
            continue;
        }

        if (env.find(reg.first) != env.end() && reg.first != curr_func_name) {
            // Add all the stages of the function representing the
            // region into the queue.
            queue_func_regions(f_queue, env.at(reg.first), reg.second);
        }
    }
}

/* Returns the regions of the producers (prods) required to compute the region
 * of the function stage (f, stage_num) specified by bounds.*/
map<string, Box>
DependenceAnalysis::regions_required(Function f, int stage_num,
                                     const DimBounds &bounds,
                                     const set<string> &prods,
                                     bool only_regions_computed) {
    // Iteratively compute the required regions by doing a traversing the chain
    // of dependencies.

    // Map of all the regions that are required.
    map<string, Box> regions;
    deque<pair<FStage, DimBounds>> f_queue;

    // Add the query function and its region to the queue
    FStage start(f, stage_num);
    f_queue.push_back(make_pair(start, bounds));

    while(!f_queue.empty()) {

        FStage s = f_queue.front().first;
        DimBounds curr_bounds = f_queue.front().second;

        Definition def = get_stage_definition(s.func, s.stage_num);
        // Scope for containing all the estimates on parameters and intervals.
        Scope<Interval> curr_scope;

        const vector<Dim> &dims = def.schedule().dims();

        // Substitute parameter estimates into the bounds and add them to the
        // current scope.
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            string var_name = dims[d].var;
            internal_assert(curr_bounds.find(var_name) != curr_bounds.end());

            Expr lower = SubstituteVarEstimates().mutate(curr_bounds.at(dims[d].var).min);
            Expr upper = SubstituteVarEstimates().mutate(curr_bounds.at(dims[d].var).max);
            Interval simple_bounds = Interval(simplify(lower), simplify(upper));
            curr_scope.push(var_name, simple_bounds);
        }

        // If the function has an extern definition there is no visibility into
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
                    // If the argument is an entire function the bounds of the
                    // function required are unknown. Create an infinite region
                    // of the correct dimension, update the region map, and
                    // add it to the queue.
                    string prod_name = Function(arg.func).name();
                    const Function &prod_func = env.at(prod_name);
                    map<string, Box> prod_reg;
                    const vector<string> &args = prod_func.args();
                    for (size_t v = 0; v < args.size(); v++) {
                        prod_reg[prod_name].push_back(Interval());
                    }
                    merge_and_queue_regions(f_queue, regions, prod_reg, prods,
                                            env, only_regions_computed, s.func.name());
                } else if (arg.is_expr()) {
                    // Find the boxes required for the expression and add the regions
                    // to the queue.
                    Expr subs_arg = SubstituteVarEstimates().mutate(arg.expr);
                    map<string, Box> arg_regions =
                            boxes_required(subs_arg, curr_scope, func_val_bounds);

                    merge_and_queue_regions(f_queue, regions, arg_regions, prods,
                                            env, only_regions_computed, s.func.name());
                } else if (arg.is_image_param() || arg.is_buffer()) {
                    // If the argument is an image or a buffer the bounds
                    // required are unknown. Create an infinite region of the
                    // correct dimension and update the region map.
                    BufferPtr buf;
                    if (arg.is_image_param()) {
                        buf = arg.image_param.get_buffer();
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
        }

        // Find the regions required for each value of the current function stage,
        // update the region map, and add them to the queue.
        for (auto &val : def.values()) {
            // Substitute the parameter estimates into the expression and get
            // the regions required for the expression.
            Expr subs_val = SubstituteVarEstimates().mutate(val);
            map<string, Box> curr_regions =
                    boxes_required(subs_val, curr_scope, func_val_bounds);

            // Arguments to the definition may require regions of functions.
            // For example, update definitions in histograms where the bin is
            // based on the value of a function.
            Box left_reg;
            for (const Expr &arg : def.args()) {
                Expr subs_arg = SubstituteVarEstimates().mutate(arg);
                map<string, Box> arg_regions =
                        boxes_required(subs_arg, curr_scope, func_val_bounds);

                // Merge the regions with the regions found while looking at
                // the values.
                merge_regions(curr_regions, arg_regions);

                Interval arg_bounds =
                        bounds_of_expr_in_scope(arg, curr_scope, func_val_bounds);
                left_reg.push_back(arg_bounds);
            }

            if (curr_regions.find(s.func.name()) == curr_regions.end()) {
                curr_regions[s.func.name()] = left_reg;
            } else {
                merge_boxes(curr_regions[s.func.name()], left_reg);
            }

            // Update the region map, and add curr_regions to the queue.
            merge_and_queue_regions(f_queue, regions, curr_regions,
                                    prods, env, only_regions_computed, s.func.name());
        }
        // Remove processed region from the queue.
        f_queue.pop_front();
    }

    // Simplify the bounds on each of the regions and substitute global pipeline
    // bounds for function regions where the lower and upper bounds could not be
    // determined.
    map<string, Box> concrete_regions;

    for (auto &f_reg : regions) {
        simplify_box(f_reg.second);

        Box concrete_box;
        for (size_t i = 0; i < f_reg.second.size(); i++) {
            Expr lower = f_reg.second[i].min;
            Expr upper = f_reg.second[i].max;

            bool in_env = (env.find(f_reg.first) != env.end());

            if (!lower.as<IntImm>() && in_env) {
                const Function &curr_f = env.at(f_reg.first);
                for (auto &b : curr_f.schedule().estimates()) {
                    size_t num_pure_args = curr_f.args().size();
                    if (i < num_pure_args && b.var == curr_f.args()[i]) {
                        lower = Expr(b.min.as<IntImm>()->value);
                    }
                }
            }

            if (!upper.as<IntImm>() && in_env) {
                const Function &curr_f = env.at(f_reg.first);
                for (auto &b : curr_f.schedule().estimates()) {
                    size_t num_pure_args = curr_f.args().size();
                    if (i < num_pure_args && b.var == curr_f.args()[i]) {
                        const IntImm * bmin = b.min.as<IntImm>();
                        const IntImm * bextent = b.extent.as<IntImm>();
                        upper = Expr(bmin->value + bextent->value - 1);
                    }
                }
            }

            Interval concrete_bounds = Interval(lower, upper);
            concrete_box.push_back(concrete_bounds);
        }
        concrete_regions[f_reg.first] = concrete_box;
    }
    return concrete_regions;
}

/* Returns redundantly computed regions of producers (prods) while computing a
 * region of the function stage (f, stage_num) specified by bounds. var is the
 * dimension along which redundant computation is accounted for.*/
map<string, Box>
DependenceAnalysis::redundant_regions(Function f, int stage_num, string var,
                                      const DimBounds &bounds,
                                      const set<string> &prods,
                                      bool only_regions_computed) {
    // Find the regions required to compute the region of f specified
    // by bounds.
    map<string, Box> regions = regions_required(f, stage_num, bounds,
                                                prods, only_regions_computed);

    // Shift the bounds by the size of the interval along the direction
    // of var.
    DimBounds shifted_bounds;

    for (auto &b : bounds) {
        if (b.first == var) {
            Expr len = b.second.max - b.second.min + 1;
            Interval bound = Interval(b.second.min + len,
                                      b.second.max + len);
            shifted_bounds[b.first] = bound;
        } else {
            shifted_bounds[b.first] = b.second;
        }
    }

    // Find the regions required to compute the region of f specified
    // by shifted_bounds.
    map<string, Box> regions_shifted =
            regions_required(f, stage_num, shifted_bounds, prods,
                             only_regions_computed);

    // Compute the overlaps between the regions_shifted and the original
    // regions required.
    map<string, Box> overlaps;
    for (auto &reg : regions) {
        if (regions_shifted.find(reg.first) == regions.end()) {
            // It will be interesting to log cases where this actually happens
            // i.e., the shifted regions do not contain a function that was
            // there in the original regions.
            continue;
        }
        Box b = reg.second;
        Box b_shifted = regions_shifted[reg.first];
        // The boxes should be of the same size.
        internal_assert(b.size() == b_shifted.size());

        Box b_intersect;
        for (uint32_t i = 0 ; i < b.size(); i++) {
            b_intersect.push_back(Interval::make_intersection(b[i], b_shifted[i]));
        }
        // A function should appear once in the regions and therefore cannot
        // already be present in the overlaps map.
        internal_assert(overlaps.find(reg.first) == overlaps.end());
        overlaps[reg.first] = b_intersect;
    }

    // Simplify the bounds of each of the overlap regions.
    for (auto &f : overlaps) {
        simplify_box(f.second);
    }

    return overlaps;
}

/* Returns overlapping regions of producers (prods) while computing a function
 * stage along each of the dimensions.*/
vector<map<string, Box>>
DependenceAnalysis::overlap_regions(Function f, int stage_num,
                                    const DimBounds &bounds,
                                    const set<string> &prods,
                                    bool only_regions_computed) {
    vector<map<string, Box>> conc_overlaps;

    Definition def = get_stage_definition(f, stage_num);
    const vector<Dim> &dims = def.schedule().dims();

    // Get the redundant regions along each dimension of f.
    for (int d = 0; d < (int)dims.size(); d++) {
        map<string, Box> conc_reg =
                redundant_regions(f, stage_num, dims[d].var,
                                  bounds, prods, only_regions_computed);
        conc_overlaps.push_back(conc_reg);
    }
    return conc_overlaps;
}

/* Returns the regions of each function required for computing the
 * outputs of the pipeline.*/
map<string, Box> get_pipeline_bounds(DependenceAnalysis &analysis,
                                     const vector<Function> &outputs) {
    map<string, Box> pipeline_bounds;

    // Find the regions required for each of the outputs and merge them
    // to compute the full pipeline_bounds.
    for (auto &out : outputs) {
        DimBounds pure_bounds;
        Box out_box;
        // Use the estimates on the output for determining the output bounds.
        for (auto &arg : out.args()) {
            bool estimate_found = false;
            for (auto &est : out.schedule().estimates()) {
                if (est.var == arg) {
                    Interval I = Interval(est.min, simplify(est.min + est.extent - 1));
                    pure_bounds[arg] = I;
                    out_box.push_back(I);
                    estimate_found = true;
                    break;
                }
            }
            if (!estimate_found) {
                pure_bounds[arg] = Interval();
            }
        }

        set<string> prods;
        for (const pair<string, Function> fpair : analysis.env) {
            prods.insert(fpair.first);
        }

        map<string, Box> regions =
                analysis.regions_required(out, pure_bounds, prods, false);

        // Add the output region to the pipeline bounds as well.
        regions[out.name()] = out_box;

        merge_regions(pipeline_bounds, regions);
    }

    return pipeline_bounds;
}

/* Implements the grouping algorithm and the cost model for making the grouping
 * choices. */
struct Partitioner {
    /* FusionChoice encodes the fusion of the prod function into the cons stage.*/
    struct FusionChoice {
        string prod;
        FStage cons;

        FusionChoice(string prod, FStage cons) : prod(prod), cons(cons) {}

        bool operator==(const FusionChoice &other) const {
            return (prod == other.prod) && (cons == other.cons);
        }

        bool operator<(const FusionChoice &other) const {
            return prod < other.prod || (prod == other.prod && cons < other.cons);
        }

        friend std::ostream& operator<<(std::ostream &stream,
                                        const FusionChoice &choice) {
            stream << "Choice:" << choice.prod << "->" << choice.cons << '\n';
            return stream;
        }
    };

    /* A group is a sub-pipeline with a single output. Members of a group are
     * either inlined into the consumer functions within the group or computed
     * at tiles of the output, specified by tile_sizes.
     *
     * TODO: The restriction of computing either at the inline or tile level
     * makes the space of scheduling choices for a group very tractable.
     * However, the restriction might miss good schedules which can only be
     * realized by computing the members of the group at different levels of
     * the group.
     *
     * There are two approaches to extending the space of schedules considered:
     * 1) Recursive grouping: Treat the problem of determining the compute levels
     * within a group as a smaller instance of the grouping problem with
     * different parameters for the input, output sizes, and cache model.
     *
     * 2) Tightening: Always compute a function at the lowest level possible
     * without introducing redundant work. This is a restricted form of recursive
     * grouping which does not explore the trade-off between redundant work and
     * locality.
     *
     * Either approach can be implemented as a post process for each group
     * after the initial grouping process finishes. The cost model may
     * already make sub-optimal higher level partitioning when it is not aware
     * of the benefits of the post processing. However, it should strictly be
     * an improvement over the initial grouping. As a first step it is good
     * to make it a post process.
     *
     * Incorporating the recursive grouping process into the cost model can be
     * tricky and can potentially make the cost of analyzing a group
     * prohibitive, as it requires solving smaller instances of the grouping
     * problem for analyzing each configuration. On the other hand tightening
     * can be integrated into the cost model with out significantly increasing
     * the time to analyze a grouping configuration.
     *
     * TODO: Sliding window schedules be implemented as a post pass by moving
     * the store level of all the members of the group to the outermost serial
     * loop. Can be incorporated in the cost model with some effort.
     *
     * TODO: Register tiling is an important transformation especially for
     * benchmarks with significant reuse of the data (like matrix multiply and
     * convolutional layers). The mechanism for realizing register tiling is
     * completely unrolling small tiles of the innermost kernels. Unrolling
     * interacts with vectorization, storage layout, and depends on the outer
     * level tiling.*/
    struct Group {
        // The output stage representing the group.
        FStage output;
        // Functions that belong to the group.
        vector<FStage> members;

        // Members of the group which are inlined.
        set<string> inlined;
        // Tile sizes along dimensions of the output function of the group.
        map<string, int> tile_sizes;

        Group(FStage output, vector<FStage> members):
              output(output), members(members) {}

        friend std::ostream& operator <<(std::ostream &stream, const Group &g) {

            stream << "Output FStage:" << g.output << '\n';
            stream << "Members:" << '[';
            for (auto &m : g.members) {
                stream << m << ",";
            }
            stream << "]" << '\n';

            stream << "Inlined:" << '[';
            for (auto &in : g.inlined) {
                stream << in << ",";
            }
            stream << "]" << '\n';

            stream << "Tile sizes:" << "[";
            for (auto &s : g.tile_sizes) {
                stream << "(" << s.first << "," <<  s.second << ")";
            }
            stream << "]" << '\n';

            return stream;
        }
    };

    /* Result of the analysis of a group.*/
    struct GroupAnalysis {
        // Estimate of the arithmetic and memory cost for computing the group.
        Cost cost;
        // Estimate of the parallelism that can be exploited while computing
        // the group.
        int64_t parallelism;

        friend std::ostream& operator<<(std::ostream &stream,
                                        const GroupAnalysis &analysis) {
            stream << "[arith cost:" << analysis.cost.arith << ",";
            stream << "memory cost:" << analysis.cost.memory << ",";
            stream << "parallelism:" << analysis.parallelism << "]\n";

            return stream;
        }
    };

    /* Configuration of a group and the corresponding analysis. A group is the
     * set of functions that are fused together and the group config specifies at
     * what granularity they are fused together (tile_sizes).*/
    struct GroupConfig {
        map<string, int> tile_sizes;
        GroupAnalysis analysis;
        GroupConfig(const map<string, int> &tile_sizes, const GroupAnalysis &analysis) :
                   tile_sizes(tile_sizes), analysis(analysis) {}
    };

    /* Cache for storing the best configuration for the fusion choice. During
     * the grouping process the impact of fusing two groups together is only
     * limited to the producers and consumers of the groups that are being fused
     * together. The best fusion choices for the rest of the pipeline need not be
     * re-evaluated and caching them improves performance significantly.*/
    map<FusionChoice, GroupConfig> fusion_cache;

    /* Each group in the pipeline has a single output stage. groups is the mapping
     * from the output stage of the group to the group.*/
    map<FStage, Group> groups;
    /* The child stages of each stage in the pipeline.*/
    map<FStage, set<FStage>> children;
    /* Map from the output stage of the group to the analysis of the group. The mapping
     * needs to be updated whenever the grouping changes.*/
    map<FStage, GroupAnalysis> group_costs;

    /* Levels that are targeted by the grouping algorithm. In the INLINE mode the grouping
     * algorithm fuses the functions by inlining the expression for the producer function
     * into the consumer stage. In the FAST_MEM mode the fusion is done at the level of tiles
     * of the group output stage.*/
    enum Level {INLINE, FAST_MEM};

    /* Bounds of each stage function in the pipeline. These bounds are inferred from the
     * estimates of the outputs and other functions in the pipeline.*/
    const map<string, Box> &pipeline_bounds;
    /* Parameters for the machine model used for estimating the cost of each group in
     * the pipeline. */
    const MachineParams &arch_params;
    /* Dependency analysis for the pipeline. Supports queries for regions accessed and
     * computed for producing regions of functions.*/
    DependenceAnalysis &dep_analysis;
    /* The arithmetic and memory costs of evaluating the expressions which define each
     * function in the pipeline.*/
    RegionCosts &costs;
    /* Output functions of the pipeline.*/
    const vector<Function> &outputs;

    Partitioner(map<string, Box> &_pipeline_bounds, const MachineParams &_arch_params,
                DependenceAnalysis &_dep_analysis, RegionCosts &_costs,
                const vector<Function> &_outputs);

    void merge_groups(const FusionChoice &choice, const GroupConfig &eval,
                      Partitioner::Level level);

    GroupConfig evaluate_choice(const FusionChoice &fuse, Partitioner::Level level);

    Group fuse_groups(const Group &g1, const Group &g2);

    GroupAnalysis analyze_group(const Group &g, bool show_analysis);

    map<FStage, map<FStage, DimBounds>> group_loop_bounds();
    map<FStage, map<string, Box>> group_storage_bounds();

    void group(Partitioner::Level level);

    vector<pair<FusionChoice, GroupConfig>>
    choose_candidate_fuse(const vector<pair<string, string>> &cand_pairs,
                          Partitioner::Level level);

    map<string, int64_t> evaluate_reuse(const FStage &stg,
                                        const set<string> &prods);

    map<string, int64_t>
            analyze_spatial_locality(const FStage &stg,
                                     const map<string, Box> &parent_bounds,
                                     const set<string> &inlines = set<string>());

    int64_t find_max_access_stride(const set<string> &vars, string func_acc,
                                   vector<Expr> acc_exprs, const Box &buffer_bounds);

    map<string, int64_t> bounds_to_estimates(const DimBounds &bounds);

    string generate_cpu_schedule(const Target &t);

    string generate_group_cpu_schedule(const Group &g, const Target &t,
                                       const map<FStage, DimBounds> &group_loop_bounds,
                                       const map<string, Box> &group_storage_bounds,
                                       const set<string> &inlines);

    DimBounds get_bounds(const FStage &stg);

    DimBounds get_bounds_from_tile_sizes(const FStage &stg,
                                         const map<string, int> &tile_sizes);

    vector<map<string, int>> generate_tile_configs(const FStage &stg);

    pair<map<string, int>, GroupAnalysis> find_best_tile_config(const Group &g);

    int64_t estimate_benefit(const GroupAnalysis &nofuse, const GroupAnalysis &fuse,
                             bool no_redundant_work, bool ensure_parallelism);

    int64_t estimate_benefit(const vector<pair<FusionChoice, GroupConfig>> &choices,
                             bool no_redundant_work, bool ensure_parallelism);

    void initialize_groups();

    Cost get_pipeline_cost();

    void disp_pipeline_costs(int dlevel);
    void disp_pipeline_bounds(int dlevel);
    void disp_pipeline_graph(int dlevel);
    void disp_grouping(int dlevel);
};

void Partitioner::disp_grouping(int dlevel = debug_level) {
    debug(dlevel) << "\n=========" << '\n';
    debug(dlevel) << "Grouping:" << '\n';
    debug(dlevel) << "=========" << '\n';
    for (auto &g : groups) {
        debug(dlevel) << g.second << '\n';
    }
    debug(dlevel) << "=========" << '\n';
}

void Partitioner::disp_pipeline_graph(int dlevel = debug_level) {
    debug(dlevel) << "\n================" << '\n';
    debug(dlevel) << "Pipeline graph:" << '\n';
    debug(dlevel) << "================" << '\n';
    for (auto &f : children) {
        debug(dlevel) << f.first << ": [";
        for (auto &c : f.second) {
            debug(dlevel) << c << ",";
        }
        debug(dlevel) << "]" << '\n';
    }
    debug(dlevel) << "================" << '\n';
}

void Partitioner::disp_pipeline_bounds(int dlevel = debug_level) {
    debug(dlevel) << "\n================" << '\n';
    debug(dlevel) << "Pipeline bounds:" << '\n';
    debug(dlevel) << "================" << '\n';
    disp_regions(pipeline_bounds, dlevel);
    debug(dlevel) << "===============" << '\n';
}

Cost Partitioner::get_pipeline_cost() {
    internal_assert(group_costs.size() > 0);

    Cost total_cost(0, 0);
    for (const pair<FStage, Group> &g : groups) {
        GroupAnalysis analysis = group_costs.at(g.first);
        total_cost.arith += analysis.cost.arith;
        total_cost.memory += analysis.cost.memory;
    }
    return total_cost;
}

void Partitioner::disp_pipeline_costs(int dlevel = debug_level) {
    internal_assert(group_costs.size() > 0);
    Cost total_cost(0, 0);
    debug(dlevel) << "\n===============" << '\n';
    debug(dlevel) << "Pipeline costs:" << '\n';
    debug(dlevel) << "===============" << '\n';
    debug(dlevel) << "Group:(name) [arith cost, mem cost, parallelism]" << '\n';
    for (const pair<FStage, Group> &g : groups) {
        GroupAnalysis analysis = group_costs.at(g.first);
        total_cost.arith += analysis.cost.arith;
        total_cost.memory += analysis.cost.memory;

        debug(dlevel) << "Group:" << g.first << "[";
        debug(dlevel) << analysis.cost.arith << "," <<
                    analysis.cost.memory << "," << analysis.parallelism << "]\n";
    }
    debug(dlevel) << "Total arithmetic cost:" << total_cost.arith << '\n';
    debug(dlevel) << "Total memory cost:" << total_cost.memory << '\n';
    debug(dlevel) << "===============" << '\n';
}

/* Constructs a partitioner and builds the pipeline graph the grouping
 * algorithm operates on. */
Partitioner::Partitioner(map<string, Box> &_pipeline_bounds,
                         const MachineParams &_arch_params,
                         DependenceAnalysis &_dep_analysis,
                         RegionCosts &_costs,
                         const vector<Function> &_outputs):
    pipeline_bounds(_pipeline_bounds), arch_params(_arch_params),
    dep_analysis(_dep_analysis), costs(_costs), outputs(_outputs) {
    // Place each stage of a function in its own group. Each stage
    // is a node in the pipeline graph.
    for (auto &f : dep_analysis.env) {
        int num_stages = f.second.updates().size() + 1;
        for (int s = 0; s < num_stages; s++) {
            FStage stg(f.second, s);
            Group g(stg, {stg});
            groups.insert(make_pair(stg, g));
        }
    }

    // Find consumers of each function and use it to populate the children map.
    for (auto &f : dep_analysis.env) {
        int num_stages = f.second.updates().size() + 1;
        for (int s = 0; s < num_stages; s++) {

            set<string> parents = get_parents(f.second, s);

            for (const string &c : parents) {
                // Filter out the calls to pipeline inputs. env only contains
                // the functions computed and not the inputs.
                if (c != f.first && dep_analysis.env.find(c) != dep_analysis.env.end()) {
                    // Consumer depends only on the last stage of a producer
                    // with multiple stages.
                    Function prod_func = dep_analysis.env.at(c);
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

void Partitioner::merge_groups(const FusionChoice &choice, const GroupConfig &eval,
                               Partitioner::Level level) {
    Function prod_f = dep_analysis.env.at(choice.prod);
    size_t num_stages = prod_f.updates().size() + 1;

    FStage child = choice.cons;
    Group &child_group = groups.at(child);

    for (size_t s = 0; s < num_stages; s++) {
        FStage cand(prod_f, s);

        internal_assert(groups.find(child) != groups.end());
        Group &cand_group = groups.at(cand);

        vector<FStage> cand_funcs = cand_group.members;

        vector<FStage> &child_group_members = child_group.members;
        child_group_members.insert(child_group_members.end(),
                                   cand_funcs.begin(), cand_funcs.end());

        if (level == Partitioner::INLINE) {
            for (auto &stg : cand_funcs) {
                child_group.inlined.insert(stg.func.name());
            }
        } else {
            for (auto &in : cand_group.inlined) {
                child_group.inlined.insert(in);
            }
        }
    }

    child_group.tile_sizes = eval.tile_sizes;

    // Update group costs.
    // TODO: check if this is necessary or if the analysis from eval can just
    // be reused.
    group_costs[child] = analyze_group(child_group, false);
}

void Partitioner::initialize_groups() {
    for (pair<const FStage, Group> &g : groups) {
        pair<map<string, int>, GroupAnalysis> best = find_best_tile_config(g.second);
        g.second.tile_sizes = best.first;
        group_costs[g.second.output] = best.second;
    }
    fusion_cache.clear();
}

map<string, int64_t> Partitioner::evaluate_reuse(const FStage &stg,
                                                 const set<string> &prods) {
    map<string, int64_t> reuse;
    Function f = stg.func;

    Definition def = get_stage_definition(stg.func, stg.stage_num);

    // TODO: Check if tile sizes of 1 in each dimension gives a reasonable
    // answer or reuse should be evaluated at a much larger granularity or
    // symbolically.  Using a symbolic version might be better if the objective
    // is to prove the dimension has no reuse. The only downside with the
    // symbolic method is it totally at the mercy of the simplifier.  Another
    // option is sampling or using a larger granularity.
    map<string, int> tile_sizes;

    const vector<Dim> &dims = def.schedule().dims();
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        tile_sizes[dims[d].var] = 1;
    }

    DimBounds bounds = get_bounds_from_tile_sizes(stg, tile_sizes);

    vector<map<string, Box>> reuse_regions =
                dep_analysis.overlap_regions(stg.func, stg.stage_num,
                                             bounds, prods, false);

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        int64_t total_reuse = 0;
        disp_regions(reuse_regions[d]);
        for (auto &reg : reuse_regions[d]) {
            int64_t size = box_size(reg.second);
            if (size != unknown) {
                total_reuse += size;
            } else {
                total_reuse = unknown;
                break;
            }
        }
        reuse[dims[d].var] = total_reuse;
    }

    return reuse;
}

/* Picks the best choice among all the fusion options currently available. Uses
 * the cost model to estimate the benefit of each choice. Returns a vector of
 * choice and configuration pairs which describe the best fusion choice.*/
vector<pair<Partitioner::FusionChoice, Partitioner::GroupConfig>>
Partitioner::choose_candidate_fuse(const vector<pair<string, string>> &cands,
                                   Partitioner::Level level) {
    vector<pair<FusionChoice, GroupConfig>> best_choices;
    int64_t best_benefit = 0;
    for (auto &p : cands) {
        // Compute the aggregate benefit for inlining into all the children.
        vector<pair<FusionChoice, GroupConfig>> choices;

        Function prod_f = dep_analysis.env.at(p.first);
        int final_stage = prod_f.updates().size();

        FStage prod(prod_f, final_stage);

        for (const FStage &c : children[prod]) {

            GroupAnalysis tmp;
            GroupConfig best_config(map<string, int>(), tmp);
            FusionChoice cand_choice(prod_f.name(), c);

            // Check if the candidate has been evaluated for fusion before
            if (fusion_cache.find(cand_choice) != fusion_cache.end()) {
                best_config = fusion_cache.at(cand_choice);
            } else {
                best_config = evaluate_choice(cand_choice, level);
                // Cache the result of the evaluation for the pair
                fusion_cache.insert(make_pair(cand_choice, best_config));
            }

            choices.push_back(make_pair(cand_choice, best_config));
        }

        bool no_redundant_work = false;
        int64_t overall_benefit = estimate_benefit(choices, no_redundant_work, true);

        for (auto &choice : choices) {
            debug(debug_level) << "Cand choice:" << choice.first;
        }
        debug(debug_level) << "Cand benefit:" << overall_benefit << '\n';
        // TODO: The grouping process can be non-deterministic when the costs
        // of two choices are equal
        if (best_benefit < overall_benefit) {
            best_choices = choices;
            best_benefit = overall_benefit;
        }
    }

    for (auto &choice : best_choices) {
        debug(debug_level) << "\nBest choice:" << choice.first;
    }
    if (best_choices.size() > 0) {
        debug(debug_level) << "Best benefit:" << best_benefit << '\n';
    }

    return best_choices;
}

vector<map<string, int>>
Partitioner::generate_tile_configs(const FStage &stg) {
    // TODO: This is a wart due to the cost model not taking vectorization
    // and pre-fetching into account. Ensuring the inner most dimension has
    // atleast size 64 gives enough values for vectorization and can help
    // with prefetching. This also interacts with the number of parallel tasks
    // that are generated.
    int min_inner_dim_size = 64;

    Definition def = get_stage_definition(stg.func, stg.stage_num);
    const vector<Dim> &dims = def.schedule().dims();

    set<string> pure_vars;
    for (const string &arg : stg.func.args()) {
        pure_vars.insert(arg);
    }

    // Get the dimensions that are going to be tiled in this stage.
    // skipping rvars for now.
    vector<string> tile_vars;
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        if (pure_vars.find(dims[d].var) != pure_vars.end()) {
            tile_vars.push_back(dims[d].var);
        }
    }

    vector<int> size_variants = {1, 4, 8, 16, 32, 64, 128, 256};
    vector<map<string, int>> tile_configs;

    // Skewed tile configurations
    for (size_t i = 0; i < tile_vars.size(); i++) {
        for (auto &dim_size : size_variants) {
            map<string, int> tiling;
            for (size_t j = 0; j < tile_vars.size(); j++) {
                if (j == i) {
                    tiling[tile_vars[j]] = (j == 0) ?
                                std::max(dim_size, min_inner_dim_size): dim_size;
                } else if (j < i) {
                    tiling[tile_vars[j]] =
                            size_variants[size_variants.size() - 1];
                } else {
                    tiling[tile_vars[j]] = size_variants[0];
                }
            }
            tile_configs.push_back(tiling);
        }
    }

    // Almost square tile configurations
    for (auto &dim_size : size_variants) {
        map<string, int> tiling;
        for (size_t j = 0; j < tile_vars.size(); j++) {
            tiling[tile_vars[j]] = j == 0 ?
                            std::max(dim_size, min_inner_dim_size): dim_size;
        }
        tile_configs.push_back(tiling);
    }

    // Reorder tile configurations
    for (int i = 0; i < (1 << (tile_vars.size())); i++) {
        map<string, int> tiling;
        for (size_t j = 0; j < tile_vars.size(); j++) {
            if (((i >> (j)) & 1) == 1) {
                if (j == 0) {
                    tiling[tile_vars[j]] = min_inner_dim_size;
                } else {
                    tiling[tile_vars[j]] = 1;
                }
            }
        }
        tile_configs.push_back(tiling);
    }

    return tile_configs;
}

/* Finds the best tiling configuration among a set of tile configurations and
 * returns the configuration with the highest estimated benefit. */
pair<map<string, int>, Partitioner::GroupAnalysis>
Partitioner::find_best_tile_config(const Group &g) {
    // Initialize to no tiling
    map<string, int> no_tile_config;
    Group no_tile = g;
    no_tile.tile_sizes = no_tile_config;

    bool show_analysis = false;
    GroupAnalysis best_analysis = analyze_group(no_tile, show_analysis);

    GroupAnalysis no_tile_analysis = analyze_group(no_tile, show_analysis);
    map<string, int> best_config = no_tile_config;

    if (best_analysis.cost.arith == unknown) {
        return make_pair(best_config, best_analysis);
    }

    // Generate tiling configurations
    vector<map<string, int>> configs = generate_tile_configs(g.output);

    Group best_group = g;
    for (auto &config : configs) {
        Group new_group = g;
        new_group.tile_sizes = config;

        GroupAnalysis new_analysis = analyze_group(new_group, show_analysis);

        bool no_redundant_work = false;
        int64_t benefit = estimate_benefit(best_analysis, new_analysis,
                                           no_redundant_work, true);

        if (show_analysis) {
            debug(debug_level) << "Benefit relative to not tiling:" << benefit << '\n';
            debug(debug_level) << "Best analysis:" << new_analysis;
            debug(debug_level) << "No tile analysis:" << no_tile_analysis;
            debug(debug_level) << "arith cost:" <<
                (float)new_analysis.cost.arith / no_tile_analysis.cost.arith << "," <<
                "mem cost:" <<
                (float)new_analysis.cost.memory / no_tile_analysis.cost.memory << '\n';
        }

        if (benefit > 0) {
            best_config = config;
            best_analysis = new_analysis;
            best_group = new_group;
        }
    }

    debug(debug_level) << "\nBest grouping:\n" << best_group << '\n';

    return make_pair(best_config, best_analysis);
}

// Partition the pipeline by iteratively merging groups until a fixpoint.
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

            // All the stages of a function are computed at a single location.
            // The last stage of the pipeline represents the candidate choice
            // of fusing the funtion into a consumer.

            const Function &prod_f = dep_analysis.env.at(g.first.func.name());
            bool is_final_stage = (g.first.stage_num == prod_f.updates().size());

            if (is_output || !is_final_stage) {
                continue;
            }

            if (children.find(g.first) != children.end()) {
                // All the stages beloning to a function are considered to be a
                // single child.
                set<string> child_groups;
                for (const FStage &s : children[g.first]) {
                    child_groups.insert(s.func.name());
                }

                int num_children = child_groups.size();
                // Only groups with a single child are considered for fusion
                // when grouping for computing in tiles. The scheduling model
                // does not allow functions to be computed at different points.
                if (num_children == 1 && level == Partitioner::FAST_MEM) {
                    string prod_name = prod_f.name();
                    string cons_name = (*child_groups.begin());
                    cand.push_back(make_pair(prod_name, cons_name));
                } else if(level == Partitioner::INLINE && prod_f.is_pure()) {
                    string prod_name = prod_f.name();
                    cand.push_back(make_pair(prod_name, ""));
                }
            }
        }

        debug(debug_level) << "\n============================" << '\n';
        debug(debug_level) << "Current grouping candidates:" << '\n';
        debug(debug_level) << "============================" << '\n';
        for (auto &p : cand) {
            debug(debug_level) << "[" << p.first << "," << p.second << "]" << '\n';
        }

        vector<pair<FusionChoice, GroupConfig>> best;
        best = choose_candidate_fuse(cand, level);

        if (!(best.size() > 0)) {
            continue;
        } else {
            fixpoint = false;
        }

        // The following code makes the assumption that all the stages of a function
        // will be in the same group. choose_candidate_ensures that the fusion choice
        // it returns adheres to this constraint.
        string prod = best[0].first.prod;

        Function prod_f = dep_analysis.env.at(prod);
        size_t num_stages = prod_f.updates().size() + 1;

        FStage final_stage(prod_f, num_stages - 1);
        set<FStage> prod_group_children = children[final_stage];

        // Invalidate entries of the fusion cache
        set<FusionChoice> invalid_keys;
        for (auto &c : prod_group_children) {
            for (auto &entry : fusion_cache) {
                if (entry.first.prod == c.func.name() || entry.first.cons == c) {
                    invalid_keys.insert(entry.first);
                }
            }
        }

        for (auto &key : invalid_keys) {
            fusion_cache.erase(key);
        }

        for (auto &fuse : best) {
            internal_assert(fuse.first.prod == prod);
            merge_groups(fuse.first, fuse.second, level);
        }

        for (size_t s = 0; s < num_stages; s++) {
            FStage prod_group(prod_f, s);
            groups.erase(prod_group);
            group_costs.erase(prod_group);

            // Update the children mapping
            children.erase(prod_group);
            for (auto &f : children) {
                set<FStage> &cons = f.second;
                if (cons.find(prod_group) != cons.end()) {
                    cons.erase(prod_group);
                    // For a function with multiple stages all the stages will
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

        disp_pipeline_costs();

        internal_assert((pre_merge.arith + pre_merge.memory) >=
                        (post_merge.arith + post_merge.memory));
    }
}

DimBounds Partitioner::get_bounds(const FStage &s) {
    Definition def = get_stage_definition(s.func, s.stage_num);
    DimBounds bounds;

    const vector<string> &args = s.func.args();
    for (size_t d = 0; d < args.size(); d++) {
        bounds[args[d]] = pipeline_bounds.at(s.func.name())[d];
    }

    return get_stage_bounds(s.func, s.stage_num, bounds);
}

DimBounds
Partitioner::get_bounds_from_tile_sizes(const FStage &s,
                                        const map<string, int> &tile_sizes) {
    Definition def = get_stage_definition(s.func, s.stage_num);
    map<string, Interval> bounds;

    const map<string, Interval> &def_bounds = get_bounds(s);
    const vector<Dim> &dims = def.schedule().dims();

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        string var = dims[d].var;
        const Interval &bound = def_bounds.at(var);
        if (tile_sizes.find(var) != tile_sizes.end()) {
            int size = tile_sizes.at(var);
            // Check if the bounds allow for tiling with the given tile size
            // i.e., ensure atleast 2 tiles
            int64_t extent = get_extent(bound);
            if (extent >= 2 * size) {
                // TODO: Maybe shift this to the center of the pipeline bound
                bounds[var] = Interval(0, size - 1);
            }
            else {
                // If the dimension is too small do not tile it and set the
                // extent of the bounds to that of the dimension estimate
                bounds[var] = bound;
            }
        } else {
            bounds[var] = bound;
        }
    }

    return bounds;
}

Partitioner::GroupAnalysis
Partitioner::analyze_group(const Group &g, bool show_analysis) {
    // Get the definition corresponding to the group output
    Definition def = get_stage_definition(g.output.func, g.output.stage_num);

    set<string> group_inputs;
    set<string> group_mem;

    for (auto &stg : g.members) {
        group_mem.insert(stg.func.name());
        set<string> parents = get_parents(stg.func, stg.stage_num);
        for (auto &c : parents) {
            bool is_member = false;
            for (auto &m : g.members) {
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
    uint64_t estimate_tiles = 1;
    uint64_t parallelism = 1;
    uint64_t num_ele_per_tile = 1;

    const vector<Dim> &dims = def.schedule().dims();

    DimBounds stg_bounds = get_bounds(g.output);

    GroupAnalysis g_analysis;
    g_analysis.cost = Cost(unknown, unknown);
    g_analysis.parallelism = unknown;

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        string var = dims[d].var;
        if (g.tile_sizes.find(var) != g.tile_sizes.end()) {
            int size = g.tile_sizes.at(var);
            int64_t extent = get_extent(stg_bounds.at(var));

            if (extent == unknown) {
                return g_analysis;
            }

            estimate_tiles *= std::ceil((float)extent / size);
            num_ele_per_tile *= size;
            if (can_parallelize_rvar(var, g.output.func.name(), def)) {
                parallelism *= std::ceil((float)extent / size);
            }
        }
    }

    // Get the regions of the pipeline required to compute a tile of the group
    DimBounds tile_bounds = get_bounds_from_tile_sizes(g.output, g.tile_sizes);

    map<string, Box> alloc_reg =
            dep_analysis.regions_required(g.output.func, g.output.stage_num,
                                          tile_bounds, group_mem, false);

    map<string, Box> compute_reg =
            dep_analysis.regions_required(g.output.func, g.output.stage_num,
                                          tile_bounds, group_mem, true);

    map<string, Box> group_reg, prod_reg, input_reg;

    // Separating into regions that belong to the group and regions that are
    // input to the group
    for (auto &reg : compute_reg) {
        if (group_mem.find(reg.first) != group_mem.end() &&
            reg.first != g.output.func.name()) {
            group_reg[reg.first] = reg.second;
        } else if (group_inputs.find(reg.first) != group_inputs.end()) {
            if (dep_analysis.env.find(reg.first) != dep_analysis.env.end()) {
                prod_reg[reg.first] = reg.second;
            } else {
                input_reg[reg.first] = reg.second;
            }
        }
    }

    // TODO: remove debug code.
    if (show_analysis) {
        debug(0) << "==============\n";
        debug(0) << "Group Analysis\n";
        debug(0) << "==============\n";
        debug(0) << g;
        debug(0) << "\nProd reg:" << '\n';
        disp_regions(prod_reg, 0);
        debug(0) << "Input reg:" << '\n';
        disp_regions(input_reg, 0);
        debug(0) << "Group reg:" << '\n';
        disp_regions(group_reg);
    }

    // Aggregate costs for intermediate functions in a tile and the
    // tile output
    Cost tile_cost = costs.region_cost(group_reg, g.inlined);

    Cost out_cost = costs.stage_region_cost(g.output.func.name(),
                                            g.output.stage_num,
                                            tile_bounds, g.inlined);
    bool bounds_defined = true;
    for (auto &reg : alloc_reg) {
        if (box_size(reg.second) == unknown) {
            bounds_defined = false;
        }
    }

    if (tile_cost.arith == unknown || tile_cost.memory == unknown ||
        out_cost.arith == unknown || out_cost.memory == unknown ||
        !bounds_defined) {
        return g_analysis;
    }

    Cost group_cost(tile_cost.arith + out_cost.arith,
                    tile_cost.memory + out_cost.memory);

    // Detailed load costs for all the group intermediates
    map<string, int64_t> group_load_costs =
            costs.detailed_load_costs(group_reg, g.inlined);

    map<string, int64_t> out_load_costs =
            costs.stage_detailed_load_costs(g.output.func.name(),
                                            g.output.stage_num,
                                            tile_bounds, g.inlined);

    combine_load_costs(group_load_costs, out_load_costs);

    Box out_tile_extent;
    if (g.output.stage_num == 0) {
        const vector<string> &args = g.output.func.args();
        for (size_t d = 0; d < args.size(); d++ ) {
            if (tile_bounds.find(args[d]) != tile_bounds.end()) {
                out_tile_extent.push_back(tile_bounds[args[d]]);
            } else {
                out_tile_extent.push_back(Interval());
            }
        }
    }

    Cost per_tile_cost(group_cost.arith, 0);

    // TODO: Add comments on the cost model
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

    // TODO: Use smooth step curve from Jon

    // Intermediates of inlined pure fuctions should not show up in
    // the group_load_costs.
    // TODO: add an assert that checks for that

    bool model_reuse = false;

    // Linear dropoff
    float load_slope = (float)arch_params.balance / arch_params.last_level_cache_size;
    for (auto &f_load : group_load_costs) {
        int64_t footprint = 0;
        if (group_mem.find(f_load.first) != group_mem.end() &&
            f_load.first != g.output.func.name()) {
            footprint = costs.region_size(f_load.first, alloc_reg[f_load.first]);
        } else {
            int64_t initial_footprint = 0;
            if (dep_analysis.env.find(f_load.first) != dep_analysis.env.end()) {
                // Initial loads
                initial_footprint =
                        costs.region_size(f_load.first,
                                          pipeline_bounds.at(f_load.first));
                // Subsequent loads
                footprint = costs.region_size(f_load.first,
                                              alloc_reg.at(f_load.first));
            } else {
                // Initial loads
                initial_footprint =
                        costs.input_region_size(f_load.first,
                                                pipeline_bounds.at(f_load.first));
                // Subsequent loads
                if (f_load.first == g.output.func.name()) {
                    footprint = costs.input_region_size(f_load.first,
                                                        out_tile_extent);
                } else {
                    footprint = costs.input_region_size(f_load.first,
                                                        alloc_reg.at(f_load.first));
                }
            }

            if (model_reuse) {
                int64_t initial_factor =
                        std::trunc(std::min(1 + initial_footprint * load_slope,
                                            (float)arch_params.balance));

                per_tile_cost.memory += initial_factor * footprint;
            } else {
                footprint = initial_footprint;
            }

            if (footprint == unknown) {
                return g_analysis;
            }
        }

        int cost_factor = std::trunc(std::min(1 + footprint * load_slope,
                                     (float)arch_params.balance));
        per_tile_cost.memory += cost_factor * f_load.second;
    }

    if (show_analysis) {
        debug(debug_level) << "\nDetailed loads:\n";
        for (auto &f_load : group_load_costs) {
            debug(debug_level) << "(" << f_load.first << "," << f_load.second << ")";
        }
        debug(debug_level) << '\n';

        debug(debug_level) << "\nPer tile memory cost:" << per_tile_cost.memory << '\n';
        debug(debug_level) << "Per tile arith cost:" << per_tile_cost.arith << '\n';
    }

    g_analysis.cost.memory = per_tile_cost.memory * estimate_tiles;
    g_analysis.cost.arith = per_tile_cost.arith * estimate_tiles;
    g_analysis.parallelism = parallelism;

    internal_assert(per_tile_cost.memory > 0);

    return g_analysis;
}

Partitioner::Group Partitioner::fuse_groups(const Group &prod_group,
                                            const Group &cons_group) {
    vector<FStage> fused_members;
    for (auto &s : prod_group.members) {
        fused_members.push_back(s);
    }
    for (auto &s : cons_group.members) {
        fused_members.push_back(s);
    }

    Group fused_group(cons_group.output, fused_members);

    for (auto &f : prod_group.inlined) {
        fused_group.inlined.insert(f);
    }
    for (auto &f : cons_group.inlined) {
        fused_group.inlined.insert(f);
    }

    return fused_group;
}

Partitioner::GroupConfig
Partitioner::evaluate_choice(const FusionChoice &choice,
                             Partitioner::Level level) {
    // Create a group that reflects the fusion choice and evaluate the cost
    // of the group.
    Function prod_f = dep_analysis.env.at(choice.prod);
    int num_prod_stages = prod_f.updates().size() + 1;
    vector<Group> prod_groups;

    for (int s = 0; s < num_prod_stages; s++) {
        FStage prod_s(prod_f, s);
        prod_groups.push_back(groups.at(prod_s));
    }

    Group cons = groups.at(choice.cons);
    Group fused = cons;
    for (auto &prod_g : prod_groups) {
        fused = fuse_groups(prod_g, fused);
    }

    GroupAnalysis fused_analysis;
    map<string, int> best_tile_config;

    if (level == Partitioner::INLINE) {
        // Set the tile sizes to one along all dimensions of the consumer group
        map<string, int> tile_sizes;

        const Function &cons_f = cons.output.func;
        Definition def = get_stage_definition(cons_f, cons.output.stage_num);

        const vector<Dim> &dims = def.schedule().dims();
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            tile_sizes[dims[d].var] = 1;
        }

        fused.tile_sizes = tile_sizes;

        for (auto &prod_g : prod_groups) {
            for (const FStage &s : prod_g.members) {
                fused.inlined.insert(s.func.name());
            }
        }

        for (const string &f : cons.inlined) {
            fused.inlined.insert(f);
        }

        fused_analysis = analyze_group(fused, false);
        best_tile_config = tile_sizes;

    } else {
        pair<map<string, int>, GroupAnalysis> config = find_best_tile_config(fused);
        best_tile_config = config.first;
        fused_analysis = config.second;
    }

    return GroupConfig(best_tile_config, fused_analysis);
}

int64_t Partitioner::estimate_benefit(const GroupAnalysis &nofuse,
                                      const GroupAnalysis &fuse,
                                      bool no_redundant_work,
                                      bool ensure_parallelism) {
    if (ensure_parallelism &&
        fuse.parallelism < arch_params.parallelism) {
        return unknown;
    }

    int64_t arith_benefit = 0;
    if (nofuse.cost.arith != unknown && fuse.cost.arith != unknown) {
        arith_benefit = nofuse.cost.arith - fuse.cost.arith;
    } else {
        return unknown;
    }

    if (no_redundant_work && arith_benefit < 0) {
        return arith_benefit;
    }

    int64_t mem_benefit = 0;
    if (nofuse.cost.memory != unknown && fuse.cost.memory != unknown) {
        mem_benefit = nofuse.cost.memory - fuse.cost.memory;
    } else {
        return unknown;
    }

    return mem_benefit + arith_benefit;
}

int64_t Partitioner::estimate_benefit(
        const vector<pair<FusionChoice, GroupConfig>> &choices,
        bool no_redundant_work, bool ensure_parallelism) {
    GroupAnalysis fused_analysis;
    fused_analysis.cost = Cost(0, 0);
    fused_analysis.parallelism = std::numeric_limits<int64_t>::max();

    set<FStage> no_fuse_groups;

    for (auto &choice : choices) {
        Function prod_f = dep_analysis.env.at(choice.first.prod);
        int num_prod_stages = prod_f.updates().size() + 1;
        for (int s = 0; s < num_prod_stages; s++) {
            FStage prod_s(prod_f, s);
            no_fuse_groups.insert(prod_s);
        }

        no_fuse_groups.insert(choice.first.cons);

        GroupAnalysis analysisg = choice.second.analysis;
        if (analysisg.cost.arith != unknown) {
            fused_analysis.cost.arith += analysisg.cost.arith;
            fused_analysis.cost.memory += analysisg.cost.memory;
            fused_analysis.parallelism = std::min(fused_analysis.parallelism,
                                              analysisg.parallelism);
        } else {
            fused_analysis.cost = Cost(unknown, unknown);
            fused_analysis.parallelism = unknown;
            break;
        }
    }

    GroupAnalysis no_fuse_analysis;
    no_fuse_analysis.cost.arith = 0;
    no_fuse_analysis.cost.memory = 0;
    no_fuse_analysis.parallelism = std::numeric_limits<int64_t>::max();

    for (auto &g : no_fuse_groups) {
        internal_assert(group_costs.find(g) != group_costs.end());
        GroupAnalysis analysisg = group_costs.at(g);
        if (analysisg.cost.arith != unknown) {
            no_fuse_analysis.cost.arith += analysisg.cost.arith;
            no_fuse_analysis.cost.memory += analysisg.cost.memory;
            no_fuse_analysis.parallelism = std::min(no_fuse_analysis.parallelism,
                                                    analysisg.parallelism);
        } else {
            no_fuse_analysis.cost = Cost(unknown, unknown);
            no_fuse_analysis.parallelism = unknown;
            break;
        }
    }

    return estimate_benefit(no_fuse_analysis, fused_analysis,
                            no_redundant_work, ensure_parallelism);
}

map<string, int64_t> Partitioner::bounds_to_estimates(const DimBounds &bounds) {
    map<string, int64_t> estimates;
    for (auto &bound : bounds) {
        int64_t estimate = get_extent(bound.second);
        estimates[bound.first] = estimate;
    }
    return estimates;
}

map<FStage, map<string, Box>> Partitioner::group_storage_bounds() {
    map<FStage, map<string, Box>> group_storage_bounds;
    for (const pair<const FStage, Group> &gpair : groups) {
        Group g = gpair.second;
        DimBounds bounds = get_bounds_from_tile_sizes(g.output, g.tile_sizes);

        set<string> prods;
        for (const FStage &s : g.members) {
            prods.insert(s.func.name());
        }

        map<string, Box> reg_alloc =
                dep_analysis.regions_required(g.output.func, g.output.stage_num,
                                           bounds, prods, false);
        map<string, Box> group_alloc;
        for (const FStage &s : g.members) {
            if (reg_alloc.find(s.func.name()) != reg_alloc.end()
                && s.func.name() != g.output.func.name()) {
                group_alloc[s.func.name()] = reg_alloc[s.func.name()];
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
                                           bounds, prods, true);

        for (const FStage &s : g.members) {
            if (reg_computed.find(s.func.name()) != reg_computed.end()) {
                map<string, int> tile_sizes;
                const vector<string> &args = s.func.args();
                for (size_t arg = 0; arg < args.size(); arg++) {
                    tile_sizes[args[arg]] =
                            get_extent(reg_computed[s.func.name()][arg]);
                }
                mem_bounds[s] = get_bounds_from_tile_sizes(s, tile_sizes);
            }
        }

        group_bounds[gpair.first] = mem_bounds;
    }

    return group_bounds;
}

string get_base_name(string name) {
    size_t dot_pos = name.rfind('.');
    if (dot_pos != string::npos) {
        return name.substr(dot_pos + 1);
    }
    return name;
}

pair<VarOrRVar, VarOrRVar>
split_dim(Stage f_handle, VarOrRVar v, int factor, string in_suffix,
          string out_suffix, map<string, int64_t> &estimates, string &sched) {
    // Create new variables for the split dimensions
    string arg_name = v.name();
    string inner_name = arg_name + in_suffix;
    string outer_name = arg_name + out_suffix;
    VarOrRVar inner(inner_name), outer(outer_name);

    sched += "Var " + inner_name + "(\"" + outer_name + "\")" + ";\n";
    sched += "Var " + outer_name + "(\"" + outer_name + "\")" + ";\n";

    f_handle.split(v, outer, inner, factor);

    sched += f_handle.name() + ".split(" + arg_name + ',' +
             outer_name + ',' + inner_name + ',' + std::to_string(factor) + ");\n";

    internal_assert(estimates.find(arg_name) != estimates.end() &&
                    estimates[arg_name] != unknown);

    estimates[inner_name] = factor;
    estimates[outer_name] = std::ceil((float)estimates.at(arg_name) / factor);
    estimates.erase(arg_name);

    return make_pair(inner, outer);
}

void vectorize_stage(Stage f_handle, Definition def, Function func,
                     const Target &t, set<string> &rvars,
                     map<string, int64_t> &estimates, string &sched) {
    const vector<Dim> &dims = f_handle.get_schedule().dims();
    int vec_dim_index = -1;

    // Set the vector length as the maximum of the natural vector size of all
    // the values produced by the function.
    int vec_len = 0;
    for (auto &type : func.output_types()) {
        vec_len = std::max(vec_len, t.natural_vector_size(type));
    }

    for (int d = 0; d < (int) dims.size() - 1; d++) {
        string dim_name = get_base_name(dims[d].var);
        bool can_vectorize = true;
        if (rvars.find(dim_name) != rvars.end()) {
            can_vectorize = can_parallelize_rvar(dim_name, func.name(), def);
        }
        if (estimates.find(dim_name) != estimates.end() &&
            estimates[dim_name] != unknown) {
            if (can_vectorize && estimates[dim_name] >= vec_len) {
                vec_dim_index = d;
                break;
            }
        }
    }

    if (vec_dim_index >= 0) {
        string vec_dim_name = get_base_name(dims[vec_dim_index].var);
        Var vec_var(vec_dim_name);

        bool is_rvar = (rvars.find(vec_dim_name) != rvars.end());

        pair<VarOrRVar, VarOrRVar> split_vars =
                split_dim(f_handle, vec_var, vec_len, "_vi", "_vo", estimates, sched);

        f_handle.vectorize(split_vars.first);
        sched += f_handle.name() + ".vectorize(" + split_vars.first.name() + ");\n";

        if (is_rvar) {
            rvars.erase(vec_dim_name);
            rvars.insert(split_vars.first.name());
            rvars.insert(split_vars.second.name());
        }

        // TODO: Reorder vector dim to the inner most if it is the inner
        // most storage dimension of the func.
        //
        // TODO: Check if the warning is necessary.
        if (vec_dim_index > 0) {
            user_warning << "Outer dim vectorization of var " << vec_dim_name
                         << " in function " << f_handle.name() << '\n';
        }
    }
}

/* Reorder the dimensions to preserve spatial locality. This function 
 * checks the stride of the access for each access. The dimensions of
 * the loop are reordered such that the dimension with the smallest
 * access strides is innermost. Takes the strides along each dimension
 * as input.*/
void reorder_dims(Stage f_handle, Definition def,
                  map<string, int64_t> strides, string &sched) {
    vector<Dim> &dims = def.schedule().dims();
    vector<pair<string, bool>> order;

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        internal_assert(strides.find(dims[d].var) != strides.end());
    }

    // Iterate until all the dimensions have been assigned an order
    while (strides.size() > 0) {
        // Find the pure dimension with smallest stride
        int64_t min_pure_stride = std::numeric_limits<int64_t>::max();
        string min_pure_var;
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            string var_name = get_base_name(dims[d].var);
            if (strides.find(var_name) != strides.end() &&
                dims[d].is_pure()) {
                int64_t dim_stride = strides[var_name];
                if (dim_stride < min_pure_stride) {
                    min_pure_stride = dim_stride;
                    min_pure_var = var_name;
                }
            }
        }

        // Check if the stride of the pure dimension is smaller than
        // the first reduction dimension that has not been assigned 
        // an order yet.
        int64_t min_impure_stride = std::numeric_limits<int64_t>::max();
        string min_impure_var;
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            string var_name = get_base_name(dims[d].var);
            if (strides.find(var_name) != strides.end() &&
                !dims[d].is_pure()) {
                int64_t dim_stride = strides[var_name];
                if (dim_stride < min_impure_stride) {
                    min_impure_stride = dim_stride;
                    min_impure_var = var_name;
                }
                // Reduction dimensions cannot be reordered relative to
                // each other. Stop after encountering the first reduction 
                // dimension.
                break;
            }
        }

        pair<string, bool> curr_min_var;
        if (min_impure_stride < min_pure_stride) {
            curr_min_var.first = min_impure_var;
            curr_min_var.second = false;
        } else {
            curr_min_var.first = min_pure_var;
            curr_min_var.second = true;
        }

        order.push_back(curr_min_var);
        strides.erase(curr_min_var.first);
    }

    // TODO: Remove debug code.
    /*
    debug(0) << "Var order for stage:" << f_handle.name() << '\n';
    for (auto &o : order) {
        debug(0) << o.first << ',';
    }
    debug(0) << '\n';
    */

    vector<VarOrRVar> ordering;
    for (auto &o : order) {
        VarOrRVar o_var(o.first, o.second);
        ordering.push_back(o_var);
    }

    string var_order = ordering[0].name();
    for (size_t o = 1; o < ordering.size(); o++) {
        var_order += ',' + ordering[o].name();
    }

    f_handle.reorder(ordering);
    sched += f_handle.name() + ".reorder(" + var_order + ");\n";
}

string Partitioner::generate_group_cpu_schedule(
                    const Group &g, const Target &t,
                    const map<FStage, DimBounds> &group_loop_bounds,
                    const map<string, Box> &group_storage_bounds,
                    const set<string> &inlines) {
    string sched = "";
    string out_f_name = g.output.func.name();
    Function g_out = g.output.func;

    debug(debug_level) << "\n================\n";
    debug(debug_level) << "Scheduling group:\n";
    debug(debug_level) << "=================\n";
    debug(debug_level) << g;

    // Get the definition corresponding to the stage
    Definition def = get_stage_definition(g_out, g.output.stage_num);

    // Get the estimates for stage bounds
    DimBounds stg_bounds = get_bounds(g.output);
    map<string, int64_t> stg_estimates = bounds_to_estimates(stg_bounds);

    Stage f_handle = Stage(Func(g_out));

    // Get a function handle for scheduling the stage
    if (g.output.stage_num > 0) {
        int stage_num = g.output.stage_num;
        f_handle = Func(g_out).update(stage_num - 1);
    } else {
        Func(g_out).compute_root();
        sched += f_handle.name() + ".compute_root()" + ";\n";
    }

    string var_prefix = g_out.name() + "_" +
                        std::to_string(g.output.stage_num);


    if (g.output.func.has_extern_definition()) {
        internal_assert(g.members.size() == 1);
        return sched;
    }

    // Realize tiling and update the dimension estimates
    vector<VarOrRVar> outer_dims;
    vector<VarOrRVar> inner_dims;

    vector<Dim> &dims = def.schedule().dims();

    // Keep track of the rvars
    set<string> rvars;
    for (int d = 0; d < (int)dims.size() - 1; d++) {
        bool is_pure_var = false;
        for (auto &arg : g_out.args()) {
            if (arg == get_base_name(dims[d].var)) {
                is_pure_var = true;
                break;
            }
        }
        if (!is_pure_var) {
            rvars.insert(get_base_name(dims[d].var));
        }
    }

    // Reorder the dimensions for better spatial locality
    map<string, int64_t> strides =
            analyze_spatial_locality(g.output, group_storage_bounds, inlines);
    reorder_dims(f_handle, def, strides, sched);

    vector<string> dim_vars;
    for (int d = 0; d < (int) dims.size() - 1; d++) {
        dim_vars.push_back(get_base_name(dims[d].var));
    }

    for (auto &var : dim_vars) {
        bool is_rvar = (rvars.find(var) != rvars.end());
        VarOrRVar v(var, is_rvar);

        if (g.tile_sizes.find(var) != g.tile_sizes.end() &&
            stg_estimates.at(var) != unknown &&
            stg_estimates.at(var) > g.tile_sizes.at(var)) {
            int tile_size = g.tile_sizes.at(var);
            if (tile_size > 1) {
                pair<VarOrRVar, VarOrRVar> tile_vars =
                        split_dim(f_handle, v, tile_size, "_i", "_o",
                                  stg_estimates, sched);

                inner_dims.push_back(tile_vars.first);
                outer_dims.push_back(tile_vars.second);

                if (is_rvar) {
                    rvars.erase(var);
                    rvars.insert(tile_vars.first.name());
                    rvars.insert(tile_vars.second.name());
                }
            } else {
                outer_dims.push_back(v);
            }
        } else {
            inner_dims.push_back(v);
        }
    }

    // Reorder the tile dimensions
    if (outer_dims.size() > 0) {

        vector<VarOrRVar> ordering;
        for (auto &v : inner_dims) {
            ordering.push_back(v);
        }
        for (auto &v : outer_dims) {
            ordering.push_back(v);
        }

        string var_order = ordering[0].name();
        for (size_t o = 1; o < ordering.size(); o++) {
            var_order += ',' + ordering[o].name();
        }

        f_handle.reorder(ordering);
        sched += f_handle.name() + ".reorder(" + var_order + ");\n";
    }

    vectorize_stage(f_handle, def, g_out, t, rvars, stg_estimates, sched);

    // Parallelize definition
    uint32_t def_par = 1;
    // TODO: Investigate if it is better to pull one large dimension and
    // parallelize over it or generate nested parallelism.
    //
    // Go from the outer to the inner most loop till sufficient parallelism
    // is achieved.
    bool nested_parallelism = true;
    if (nested_parallelism) {
        int dim_start = dims.size() - 2;
        string seq_var = "";
        for (int d = dim_start; d >= 0; d--) {
            string var = get_base_name(dims[d].var);
            bool is_rvar = (rvars.find(var) != rvars.end());
            VarOrRVar v(var, is_rvar);

            if (is_rvar && !can_parallelize_rvar(var, g_out.name(), def)) {
                if (seq_var == "") {
                    seq_var = var;
                }
                continue;
            }

            if (def_par >= arch_params.parallelism) {
                // Enough parallelism to saturate target machine
                break;
            }

            if (stg_estimates.find(var) != stg_estimates.end() &&
                stg_estimates[var] != unknown) {
                if (seq_var != "") {
                    VarOrRVar seq(seq_var, (rvars.find(seq_var) != rvars.end()));
                    f_handle.reorder(seq, v);
                    sched += f_handle.name() + ".reorder(" + seq_var + "," + var + ");\n";
                }
                f_handle.parallel(v);
                sched += f_handle.name() + ".parallel(" + var + ");\n";
                def_par *= stg_estimates[var];
            } else {
                break;
            }
        }
    }

    if (def_par < arch_params.parallelism) {
        user_warning << "Warning: insuffcient parallelism for " <<
                         f_handle.name() << '\n';
    }

    // Find the level at which group members will be computed.
    int tile_inner_index = dims.size() - outer_dims.size() - 1;
    VarOrRVar tile_inner_var("", false);
    if (outer_dims.size() > 0) {
        string var_name = get_base_name(dims[tile_inner_index].var);
        bool is_rvar = (rvars.find(var_name) != rvars.end());
        tile_inner_var = VarOrRVar(var_name, is_rvar);
    }

    for (const FStage &mem : g.members) {
        // Skip member stages that have been inlined
        if (g.inlined.find(mem.func.name()) != g.inlined.end() ||
            mem.func.name() == g_out.name()) {
            continue;
        }

        // Get the definition corresponding to the stage
        Definition mem_def = get_stage_definition(mem.func, mem.stage_num);

        // Get the estimates for the dimensions of the member stage
        map<string, int64_t> mem_estimates =
                bounds_to_estimates(group_loop_bounds.at(mem));

        set<string> mem_rvars;
        const vector<Dim> &mem_dims = mem_def.schedule().dims();
        for (int d = 0; d < (int)mem_dims.size() - 1; d++) {
            bool is_pure_var = false;
            for (auto &arg : mem.func.args()) {
                if (arg == get_base_name(mem_dims[d].var)) {
                    is_pure_var = true;
                    break;
                }
            }
            if (!is_pure_var) {
                mem_rvars.insert(get_base_name(mem_dims[d].var));
            }
        }

        // Get a function handle for scheduling the stage
        Stage mem_handle = Stage(Func(mem.func));

        if (mem.stage_num > 0) {
            mem_handle = Func(mem.func).update(mem.stage_num - 1);
        } else {
            if (outer_dims.size() > 0) {
                if (tile_inner_var.is_rvar) {
                    Func(mem.func).compute_at(Func(g_out), tile_inner_var.rvar);
                } else {
                    Func(mem.func).compute_at(Func(g_out), tile_inner_var.var);
                }
                sched += mem_handle.name() + ".compute_at(" + g_out.name() +
                        ',' + tile_inner_var.name() + ");\n";
            } else {
                user_warning << "Warning: Degenerate tiling no dimensions are tiled" << '\n';
                user_warning << "Computing " <<  mem.func.name() << " at root" << '\n';
                Func(mem.func).compute_root();
                sched += mem_handle.name() + ".compute_root()";
            }
        }

        // Reorder the dimensions for better spatial locality
        map<string, int64_t> mem_strides =
                analyze_spatial_locality(mem, group_storage_bounds, inlines);
        reorder_dims(mem_handle, mem_def, mem_strides, sched);

        vectorize_stage(mem_handle, mem_def, mem.func, t, mem_rvars,
                        mem_estimates, sched);
    }

    return sched;
}

/** Realizes the scheduling by following the grouping structure. Returns a
 * string representation of the schedule.
 *
 * TODO: A mode where schedules are not applied to the functions might be
 * interesting.
 *
 * TODO: The current form of the schedule returned is not very useful since it
 * cannot be manipulated and introspected very easily. The problem is that all
 * of the scheduling uses internal function and variable names which are not
 * visible to the user. Additionally, functions like sum and maximum are not
 * user visible. More thought needs to go into interaction between the user and
 * auto scheduling. */
string Partitioner::generate_cpu_schedule(const Target &t) {
    string sched = "";

    // Grab the group bounds early as they rely on the dimensions of the group
    // outputs which will be altered by modifying schedules.
    map<FStage, map<FStage, DimBounds>> loop_bounds = group_loop_bounds();
    map<FStage, map<string, Box>> storage_bounds = group_storage_bounds();

    set<string> inlines;
    // Mark all the functions that are Inlined.
    for (const pair<FStage, Group> &g : groups) {
        for (const string &inline_func : g.second.inlined) {
            inlines.insert(inline_func);
            Function f = dep_analysis.env.at(inline_func);
            Func f_handle(f);
            // TODO: Inlining functions with update definitions has different
            // behavior than pure functions. They may need to be computed above
            // the inner most vector loop to avoid complications with varying
            // extents across different vector lanes.
            f_handle.compute_inline();
            sched += f_handle.name() + ".compute_inline()" + ";\n";
        }
    }

    // Realize schedule for each group in the pipeline.
    for (auto &g : groups) {
        sched += generate_group_cpu_schedule(g.second, t, loop_bounds[g.first],
                                             storage_bounds[g.first], inlines);
    }

    return sched;
}

/* Visitor to check if any of the variables is used in an expression. */
struct ExprUsesVars : public IRVisitor {
    set<string> vars;
    bool vars_used;

    using IRVisitor::visit;

    ExprUsesVars(const set<string> &vars): vars(vars) {
        vars_used = false;
    }

    void visit(const Variable * v) {
        if(vars.find(v->name) != vars.end()) {
            vars_used = true;
        }
    }
};

/* Visitor to get all the variables the dependent on a variable. */
struct VarsUsingVar : public IRVisitor {
    set<string> vars;
    bool var_used;

    using IRVisitor::visit;

    VarsUsingVar(string var) {
        vars.insert(var);
        var_used = false;
    }

    void visit(const Let *let) {
        ExprUsesVars check(vars);
        let->value.accept(&check);
        if (check.vars_used) {
            vars.insert(let->name);
        }
        let->value.accept(this);
        let->body.accept(this);
    }
};

/* Returns the maximum stride a loop over var accesses the allocation func_acc.
 * Access expressions along each dimension of the allocation are specified by
 * acc_exprs. The dimensions of the allocation are specified by
 * buffer_bounds.*/
int64_t Partitioner::find_max_access_stride(const set<string> &vars,
                                            string func_acc, vector<Expr> acc_exprs,
                                            const Box &buffer_bounds) {
    size_t num_storage_dims = 0;
    int64_t bytes_per_ele = 0;

    // Get the number of dimensions of the allocated storage and the
    // number of bytes required to store a single value of func_acc.
    if (dep_analysis.env.find(func_acc) != dep_analysis.env.end()) {
        Function f = dep_analysis.env.at(func_acc);
        for (auto &e : f.values()) {
            bytes_per_ele += e.type().bytes();
        }
        num_storage_dims = f.schedule().storage_dims().size();
    } else {
        bytes_per_ele = costs.inputs.at(func_acc).bytes();
        num_storage_dims = buffer_bounds.size();
    }

    int64_t curr_stride = bytes_per_ele;
    int64_t stride = 0;

    for (size_t sdim = 0; sdim < num_storage_dims; sdim++) {
        // Check if the access expression is dependent on the loop variable
        // var. Expressions that do not involve the variable have stride 0.
        ExprUsesVars uses_vars(vars);
        acc_exprs[sdim].accept(&uses_vars);

        if (uses_vars.vars_used) {
           stride = std::max(stride, curr_stride);
        }

        Interval dim_range = buffer_bounds[sdim];
        int64_t dim_extent = get_extent(dim_range);
        curr_stride *= dim_extent;
    }

    return stride;
}

/* Returns the sum of access strides along each of the loop variables of a stage.
 * The bounds of all the allocations accessed is specified in allocation_bounds. */
map<string, int64_t>
Partitioner::analyze_spatial_locality(const FStage &stg,
                                      const map<string, Box> &allocation_bounds,
                                      const set<string> &inlines) {
    internal_assert(!stg.func.has_extern_definition());
    // Handle inlining. When a function is inlined into another the
    // stride of the accesses should be computed on the expression post inlining.
    // For example:
    // f(x, y) = ...;
    // g(x, y) = f(y, x); // transpose
    // h(x, y) = g(y, x); // transpose
    //
    // If both g and f are inlined into h then the resulting expression for h
    // will look like:
    // h(x, y) = f(x, y);
    //
    // Computing the stride of a loop over x in the function h will be incorrect
    // if inlining is not taken into account.

    // Get all the allocations accessed in the definition corresponding to stg.
    FindAllCalls find;
    Definition def = get_stage_definition(stg.func, stg.stage_num);
    // Perform inlining on the all the values and the args in the stage.
    for (size_t v = 0; v < def.values().size(); v++) {
        def.values()[v] = perform_inline(def.values()[v], dep_analysis.env,
                                         inlines);
    }

    for (size_t arg = 0; arg < def.args().size(); arg++) {
        def.args()[arg] = perform_inline(def.args()[arg], dep_analysis.env,
                                         inlines);
    }
    def.accept(&find);

    // Arguments on the left hand side might themselves involve accesses
    // to allocations and they need to be accounted for computing the strides
    // along each dimension.
    vector<pair<string, vector<Expr>>> call_args = find.call_args;
    // Account for the spatial locality of the store. Add the access on the
    // left hand side to call_args.
    vector<Expr> left_arg_exprs;
    for (size_t arg = 0; arg < def.args().size(); arg++) {
        left_arg_exprs.push_back(def.args()[arg]);
    }
    call_args.push_back(make_pair(stg.func.name(), left_arg_exprs));

    // Map for holding the strides across each dimension
    map<string, int64_t> var_strides;
    const vector<Dim> &dims = def.schedule().dims();

    for (size_t d = 0; d < dims.size() - 1; d++) {
        // Get all the variables involving the dimension in the definition.
        VarsUsingVar dep_vars(dims[d].var);
        def.accept(&dep_vars);

        // Accumulate the stride for each access for a loop dimension.
        int total_stride = 0;
        for (const pair<string, vector<Expr>> &call : call_args) {
            Box call_alloc_reg;
            if (allocation_bounds.find(call.first) != allocation_bounds.end()) {
                call_alloc_reg = allocation_bounds.at(call.first);
            } else {
                call_alloc_reg = pipeline_bounds.at(call.first);
            }
            total_stride +=
                    find_max_access_stride(dep_vars.vars, call.first, call.second,
                                           call_alloc_reg);
        }
        var_strides[dims[d].var] = total_stride;
    }

    return var_strides;
}

/* Finds a schedule for all the functions in the pipeline required to compute
 * the outputs. Applies the schedule and returns a string representation of the
 * schedule. The target architecture is specified by target. */
string generate_schedules(const vector<Function> &outputs, const Target &target,
                          const MachineParams &arch_params) {
    string sched;
    // Make an environment map which is used throughout the auto scheduling
    // process.
    map<string, Function> env;
    for (Function f : outputs) {
        map<string, Function> more_funcs = find_transitive_calls(f);
        env.insert(more_funcs.begin(), more_funcs.end());
    }

    // Compute the bounds of function values which are used for dependence
    // analysis.
    vector<string> order = realization_order(outputs, env);
    FuncValueBounds func_val_bounds = compute_function_value_bounds(order, env);

    // The auto scheduling algorithm requires estimates on the outputs of the
    // pipeline to get quantitative estimates of costs for computing functions
    // in the pipeline.
    bool estimates_avail = check_estimates_on_outputs(outputs);

    if (!estimates_avail) {
        user_warning << "Please provide estimates for each dimension " <<
                        "of the pipeline output functions.\n";

        // Computing all the pipeline stages at root and storing them at root.
        set_schedule_defaults(env);
        return sched;
    }

    map<string, vector<string>> update_args;
    set<string> reductions;
    DependenceAnalysis dep_analysis(env, func_val_bounds);

    // Compute bounds of all the functions in the pipeline given estimates
    // on outputs. Also report functions where the bounds could not be inferred.
    map<string, Box> pipeline_bounds = get_pipeline_bounds(dep_analysis, outputs);

    // Initialize the cost model.
    // Compute the expression costs for each function in the pipeline.
    RegionCosts costs(env);
    costs.disp_func_costs();

    Partitioner part(pipeline_bounds, arch_params, dep_analysis,
                     costs, outputs);

    // Compute and display reuse
    /* TODO: Use the reuse estimates to reorder loops
    for (auto &f : env) {
        FindAllCalls find;
        f.second.accept(&find);
        int num_stages = f.second.updates().size() + 1;
        for (int s = 0; s < num_stages; s++) {
            FStage curr_s(f.second, s);
            map<string, int64_t> reuse =
                    part.evaluate_reuse(curr_s, find.funcs_called);
            debug(0) << curr_s << '\n';
            for (auto &dir : reuse) {
                debug(0) << dir.first << " " << dir.second << ',';
            }

            debug(0) << '\n';
        }
    }*/

    // Display the current pipeline graph.
    // TODO: Output the graph in dot format.
    part.disp_pipeline_graph();
    part.disp_pipeline_bounds();

    part.initialize_groups();
    part.disp_pipeline_costs();

    part.group(Partitioner::INLINE);
    part.disp_grouping();

    part.fusion_cache.clear();
    part.group(Partitioner::FAST_MEM);

    part.disp_pipeline_costs();
    part.disp_grouping();
    part.disp_pipeline_graph();

    sched = part.generate_cpu_schedule(target);

    // TODO: Unify both inlining and grouping for fast mem
    // TODO: GPU scheduling
    // TODO: Hierarchical tiling

    return sched;
}

}
}
