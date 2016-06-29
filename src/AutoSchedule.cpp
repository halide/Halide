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

void simplify_box(Box &b) {
    for (size_t i = 0; i < b.size(); i++) {
        b[i].min = simplify(b[i].min);
        b[i].max = simplify(b[i].max);
    }
}

struct FStage {
    Function func;
    uint32_t stage_num;
    FStage(Function _func, uint32_t _stage_num) :
          func(_func), stage_num(_stage_num) {}

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

struct MachineParams {
    uint32_t parallelism;
    uint32_t vec_len;
    uint32_t register_file_size;
    uint32_t last_level_size;
    uint32_t balance;
};

void set_schedule_defaults(map<string, Function> &env) {
    // Changing the default to compute root.
    for (auto &kv : env) {
        kv.second.schedule().store_level() = LoopLevel::root();
        kv.second.schedule().compute_level() = LoopLevel::root();

        // Initializing the schedules for update definitions
        for (size_t u = 0; u < kv.second.updates().size(); u++) {
            kv.second.update_schedule(u).store_level() = LoopLevel::root();
            kv.second.update_schedule(u).compute_level() = LoopLevel::root();
        }
    }
}

bool check_estimates_on_outputs(const vector<Function> &outputs) {
    bool estimates_avail = true;
    for (auto &out: outputs) {
        const vector<Bound> &estimates = out.schedule().estimates();
        if (estimates.size() != out.args().size()) {
            estimates_avail = false;
            break;
        }
        vector<string> vars = out.args();

        for (uint32_t i = 0; i < estimates.size(); i++) {
            if (std::find(vars.begin(), vars.end(),
                          estimates[i].var) == vars.end() ||
                !((estimates[i].min.as<IntImm>()) &&
                  (estimates[i].extent.as<IntImm>()))) {
                estimates_avail = false;
                break;
            }
        }
    }
    return estimates_avail;
}

struct DependenceAnalysis {

    const map<string, Function> &env;
    const FuncValueBounds &func_val_bounds;

    // TODO: Build a cache for bounds queries

    DependenceAnalysis(map<string, Function> &_env,
                       const FuncValueBounds &_func_val_bounds):
                       env(_env), func_val_bounds(_func_val_bounds) {}

    map<string, Box> regions_required(Function f, int stage_num,
                                      const DimBounds &bounds,
                                      const set<string> &prods,
                                      bool values_computed);

    map<string, Box> regions_required(Function f,
                                      const DimBounds &pure_bounds,
                                      const set<string> &prods,
                                      bool values_computed);

    map<string, Box> redundant_regions(Function f, int stage_num, string var,
                                       const DimBounds &bounds,
                                       const set<string> &prods,
                                       bool values_computed);
    vector<map<string, Box>>
    overlap_regions(Function f, int stage_num, const DimBounds &bounds,
                    const set<string> &prods, bool values_computed);
};

vector<map<string, Box>>
DependenceAnalysis::overlap_regions(Function f, int stage_num,
                                    const DimBounds &bounds,
                                    const set<string> &prods,
                                    bool values_computed) {

    vector< map<string, Box> > conc_overlaps;

    Definition def = get_stage_definition(f, stage_num);
    const vector<Dim> &dims = def.schedule().dims();

    for (int d = 0; d < (int)dims.size(); d++) {
        map<string, Box> conc_reg =
                redundant_regions(f, stage_num, dims[d].var,
                                  bounds, prods, values_computed);
        conc_overlaps.push_back(conc_reg);
    }
    return conc_overlaps;
}

map<string, Box>
DependenceAnalysis::regions_required(Function f,
                                     const DimBounds &pure_bounds,
                                     const set<string> &prods,
                                     bool values_computed) {
    map<string, Box> regions;
    int num_stages = f.updates().size() + 1;
    for (int s = 0; s < num_stages; s++) {

        DimBounds bounds = get_stage_bounds(f, s, pure_bounds);
        map<string, Box> stage_regions =
                regions_required(f, s, bounds, prods, values_computed);

        for (auto& reg: stage_regions) {
            // Merge region with an existing region for the function
            if (regions.find(reg.first) == regions.end())
                regions[reg.first] = reg.second;
            else
                merge_boxes(regions[reg.first], reg.second);
        }
    }
    return regions;
}

map<string, Box>
DependenceAnalysis::regions_required(Function f, int stage_num,
                                     const DimBounds &bounds,
                                     const set<string> &prods,
                                     bool values_computed) {

    map<string, Box> regions;
    // Add the query function and its region to the queue
    deque<pair<FStage, DimBounds>> f_queue;
    FStage start(f, stage_num);
    f_queue.push_back(make_pair(start, bounds));

    // Recursively compute the regions required
    while(!f_queue.empty()) {
        FStage s = f_queue.front().first;
        DimBounds curr_bounds = f_queue.front().second;

        Definition def = get_stage_definition(s.func, s.stage_num);
        Scope<Interval> curr_scope;

        const vector<Dim> &dims = def.schedule().dims();
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            string var_name = dims[d].var;
            internal_assert(curr_bounds.find(var_name) != curr_bounds.end());

            Interval simple_bounds =
                    Interval(simplify(curr_bounds.at(dims[d].var).min),
                             simplify(curr_bounds.at(dims[d].var).max));
            curr_scope.push(var_name, simple_bounds);
        }

        for (auto &val: def.values()) {

            map<string, Box> curr_regions =
                    boxes_required(val, curr_scope, func_val_bounds);

            Box left_reg;
            for (const Expr &arg: def.args()) {
                map<string, Box> arg_regions =
                        boxes_required(arg, curr_scope, func_val_bounds);

                // Merge the regions with the regions found while looking at
                // the values
                for (auto& reg: arg_regions) {
                    if (curr_regions.find(reg.first) == curr_regions.end())
                        curr_regions[reg.first] = reg.second;
                    else
                        merge_boxes(curr_regions[reg.first], reg.second);
                }

                Interval arg_bounds =
                        bounds_of_expr_in_scope(arg, curr_scope, func_val_bounds);
                left_reg.push_back(arg_bounds);
            }

            if (curr_regions.find(s.func.name()) == curr_regions.end()) {
                curr_regions[s.func.name()] = left_reg;
            } else {
                merge_boxes(curr_regions[s.func.name()], left_reg);
            }

            for (auto &reg: curr_regions) {
                // Merge region with an existing region for the function in the
                // global map. Do not merge the parent funtion itself to the region
                // when querying only for the values computed.
                if (!values_computed ||
                    (values_computed && reg.first != s.func.name())) {
                    if (regions.find(reg.first) == regions.end())
                        regions[reg.first] = reg.second;
                    else
                        merge_boxes(regions[reg.first], reg.second);
                }

                // Skip adding to the queue if not the set of producers
                if (prods.find(reg.first) == prods.end())
                    continue;

                if (env.find(reg.first) != env.end() &&
                    reg.first != s.func.name()) {
                    // Add all the stages of the function representing the
                    // region into the queue

                    Function prod_func = env.at(reg.first);
                    DimBounds prod_pure_bounds;
                    const vector<string>& args = prod_func.args();

                    internal_assert(reg.second.size() == args.size());

                    for (size_t v = 0; v < args.size(); v++) {
                        prod_pure_bounds[args[v]] = reg.second[v];
                    }

                    vector<DimBounds> prod_bounds =
                                get_stage_bounds(env.at(reg.first),
                                                 prod_pure_bounds);

                    size_t num_stages = prod_func.updates().size() + 1;

                    internal_assert(prod_bounds.size() == num_stages);

                    for (size_t prod_s = 0; prod_s < num_stages; prod_s++) {
                        FStage prod_stage(prod_func, prod_s);
                        f_queue.push_back(make_pair(prod_stage,
                                                    prod_bounds[prod_s]));
                    }
                }
            }
        }
        f_queue.pop_front();
    }

    // Simplify
    map<string, Box> concrete_regions;

    for (auto &f_reg : regions) {
        simplify_box(f_reg.second);

        Box concrete_box;
        for (uint32_t i = 0; i < f_reg.second.size(); i++) {
            Expr lower = f_reg.second[i].min;
            Expr upper = f_reg.second[i].max;

            // TODO: Assumes estimates cannot be provided on input parameters
            // like images. Need to have a better way of doing this see if
            // input parameters can have estimates attached to them.
            bool in_env = (env.find(f_reg.first) != env.end());

            // Use the estimates if the lower and upper bounds cannot be determined
            if (!lower.as<IntImm>() && in_env) {
                user_warning <<
                "Bounds for the following expression could not be inferred" <<
                "might result in suboptimal scheduling decisions:\n" << lower << '\n';
                const Function& curr_f = env.at(f_reg.first);
                for (auto& b: curr_f.schedule().estimates()) {
                    uint32_t num_pure_args = curr_f.args().size();
                    if (i < num_pure_args && b.var == curr_f.args()[i])
                        lower = Expr(b.min.as<IntImm>()->value);
                }
            }

            if (!upper.as<IntImm>() && in_env) {
                user_warning <<
                "Bounds for the following expression could not be inferred" <<
                "might result in suboptimal scheduling decisions:\n" << upper << '\n';
                const Function& curr_f = env.at(f_reg.first);
                for (auto& b: curr_f.schedule().estimates()) {
                    uint32_t num_pure_args = curr_f.args().size();
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

map<string, Box>
DependenceAnalysis::redundant_regions(Function f, int stage_num, string var,
                                      const DimBounds &bounds,
                                      const set<string> &prods,
                                      bool values_computed) {

    map<string, Box> regions = regions_required(f, stage_num, bounds,
                                                prods, values_computed);

    DimBounds shifted_bounds;

    for (auto &b : bounds) {
        if (b.first == var) {
            Expr len = b.second.max - b.second.min + 1;
            Interval bound = Interval(b.second.min + len,
                                      b.second.max + len);
            shifted_bounds[b.first] = bound;
        }
        else {
            shifted_bounds[b.first] = b.second;
        }
    }

    map<string, Box> regions_shifted =
            regions_required(f, stage_num, shifted_bounds, prods,
                             values_computed);

    map<string, Box> overalps;
    for (auto &reg: regions) {
        if (regions_shifted.find(reg.first) == regions.end()) {
            // It will be interesting to log cases where this actually happens
            // i.e., the shifted regions do not contain a function that was
            // there in the original regions.
            continue;
        } else {
            Box b = reg.second;
            Box b_shifted = regions_shifted[reg.first];
            // The boxes should be of the same size
            internal_assert(b.size() == b_shifted.size());
            // The box used makes things complicated ignoring it for now
            Box b_intersect;
            for (uint32_t i = 0 ; i < b.size(); i++)
                b_intersect.push_back(interval_intersect(b[i], b_shifted[i]));
            // A function should appear once in the regions and therefore cannot
            // already be present in the overlaps map
            internal_assert(overalps.find(reg.first) == overalps.end());
            overalps[reg.first] = b_intersect;
        }
    }

    // Simplify
    for (auto& f : overalps)
        simplify_box(f.second);

    return overalps;
}

map<string, Box> get_pipeline_bounds(DependenceAnalysis &analy,
                                     const vector<Function> &outputs) {
    map<string, Box> pipeline_bounds;

    for (auto &out: outputs) {
        DimBounds pure_bounds;
        Box out_box;
        for (auto& arg: out.args()) {
            bool estimate_found = false;
            for (auto& est: out.schedule().estimates()) {
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
        for (const pair<string, Function> fpair: analy.env) {
            prods.insert(fpair.first);
        }

        map<string, Box> regions =
                analy.regions_required(out, pure_bounds, prods, false);

        // Add the output region to the pipeline bounds as well
        regions[out.name()] = out_box;

        for (auto &reg: regions) {
            // Merge region with an existing region for the function in the global map
            if (pipeline_bounds.find(reg.first) == pipeline_bounds.end())
                pipeline_bounds[reg.first] = reg.second;
            else
                merge_boxes(pipeline_bounds[reg.first], reg.second);
        }
    }

    return pipeline_bounds;
}

struct Partitioner {

    struct FusionChoice {
        // FusionChoice encodes the choice of the prod_group being merged with
        // the cons_group at the granularity of the tile given by tile_sizes
        string prod;
        FStage cons;

        FusionChoice(string _prod, FStage _cons) : prod(_prod), cons(_cons) {}

        bool operator==(const FusionChoice &other) const {
            return (prod == other.prod) &&
                    (cons == other.cons);
        }

        bool operator<(const FusionChoice &other) const {
            return prod < other.prod || (prod == other.prod &&
                                         cons < other.cons) ;
        }

        friend std::ostream& operator<<(std::ostream &stream,
                                        const FusionChoice &choice) {
            stream << "Choice:" << choice.prod << "->" << choice.cons << '\n';
            return stream;
        }
    };

    struct Group {
        // The output stage representing the group
        FStage output;
        // All the functions that belong to the group
        vector<FStage> members;

        // Schedule information
        // All the members of the group which are inlined
        set<string> inlined;
        // Tile sizes along the dimensions of the output function of the group
        map<string, int> tile_sizes;

        Group(FStage _output, vector<FStage> _members):
              output(_output), members(_members) { }

        friend std::ostream& operator <<(std::ostream &stream, const Group &g) {

            stream << "Output FStage:" << g.output << '\n';
            stream << "Members:" << '[';
            for (auto &m: g.members) {
                stream << m << ",";
            }
            stream << "]" << '\n';

            stream << "Inlined:" << '[';
            for (auto &in: g.inlined) {
                stream << in << ",";
            }
            stream << "]" << '\n';

            stream << "Tile sizes:" << "[";
            for (auto &s: g.tile_sizes) {
                stream << "(" << s.first << "," <<  s.second << ")";
            }
            stream << "]" << '\n';

            return stream;
        }
    };

    struct GroupAnalysis {
        // Estimate of arithmetic cost
        int64_t arith_cost;
        // Estimate of accesses to slow memory
        int64_t mem_cost;
        // Estimate of the parallelism
        int64_t parallelism;

        friend std::ostream& operator <<(std::ostream &stream,
                                         const GroupAnalysis &analy) {
            stream << "[arith cost:" << analy.arith_cost << ",";
            stream << "mem_cost:" << analy.mem_cost << ",";
            stream << "parallelism:" << analy.parallelism << "]\n";

            return stream;
        }
    };

    struct EvalConfig {
        map<string, int> tile_sizes;
        GroupAnalysis analy;
        EvalConfig(const map<string, int> &_tile_sizes,
                   const GroupAnalysis &_analy) :
                   tile_sizes(_tile_sizes), analy(_analy) {}
    };

    map<FusionChoice, EvalConfig> fusion_cache;

    map<FStage, Group> groups;
    map<FStage, GroupAnalysis> group_costs;

    // Levels that are targetted by the grouping algorithm
    enum Level {INLINE, FAST_MEM};

    const map<string, Box> &pipeline_bounds;
    const MachineParams &arch_params;
    DependenceAnalysis &dep_analy;
    RegionCosts &cost_model;
    const vector<Function> &outputs;

    map<FStage, set<FStage> > children;

    bool gpu_schedule;

    Partitioner(map<string, Box> &_pipeline_bounds, MachineParams &_arch_params,
                DependenceAnalysis &_dep_analy, RegionCosts &_cost_model,
                const vector<Function> &_outputs, bool _gpu_schedule);

    void merge_groups(const FusionChoice &choice, const EvalConfig &eval,
                      Partitioner::Level level);

    EvalConfig evaluate_choice(const FusionChoice &fuse, Partitioner::Level level);

    Group fuse_groups(const Group &g1, const Group &g2);

    GroupAnalysis analyze_group(const Group &g, bool show_analysis);

    map<FStage, map<FStage, DimBounds>> get_group_member_bounds();

    void group(Partitioner::Level level);

    vector<pair<FusionChoice, EvalConfig>>
    choose_candidate_fuse(const vector<pair<string, string>> &cand_pairs,
                          Partitioner::Level level);

    map<string, int64_t> evaluate_reuse(const FStage &stg,
                                        const set<string> &prods);

    map<string, int> bounds_to_estimates(const DimBounds &bounds);

    string generate_cpu_schedule(const Target &t);

    string generate_group_cpu_schedule(const Group &g, const Target &t,
                                       const map<FStage, DimBounds> &group_bounds);

    DimBounds get_bounds(const FStage &stg);

    DimBounds get_bounds_from_tile_sizes(const FStage &stg,
                                         const map<string, int> &tile_sizes);

    vector<map<string, int>> generate_tile_configs(const FStage &stg);

    pair<map<string, int>, GroupAnalysis>
            find_best_tile_config(const Group &g, Partitioner::Level level);

    int64_t estimate_benefit(const GroupAnalysis &nofuse, const GroupAnalysis &fuse,
                             bool no_redundant_work, bool ensure_parallelism);

    int64_t estimate_benefit(const vector<pair<FusionChoice, EvalConfig>> &choices,
                             bool no_redundant_work, bool ensure_parallelism);

    void initialize_groups_inline();
    void initialize_groups_fast_mem();

    pair<int64_t, int64_t> get_pipeline_cost();

    void disp_pipeline_costs();
    void disp_pipeline_bounds();
    void disp_pipeline_graph();
    void disp_grouping();
};

Partitioner::Partitioner(map<string, Box> &_pipeline_bounds,
                         MachineParams &_arch_params,
                         DependenceAnalysis &_dep_analy,
                         RegionCosts &_cost_model,
                         const vector<Function> &_outputs,
                         bool _gpu_schedule):
    pipeline_bounds(_pipeline_bounds), arch_params(_arch_params),
    dep_analy(_dep_analy), cost_model(_cost_model), outputs(_outputs),
    gpu_schedule(_gpu_schedule)
{
    // Place each stage of a function in its own group
    for (auto& f: dep_analy.env) {
        int num_stages = f.second.updates().size() + 1;
        for (int s = 0; s < num_stages; s++) {
            FStage stg(f.second, s);
            Group g(stg, {stg});
            groups.insert(make_pair(stg, g));
        }
    }

    // Find consumers of each function and relate groups with their children
    for (auto& f: dep_analy.env) {
        int num_stages = f.second.updates().size() + 1;
        for (int s = 0; s < num_stages; s++) {

            FindAllCalls find;
            Definition def = get_stage_definition(f.second, s);
            def.accept(&find);

            for (const string& c: find.calls) {
                if (c != f.first && dep_analy.env.find(c) != dep_analy.env.end()) {
                    // Consumer depends on the last stage of the producer
                    Function prod_func = dep_analy.env.at(c);
                    int final_stage = prod_func.updates().size();

                    FStage prod_stage(prod_func, final_stage);
                    FStage cons_stage(f.second, s);

                    children[prod_stage].insert(cons_stage);
                }
            }

            if (s > 0) {
                // Add dependencies between all the stages in a function
                FStage prod_stage(f.second, s-1);
                FStage cons_stage(f.second, s);

                children[prod_stage].insert(cons_stage);
            }
        }
    }
}

void Partitioner::merge_groups(const FusionChoice &choice, const EvalConfig &eval,
                               Partitioner::Level level) {

    Function prod_f = dep_analy.env.at(choice.prod);
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
            for (auto &stg: cand_funcs) {
                child_group.inlined.insert(stg.func.name());
            }
        } else {
            for (auto &in: cand_group.inlined) {
                child_group.inlined.insert(in);
            }
        }
    }

    child_group.tile_sizes = eval.tile_sizes;

    // Update group costs
    group_costs[child] = analyze_group(child_group, false);
}

void Partitioner::disp_grouping() {
    debug(3) << "\n=========" << '\n';
    debug(3) << "Grouping:" << '\n';
    debug(3) << "=========" << '\n';
    for (auto& g: groups) {
        debug(3) << g.second << '\n';
    }
    debug(3) << "=========" << '\n';
}

void Partitioner::disp_pipeline_graph() {
    debug(3) << "\n================" << '\n';
    debug(3) << "Pipeline graph:" << '\n';
    debug(3) << "================" << '\n';
    for (auto& f: children) {
        debug(3) << f.first << ": [";
        for (auto& c: f.second) {
            debug(3) << c << ",";
        }
        debug(3) << "]" << '\n';
    }
    debug(3) << "================" << '\n';
}

void Partitioner::disp_pipeline_bounds() {
    debug(3) << "\n================" << '\n';
    debug(3) << "Pipeline bounds:" << '\n';
    debug(3) << "================" << '\n';
    disp_regions(pipeline_bounds);
    debug(3) << "===============" << '\n';
}

pair<int64_t, int64_t> Partitioner::get_pipeline_cost() {
    internal_assert(group_costs.size() > 0);

    int64_t total_arith = 0;
    int64_t total_mem = 0;
    for (const pair<FStage, Group> &g: groups) {
        GroupAnalysis analy = group_costs.at(g.first);
        total_mem += analy.mem_cost;
        total_arith += analy.arith_cost;
    }
    return make_pair(total_arith, total_mem);
}

void Partitioner::disp_pipeline_costs() {
    internal_assert(group_costs.size() > 0);
    int64_t total_arith = 0;
    int64_t total_mem = 0;
    debug(3) << "\n===============" << '\n';
    debug(3) << "Pipeline costs:" << '\n';
    debug(3) << "===============" << '\n';
    debug(3) << "Group:(name) [arith cost, mem cost, parallelism]" << '\n';
    for (const pair<FStage, Group> &g: groups) {
        GroupAnalysis analy = group_costs.at(g.first);
        total_mem += analy.mem_cost;
        total_arith += analy.arith_cost;

        debug(3) << "Group:" << g.first << "[";
        debug(3) << analy.arith_cost << "," <<
                 analy.mem_cost << "," << analy.parallelism << "]\n";
    }
    debug(3) << "Total arithmetic cost:" << total_arith << '\n';
    debug(3) << "Total memory cost:" << total_mem << '\n';
    debug(3) << "===============" << '\n';
}

void Partitioner::initialize_groups_inline() {
    for (pair<const FStage, Group> &g: groups) {

        map<string, int> tile_sizes;
        Definition def = get_stage_definition(g.first.func,
                                              g.first.stage_num);

        const vector<Dim> &dims = def.schedule().dims();
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            tile_sizes[dims[d].var] = 1;
        }

        g.second.tile_sizes = tile_sizes;
        GroupAnalysis inline_analy = analyze_group(g.second, false);
        group_costs[g.second.output] = inline_analy;
    }
    fusion_cache.clear();
}

void Partitioner::initialize_groups_fast_mem() {
    for (pair<const FStage, Group> &g: groups) {
        pair<map<string, int>, GroupAnalysis> best =
            find_best_tile_config(g.second, Partitioner::FAST_MEM);
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
                dep_analy.overlap_regions(stg.func, stg.stage_num,
                                          bounds, prods, false);

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        int64_t total_reuse = 0;
        disp_regions(reuse_regions[d]);
        for (auto &reg: reuse_regions[d]) {
            int64_t area = box_area(reg.second);
            if (area >= 0) {
                total_reuse += area;
            } else {
                total_reuse = -1;
                break;
            }
        }
        reuse[dims[d].var] = total_reuse;
    }

    return reuse;
}

vector<pair<Partitioner::FusionChoice, Partitioner::EvalConfig>>
Partitioner::choose_candidate_fuse(const vector<pair<string, string>> &cands,
                                   Partitioner::Level level) {

    vector<pair<FusionChoice, EvalConfig>> best_choices;
    int64_t best_benefit = 0;
    for (auto &p: cands) {
        // Compute the aggregate benefit for inlining into all the children
        vector<pair<FusionChoice, EvalConfig>> choices;

        Function prod_f = dep_analy.env.at(p.first);
        int final_stage = prod_f.updates().size();

        FStage prod(prod_f.name(), final_stage);

        for (const FStage &c: children[prod]) {

            GroupAnalysis tmp;
            EvalConfig best_config(map<string, int>(), tmp);
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

        int64_t overall_benefit = estimate_benefit(choices, false, true);

        for (auto &choice: choices) {
            debug(3) << "Cand choice:" << choice.first;
        }
        debug(3) << "Cand benefit:" << overall_benefit << '\n';
        // TODO: The grouping process can be non-deterministic when the costs
        // of two choices are equal
        if (best_benefit < overall_benefit) {
            best_choices = choices;
            best_benefit = overall_benefit;
        }
    }

    for (auto &choice: best_choices) {
        debug(3) << "\nBest choice:" << choice.first;
    }
    if (best_choices.size() > 0) {
        debug(3) << "Best benefit:" << best_benefit << '\n';
    }

    return best_choices;
}

vector<map<string, int>>
Partitioner::generate_tile_configs(const FStage &stg) {

    int min_vec_dim_size = 64;

    Definition def = get_stage_definition(stg.func, stg.stage_num);
    const vector<Dim> &dims = def.schedule().dims();

    set<string> pure_vars;
    for (const string& arg: stg.func.args()) {
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
        for (auto &dim_size: size_variants) {
            map<string, int> tiling;
            for (size_t j = 0; j < tile_vars.size(); j++) {
                if (j == i) {
                    tiling[tile_vars[j]] = j == 0 ?
                                std::max(dim_size, min_vec_dim_size): dim_size;
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
    for (auto &dim_size: size_variants) {
        map<string, int> tiling;
        for (size_t j = 0; j < tile_vars.size(); j++) {
            tiling[tile_vars[j]] = j == 0 ?
                            std::max(dim_size, min_vec_dim_size): dim_size;
        }
        tile_configs.push_back(tiling);
    }

    // Reorder tile configurations
    for (int i = 0; i < (1 << (tile_vars.size())); i++) {
        map<string, int> tiling;
        for (size_t j = 0; j < tile_vars.size(); j++) {
            if (((i >> (j)) & 1) == 1) {
                if (j == 0) {
                    tiling[tile_vars[j]] = min_vec_dim_size;
                } else {
                    tiling[tile_vars[j]] = 1;
                }
            }
        }
        tile_configs.push_back(tiling);
    }

    return tile_configs;
}

pair<map<string, int>, Partitioner::GroupAnalysis>
Partitioner::find_best_tile_config(const Group &g, Partitioner::Level level) {

    // TODO: Add sanity checks for the cost model

    // Initialize to no tiling
    map<string, int> no_tile_config;
    Group no_tile = g;
    no_tile.tile_sizes = no_tile_config;

    bool show_analysis = false;
    GroupAnalysis best_analy = analyze_group(no_tile, show_analysis);

    GroupAnalysis no_tile_analy = analyze_group(no_tile, show_analysis);
    map<string, int> best_config = no_tile_config;

    if (best_analy.arith_cost < 0) {
        return make_pair(best_config, best_analy);
    }

    // Generate tiling configurations
    vector<map<string, int>> configs = generate_tile_configs(g.output);

    Group best_group = g;
    for (auto &config: configs) {
        Group new_group = g;
        new_group.tile_sizes = config;

        GroupAnalysis new_analy = analyze_group(new_group, show_analysis);

        bool no_redundant_work = false;
        int64_t benefit = estimate_benefit(best_analy, new_analy,
                                           no_redundant_work, true);

        if (show_analysis) {
            debug(3) << "Benefit relative to not tiling:" << benefit << '\n';
            debug(3) << "Best analy:" << new_analy;
            debug(3) << "No tile analy:" << no_tile_analy;
            debug(3) << "arith cost:" <<
                     (float)new_analy.arith_cost/no_tile_analy.arith_cost << "," <<
                     "mem cost:" <<
                     (float)new_analy.mem_cost/no_tile_analy.mem_cost << '\n';
        }

        if (benefit > 0) {
            best_config = config;
            best_analy = new_analy;
            best_group = new_group;
        }
    }

    debug(3) << "\nBest grouping:\n" << best_group << '\n';

    return make_pair(best_config, best_analy);
}

void Partitioner::group(Partitioner::Level level) {
    // Partition the pipeline by iteratively merging groups until a fixpoint
    bool fixpoint = false;
    while(!fixpoint) {
        int64_t pre_merge_arith_cost = 0;
        int64_t pre_merge_mem_cost = 0;

        std::tie(pre_merge_arith_cost,
                 pre_merge_mem_cost) = get_pipeline_cost();

        fixpoint = true;
        vector<pair<string, string>> cand;
        for (const pair<FStage, Group> &g: groups) {

            bool is_output = false;
            for (const Function &f: outputs) {
                if (g.first.func.name() == f.name()) {
                    is_output = true;
                    break;
                }
            }

            // All the stages of a function are computed at a single location.
            // The last stage of the pipeline represents the candidate choice
            // of fusing the funtion into a consumer.

            const Function &prod_f = dep_analy.env.at(g.first.func.name());
            bool is_final_stage = (g.first.stage_num == prod_f.updates().size());

            if (is_output || !is_final_stage)
                continue;

            if (children.find(g.first) != children.end()) {
                // All the stages beloning to a function are considered to be a
                // single child.
                set<string> child_funcs;
                for (const FStage &s: children[g.first]) {
                    child_funcs.insert(s.func.name());
                }

                int num_children = child_funcs.size();
                // Only groups with a single child are considered for fusion
                // when grouping for computing in tiles. This is because the
                // scheduling model does not allow functions to be computed at
                // different points.
                if (num_children == 1 && level == Partitioner::FAST_MEM) {
                    string prod_name = prod_f.name();
                    string cons_name = (*child_funcs.begin());
                    cand.push_back(make_pair(prod_name, cons_name));
                } else if(num_children > 0  && level == Partitioner::INLINE &&
                          prod_f.is_pure()) {
                    string prod_name = prod_f.name();
                    cand.push_back(make_pair(prod_name, ""));
                }
            }
        }

        debug(3) << "\n============================" << '\n';
        debug(3) << "Current grouping candidates:" << '\n';
        debug(3) << "============================" << '\n';
        for (auto& p: cand) {
            debug(3) << "[" << p.first << "," << p.second << "]" << '\n';
        }

        vector<pair<FusionChoice, EvalConfig>> best;
        best = choose_candidate_fuse(cand, level);

        if (!(best.size() > 0)) {
            continue;
        } else {
            fixpoint = false;
        }

        // TODO: state assumptions behind the following code
        string prod = best[0].first.prod;

        Function prod_f = dep_analy.env.at(prod);
        size_t num_stages = prod_f.updates().size() + 1;

        FStage final_stage(prod_f, num_stages - 1);
        set<FStage> cand_group_children = children[final_stage];

        // Invalidate entries of the fusion cache
        set<FusionChoice> invalid_keys;
        for (auto &c: cand_group_children) {
            for (auto &entry: fusion_cache) {
                if (entry.first.prod == c.func.name() || entry.first.cons == c)
                    invalid_keys.insert(entry.first);
            }
        }

        for (auto &key: invalid_keys) {
            fusion_cache.erase(key);
        }

        for (auto &fuse: best) {
            internal_assert(fuse.first.prod == prod);
            merge_groups(fuse.first, fuse.second, level);
        }

        for (size_t s = 0; s < num_stages; s++) {
            FStage cand_group(prod_f, s);
            groups.erase(cand_group);
            group_costs.erase(cand_group);

            // Update the children mapping
            children.erase(cand_group);
            for (auto &f: children) {
                set<FStage> &cons = f.second;
                if (cons.find(cand_group) != cons.end()) {
                    cons.erase(cand_group);
                    cons.insert(cand_group_children.begin(),
                                cand_group_children.end());
                }
            }
        }

        int64_t post_merge_arith_cost = 0;
        int64_t post_merge_mem_cost = 0;

        std::tie(post_merge_arith_cost,
                 post_merge_mem_cost) = get_pipeline_cost();

        disp_pipeline_costs();
        internal_assert((pre_merge_arith_cost + pre_merge_mem_cost) >=
                        (post_merge_mem_cost + post_merge_arith_cost));
    }
}

DimBounds Partitioner::get_bounds(const FStage& s) {

    Definition def = get_stage_definition(s.func, s.stage_num);
    DimBounds bounds;

    const vector<string>& args = s.func.args();
    for (size_t d = 0; d < args.size(); d++) {
        bounds[args[d]] = pipeline_bounds.at(s.func.name())[d];
    }

    for (const ReductionVariable& rvar: def.schedule().rvars()) {
        bounds[rvar.var] = Interval(simplify(rvar.min),
                                    simplify(rvar.min + rvar.extent - 1));
    }
    return bounds;
}

DimBounds
Partitioner::get_bounds_from_tile_sizes(const FStage &s,
                                        const map<string, int> &tile_sizes) {

    Definition def = get_stage_definition(s.func, s.stage_num);
    map<string, Interval> bounds;

    const map<string, Interval> &def_bounds = get_bounds(s);
    const vector<Dim> &dims = def.schedule().dims();

    for (int d = 0; d < (int) dims.size() - 1; d++) {
        string var = dims[d].var;
        const Interval &bound = def_bounds.at(var);
        if (tile_sizes.find(var) != tile_sizes.end()) {
            int size = tile_sizes.at(var);
            // Check if the bounds allow for tiling with the given tile size
            // i.e., ensure atleast 2 tiles
            int extent = get_extent(bound);
            if (extent >= 2 * size) {
                // TODO: Maybe shift this to the center of the pipeline bound
                bounds[var] = Interval(0, size - 1);
            }
            else {
                // If the dimension is too small do not tile it and set the
                // extent of the bounds to that of the dimension estimate
                bounds[var] = bound;
            }
        }
        else {
            bounds[var] = bound;
        }
    }

    return bounds;
}

Partitioner::GroupAnalysis Partitioner::analyze_group(const Group &g, bool show_analysis) {
    // Estimating the number of accesses to slow memory

    // 1) Assume all loads are a miss if the working set does not fit in cache.
    // This ignores any locality that results from the iteration order. This is
    // pretty aggresive in estimating the benefit of fusion.
    //
    // 2) Assume that the intermediates are loaded only once even if they do not
    // fit in cache. It is a pretty good model for pipelines which are streaming
    // in nature. This gives a conservative estimate of fusion benefit and does
    // not accurately capture scenarios where there is significant reuse.
    //
    // The actual number of accesses will inbetween 2) and 1) for now going with
    // model 1).
    //
    // TODO: Model needs to be refined further to account for spatial locality and
    // iteration order.

    // Get the definition corresponding to the group output
    Definition def = get_stage_definition(g.output.func, g.output.stage_num);

    set<string> group_inputs;
    set<string> group_mem;

    for (auto &stg: g.members) {
        group_mem.insert(stg.func.name());

        FindAllCalls find;
        Definition stg_def = get_stage_definition(stg.func, stg.stage_num);

        stg_def.accept(&find);
        for (auto &c: find.calls) {
            bool is_member = false;
            for (auto& m: g.members) {
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

    for (int d = 0; d < (int)dims.size() - 1; d++) {
        string var = dims[d].var;
        if (g.tile_sizes.find(var) != g.tile_sizes.end()) {
            int size = g.tile_sizes.at(var);
            int extent = get_extent(stg_bounds.at(var));
            estimate_tiles *= std::ceil((float)extent/size);
            num_ele_per_tile *= size;
            if (can_parallelize_rvar(var, g.output.func.name(), def)) {
                parallelism *= std::ceil((float)extent/size);
            }
        }
    }

    // Get the regions of the pipeline required to compute a tile of the group
    DimBounds tile_bounds = get_bounds_from_tile_sizes(g.output, g.tile_sizes);

    map<string, Box> alloc_reg =
            dep_analy.regions_required(g.output.func, g.output.stage_num,
                                       tile_bounds, group_mem, false);

    map<string, Box> compute_reg =
            dep_analy.regions_required(g.output.func, g.output.stage_num,
                                       tile_bounds, group_mem, true);

    map<string, Box> group_reg, prod_reg, input_reg;

    // Separating into regions that belong to the group and regions that are
    // input to the group
    for (auto &reg: compute_reg) {
        if (group_mem.find(reg.first) != group_mem.end() &&
            reg.first != g.output.func.name()) {
            group_reg[reg.first] = reg.second;
        } else if (group_inputs.find(reg.first) != group_inputs.end()) {
            if (dep_analy.env.find(reg.first) != dep_analy.env.end()) {
                prod_reg[reg.first] = reg.second;
            } else {
                input_reg[reg.first] = reg.second;
            }
        }
    }

    GroupAnalysis g_analy;
    g_analy.arith_cost = -1;
    g_analy.mem_cost = -1;
    g_analy.parallelism = -1;

    if (show_analysis) {
        debug(3) << "==============\n";
        debug(3) << "Group Analysis\n";
        debug(3) << "==============\n";
        debug(3) << g;
        debug(3) << "\nProd reg:" << '\n';
        disp_regions(prod_reg);
        debug(3) << "Input reg:" << '\n';
        disp_regions(input_reg);
        debug(3) << "Group reg:" << '\n';
        disp_regions(group_reg);
    }

    // Aggregate costs for intermediate functions in a tile and the
    // tile output
    pair<int64_t, int64_t> tile_cost =
            cost_model.region_cost(group_reg, g.inlined);

    pair<int64_t, int64_t> out_cost =
            cost_model.stage_region_cost(g.output.func.name(),
                                         g.output.stage_num,
                                         tile_bounds, g.inlined);

    if (tile_cost.first < 0 || tile_cost.second < 0 ||
        out_cost.first < 0 || out_cost.second < 0) {
        return g_analy;
    }

    pair<int64_t, int64_t> group_cost(tile_cost.first + out_cost.first,
                                      tile_cost.second + out_cost.second);

    // Detailed load costs for all the group intermediates
    map<string, int64_t> group_load_costs =
            cost_model.detailed_load_costs(group_reg, g.inlined);

    map<string, int64_t> out_load_costs =
            cost_model.stage_detailed_load_costs(g.output.func.name(),
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

    int64_t per_tile_arith_cost = group_cost.first;
    int64_t per_tile_mem_cost = 0;

    // Old cost model keeping it here for reference
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
    float load_slope = (float)arch_params.balance/arch_params.last_level_size;
    for (auto &f_load: group_load_costs) {
        int64_t footprint = 0;
        if (group_mem.find(f_load.first) != group_mem.end() &&
            f_load.first != g.output.func.name()) {
            footprint = cost_model.region_size(f_load.first,
                                               alloc_reg[f_load.first]);
        } else {
            int64_t initial_footprint = 0;
            if (dep_analy.env.find(f_load.first) != dep_analy.env.end()) {
                // Initial loads
                initial_footprint =
                        cost_model.region_size(f_load.first,
                                               pipeline_bounds.at(f_load.first));
                // Subsequent loads
                footprint = cost_model.region_size(f_load.first,
                                                   alloc_reg.at(f_load.first));
            } else {
                // Initial loads
                initial_footprint =
                        cost_model.input_region_size(f_load.first,
                                                     pipeline_bounds.at(f_load.first));
                // Subsequent loads
                if (f_load.first == g.output.func.name()) {
                    footprint = cost_model.input_region_size(f_load.first,
                                                             out_tile_extent);
                } else {
                    footprint = cost_model.input_region_size(f_load.first,
                                                             alloc_reg.at(f_load.first));
                }
            }

            if (model_reuse) {
                int64_t initial_factor =
                        std::trunc(std::min(1 + initial_footprint * load_slope,
                                            (float)arch_params.balance));

                per_tile_mem_cost += initial_factor * footprint;
            } else {
                footprint = initial_footprint;
            }
        }

        int cost_factor = std::trunc(std::min(1 + footprint * load_slope,
                                     (float)arch_params.balance));
        per_tile_mem_cost += cost_factor * f_load.second;
    }

    if (show_analysis) {
        debug(3) << "\nDetailed loads:\n";
        for (auto &f_load: group_load_costs) {
            debug(3) << "(" << f_load.first << "," << f_load.second << ")";
        }
        debug(3) << '\n';

        debug(3) << "\nPer tile mem cost:" << per_tile_mem_cost << '\n';
        debug(3) << "Per tile arith cost:" << per_tile_arith_cost << '\n';
    }

    g_analy.mem_cost = per_tile_mem_cost * estimate_tiles;
    g_analy.arith_cost = per_tile_arith_cost * estimate_tiles;
    g_analy.parallelism = parallelism;

    internal_assert(per_tile_mem_cost > 0);

    return g_analy;
}

Partitioner::Group Partitioner::fuse_groups(const Group &prod_group,
                                            const Group &cons_group) {

    vector<FStage> fused_members;
    for (auto &s: prod_group.members)
        fused_members.push_back(s);
    for (auto &s: cons_group.members)
        fused_members.push_back(s);

    Group fused_group(cons_group.output, fused_members);

    for (auto &f: prod_group.inlined)
        fused_group.inlined.insert(f);
    for (auto &f: cons_group.inlined)
        fused_group.inlined.insert(f);

    return fused_group;
}

Partitioner::EvalConfig
Partitioner::evaluate_choice(const FusionChoice &choice,
                             Partitioner::Level level) {

    // Create a group that reflects the fusion choice and evaluate the cost
    // of the group.
    Function prod_f = dep_analy.env.at(choice.prod);
    int num_prod_stages = prod_f.updates().size() + 1;
    vector<Group> prod_groups;

    for (int s = 0; s < num_prod_stages; s++) {
        FStage prod_s(prod_f, s);
        prod_groups.push_back(groups.at(prod_s));
    }

    Group cons = groups.at(choice.cons);
    Group fused = cons;
    for (auto &prod_g: prod_groups) {
        fused = fuse_groups(prod_g, fused);
    }

    GroupAnalysis fused_analy;
    map<string, int> best_tile_config;

    if (level == Partitioner::INLINE) {
        // Set the tile sizes to one along all dimensions of the consumer group
        map<string, int> tile_sizes;

        const Function &cons_f = cons.output.func;
        Definition def = get_stage_definition(cons_f,
                                              cons.output.stage_num);

        const vector<Dim> &dims = def.schedule().dims();
        for (int d = 0; d < (int)dims.size() - 1; d++) {
            tile_sizes[dims[d].var] = 1;
        }

        fused.tile_sizes = tile_sizes;

        for (auto &prod_g: prod_groups) {
            for (const FStage &s: prod_g.members)
                fused.inlined.insert(s.func.name());
        }

        for (const string &f: cons.inlined) {
            fused.inlined.insert(f);
        }

        fused_analy = analyze_group(fused, false);

        best_tile_config = tile_sizes;

    } else {
        pair<map<string, int>, GroupAnalysis> config =
                                    find_best_tile_config(fused, level);
        best_tile_config = config.first;
        fused_analy = config.second;
    }

    return EvalConfig(best_tile_config, fused_analy);
}

int64_t Partitioner::estimate_benefit(const GroupAnalysis &nofuse,
                                      const GroupAnalysis &fuse,
                                      bool no_redundant_work,
                                      bool ensure_parallelism) {

    if (ensure_parallelism &&
        fuse.parallelism < arch_params.parallelism) {
        return -1;
    }

    int64_t arith_benefit = 0;
    if (nofuse.arith_cost >= 0 && fuse.arith_cost >= 0) {
        arith_benefit = nofuse.arith_cost - fuse.arith_cost;
    } else {
        return -1;
    }

    if (no_redundant_work && arith_benefit < 0)
        return arith_benefit;

    int64_t mem_benefit = 0;
    if (nofuse.mem_cost >= 0 && fuse.mem_cost >= 0) {
        mem_benefit = nofuse.mem_cost - fuse.mem_cost;
    } else {
        return -1;
    }

    return mem_benefit + arith_benefit;
}

int64_t Partitioner::estimate_benefit(
        const vector<pair<FusionChoice, EvalConfig>> &choices,
        bool no_redundant_work, bool ensure_parallelism) {

    GroupAnalysis fused_analy;
    fused_analy.arith_cost = 0;
    fused_analy.mem_cost = 0;
    fused_analy.parallelism = std::numeric_limits<int64_t>::max();

    set<FStage> no_fuse_groups;

    for (auto &choice: choices) {

        Function prod_f = dep_analy.env.at(choice.first.prod);
        int num_prod_stages = prod_f.updates().size() + 1;
        for (int s = 0; s < num_prod_stages; s++) {
            FStage prod_s(prod_f, s);
            no_fuse_groups.insert(prod_s);
        }
        no_fuse_groups.insert(choice.first.cons);

        GroupAnalysis analyg = choice.second.analy;
        if (analyg.arith_cost >= 0) {
            fused_analy.arith_cost += analyg.arith_cost;
            fused_analy.mem_cost += analyg.mem_cost;
            fused_analy.parallelism = std::min(fused_analy.parallelism,
                                              analyg.parallelism);
        } else {
            fused_analy.arith_cost = -1;
            fused_analy.mem_cost= -1;
            fused_analy.parallelism = -1;
            break;
        }
    }

    GroupAnalysis no_fuse_analy;
    no_fuse_analy.arith_cost = 0;
    no_fuse_analy.mem_cost = 0;
    no_fuse_analy.parallelism = std::numeric_limits<int64_t>::max();

    for (auto &g: no_fuse_groups) {
        internal_assert(group_costs.find(g) != group_costs.end());
        GroupAnalysis analyg = group_costs.at(g);
        if (analyg.arith_cost >= 0) {
            no_fuse_analy.arith_cost += analyg.arith_cost;
            no_fuse_analy.mem_cost += analyg.mem_cost;
            no_fuse_analy.parallelism = std::min(no_fuse_analy.parallelism,
                                                 analyg.parallelism);
        } else {
            no_fuse_analy.arith_cost = -1;
            no_fuse_analy.mem_cost = -1;
            no_fuse_analy.parallelism = -1;
            break;
        }
    }

    return estimate_benefit(no_fuse_analy, fused_analy, no_redundant_work,
                            ensure_parallelism);
}

map<string, int> Partitioner::bounds_to_estimates(const DimBounds &bounds) {
    map<string, int> estimates;
    for (auto &bound: bounds) {
        int estimate = get_extent(bound.second);
        estimates[bound.first] = estimate;
    }
    return estimates;
}

map<FStage, map<FStage, DimBounds>> Partitioner::get_group_member_bounds() {

    map<FStage, map<FStage, DimBounds>> group_bounds;
    for (const pair<const FStage, Group> &gpair: groups) {
        Group g = gpair.second;
        map<FStage, DimBounds> mem_bounds;

        DimBounds bounds = get_bounds_from_tile_sizes(g.output, g.tile_sizes);

        set<string> prods;
        for (const FStage &s: g.members) {
            prods.insert(s.func.name());
        }

        map<string, Box> conc_reg =
                dep_analy.regions_required(g.output.func, g.output.stage_num,
                                           bounds, prods, false);

        for (const FStage &s: g.members) {
            if (conc_reg.find(s.func.name()) != conc_reg.end()) {
                map<string, int> tile_sizes;
                const vector<string> &args = s.func.args();
                for (size_t arg = 0; arg < args.size(); arg++) {
                    tile_sizes[args[arg]] = get_extent(conc_reg[s.func.name()][arg]);
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
          string out_suffix, map<string, int> &estimates, string &sched) {
    // Create new variables for the split dimensions
    string arg_name = v.name();
    string inner_name = arg_name + in_suffix;
    string outer_name = arg_name + out_suffix;
    VarOrRVar inner(inner_name), outer(outer_name);

    sched += "Var " + inner_name + "(\"" + outer_name + "\")" + ";\n";
    sched += "Var " + outer_name + "(\"" + outer_name + "\")" + ";\n";

    f_handle.split(v, outer, inner, factor);

    sched += f_handle.name() + ".split(" + arg_name + ',' +
             outer_name + ',' + inner_name +
            ',' + std::to_string(factor) + ");\n";

    internal_assert(estimates.find(arg_name) != estimates.end());

    estimates[inner_name] = factor;
    estimates[outer_name] =
            std::ceil((float)estimates.at(arg_name)/factor);
    estimates.erase(arg_name);

    return make_pair(inner, outer);
}

void vectorize_stage(Stage f_handle, Definition def, Function func,
                     const Target &t, set<string> &rvars,
                     map<string, int> &estimates, string &sched) {
    const vector<Dim> &dims = f_handle.get_schedule().dims();
    int vec_dim_index = -1;

    // Set the vector length as the maximum of the values produced by a
    // function
    int vec_len = 0;
    for (auto &type: func.output_types()) {
        vec_len = std::max(vec_len, t.natural_vector_size(type));
    }

    for (int d = 0; d < (int) dims.size() - 1; d++) {
        string dim_name = get_base_name(dims[d].var);
        bool can_vectorize = true;
        if (rvars.find(dim_name) != rvars.end()) {
            can_vectorize = can_parallelize_rvar(dim_name, func.name(), def);
        }
        if (estimates.find(dim_name) != estimates.end()) {
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
                split_dim(f_handle, vec_var, vec_len, "_vi", "_vo",
                          estimates, sched);

        f_handle.vectorize(split_vars.first);
        sched += f_handle.name() + ".vectorize(" +
                split_vars.first.name() + ");\n";

        if (is_rvar) {
            rvars.erase(vec_dim_name);
            rvars.insert(split_vars.first.name());
            rvars.insert(split_vars.second.name());
        }
    }
}

string Partitioner::generate_group_cpu_schedule(
                    const Group &g, const Target &t,
                    const map<FStage, DimBounds> &group_bounds) {

    string sched = "";
    string out_f_name = g.output.func.name();
    Function g_out = g.output.func;

    debug(1) << "\n================\n";
    debug(1) << "Scheduling group:\n";
    debug(1) << "=================\n";
    debug(1) << g;

    // Get the definition corresponding to the stage
    Definition def = get_stage_definition(g_out,
                                          g.output.stage_num);

    // Get the estimates for stage bounds
    DimBounds stg_bounds = get_bounds(g.output);
    map<string, int> stg_estimates = bounds_to_estimates(stg_bounds);

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

    // Realize tiling and update the dimension estimates
    vector<VarOrRVar> outer_dims;
    vector<VarOrRVar> inner_dims;

    vector<Dim> &dims = def.schedule().dims();

    // Keep track of the rvars
    set<string> rvars;
    for (int d = 0; d < (int) dims.size() - 1; d++) {
        bool is_pure_var = false;
        for (auto &arg: g_out.args()) {
            if (arg == get_base_name(dims[d].var)) {
                is_pure_var = true;
                break;
            }
        }
        if (!is_pure_var) {
            rvars.insert(get_base_name(dims[d].var));
        }
    }

    vector<string> dim_vars;
    for (int d = 0; d < (int) dims.size() - 1; d++) {
        dim_vars.push_back(get_base_name(dims[d].var));
    }

    for (auto &var: dim_vars) {
        bool is_rvar = (rvars.find(var) != rvars.end());
        VarOrRVar v(var, is_rvar);

        if (g.tile_sizes.find(var) != g.tile_sizes.end() &&
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
        for (auto& v: inner_dims)
            ordering.push_back(v);
        for (auto& v: outer_dims)
            ordering.push_back(v);

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
    // parallelize over it or generate nested parallelism
    //
    // Go from the outer to the inner most loop till sufficient parallelism
    // is achieved
    int dim_start = dims.size() - 2;
    for (int d = dim_start; d >= 0; d--) {
        string var = get_base_name(dims[d].var);
        bool is_rvar = (rvars.find(var) != rvars.end());
        VarOrRVar v(var, is_rvar);

        if (is_rvar && !can_parallelize_rvar(var, g_out.name(), def)) {
            break;
        }

        if (def_par >= arch_params.parallelism) {
            // Enough parallelism to saturate target machine
            break;
        }

        if (stg_estimates.find(var) != stg_estimates.end()) {
            f_handle.parallel(v);
            sched += f_handle.name() + ".parallel(" + var + ");\n";
            def_par *= stg_estimates[var];
        } else {
            break;
        }
    }

    if (def_par < arch_params.parallelism) {
        user_warning << "Warning: insuffcient parallelism for " <<
                         f_handle.name() << '\n';
    }

    // The level at which group members will be computed
    int tile_inner_index = dims.size() - outer_dims.size() - 1;
    VarOrRVar tile_inner_var("", false);
    if (outer_dims.size() > 0) {
        string var_name = get_base_name(dims[tile_inner_index].var);
        bool is_rvar = (rvars.find(var_name) != rvars.end());
        tile_inner_var = VarOrRVar(var_name, is_rvar);
    }

    for (const FStage &mem: g.members) {
        // Skip member stages that have been inlined
        if (g.inlined.find(mem.func.name()) != g.inlined.end() ||
            mem.func.name() == g_out.name())
            continue;

        // Get the definition corresponding to the stage
        Definition mem_def = get_stage_definition(mem.func, mem.stage_num);

        // Get the estimates for the dimensions of the member stage
        map<string, int> mem_estimates =
                bounds_to_estimates(group_bounds.at(mem));

        set<string> mem_rvars;
        const vector<Dim> &mem_dims = mem_def.schedule().dims();
        for (int d = 0; d < (int) mem_dims.size() - 1; d++) {
            bool is_pure_var = false;
            for (auto &arg: mem.func.args()) {
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

        vectorize_stage(mem_handle, mem_def, mem.func, t, mem_rvars,
                        mem_estimates, sched);
    }

    return sched;
}

string Partitioner::generate_cpu_schedule(const Target& t) {
    string sched = "";

    // Grab the group bounds early as they rely on the dimensions of the group
    // outputs which will be altered by modifying schedules
    map<FStage, map<FStage, DimBounds>> group_bounds =
                                        get_group_member_bounds();

    for (const pair<FStage, Group>& g: groups) {
        for (const string& inline_func: g.second.inlined) {
            Function f = dep_analy.env.at(inline_func);
            Func f_handle(f);
            // TODO: inling functions with update definitions has different
            // behavior than pure functions. They may need to be computed above
            // the inner most vector loop to avoid complications with varying
            // extents across different vector lanes.

            f_handle.compute_inline();
            sched += f_handle.name() + ".compute_inline()" + ";\n";
        }
    }

    for (auto& g: groups)
        sched += generate_group_cpu_schedule(g.second, t, group_bounds[g.first]);
    return sched;
}

string generate_schedules(const vector<Function>& outputs, const Target& target) {

    string sched;
    // Compute an environment map which is used throughout the auto scheduling
    // process
    map<string, Function> env;
    for (Function f : outputs) {
        map<string, Function> more_funcs = find_transitive_calls(f);
        env.insert(more_funcs.begin(), more_funcs.end());
    }

    vector<string> order = realization_order(outputs, env);

    FuncValueBounds func_val_bounds = compute_function_value_bounds(order, env);

    bool estimates_avail = check_estimates_on_outputs(outputs);

    if (!estimates_avail) {
        user_warning << "Please provide estimates for each dimension" <<
                        "of the pipeline output functions.\n";

        // TODO: Update sched even in the degenerate case
        set_schedule_defaults(env);
        return sched;
    }

    map<string, vector<string> > update_args;
    set<string> reductions;
    DependenceAnalysis dep_analy(env, func_val_bounds);

    // Compute bounds of all the functions in the pipeline given estimates
    // on outputs. Also report functions where the bounds could not be inferred.
    map<string, Box> pipeline_bounds = get_pipeline_bounds(dep_analy, outputs);

    // Set machine parameters
    // TODO: Expose machine parameters to the user
    MachineParams arch_params;
    arch_params.parallelism = 16;
    arch_params.vec_len = 8;
    arch_params.register_file_size = 1024; // 1KB
    arch_params.last_level_size = 2 * 8 * 1024 * 1024; // 64 MB
    arch_params.balance = 40;

    // Initialize the cost model
    // Compute the expression costs for each function in the pipeline
    RegionCosts cost_model(env);
    cost_model.disp_func_costs();

    Partitioner part(pipeline_bounds, arch_params, dep_analy,
                     cost_model, outputs, false);

    // Compute and display reuse
    /* TODO: Use the reuse estimates to reorder loops
    for (auto &f: env) {
        FindAllCalls find;
        f.second.accept(&find);
        int num_stages = f.second.updates().size() + 1;
        for (int s = 0; s < num_stages; s++) {
            FStage curr_s(f.second, s);
            map<string, int64_t> reuse =
                    part.evaluate_reuse(curr_s, find.calls);
            debug(0) << curr_s << '\n';
            for (auto &dir: reuse) {
                debug(0) << dir.first << " " << dir.second << ',';
            }

            debug(0) << '\n';
        }
    }*/

    // Show the current pipeline graph
    // TODO: Output the graph in dot format
    part.disp_pipeline_graph();
    part.disp_pipeline_bounds();

    part.initialize_groups_inline();
    part.disp_pipeline_costs();

    part.group(Partitioner::INLINE);
    part.disp_grouping();

    part.initialize_groups_fast_mem();
    part.group(Partitioner::FAST_MEM);

    part.disp_pipeline_costs();

    sched = part.generate_cpu_schedule(target);

    // TODO: Unify both inlining and grouping for fast mem
    // TODO: GPU scheduling
    // TODO: Hierarchical tiling

    return sched;
}

}
}
