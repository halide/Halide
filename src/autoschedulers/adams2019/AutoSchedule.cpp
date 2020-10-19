/*
  This file is the core of the autoscheduler. Most of the code here is
  about navigating the search space and computing the
  featurization. This also contains the top-level interface into the
  autoscheduler.

  The most interesting classes to look at are:

  LoopNest               Represents one node in our tree representation of loop nests.
  State                  A state in the beam search. Holds a root loop nest.

  Interesting functions below are:

  generate_schedule            The top-level entrypoint, which computes and applies a schedule to a Halide pipeline
  optimal_schedule             Runs the passes of the coarse-to-fine beam search
  optimal_schedule_pass        Runs a single pass of beam search
  LoopNest::compute_features   Recursively walks over a loop nest tree, computing our featurization using Halide's analysis tools.
  LoopNest::apply              Actually apply a computed schedule to a Halide pipeline
  State::generate_children     Generates successor states to a state in the beam search

  Environment variables used (directly or indirectly):

  HL_BEAM_SIZE
  Beam size to use in the beam search. Defaults to 32. Use 1 to get a greedy search instead.

  HL_CYOS
  "Choose-your-own-schedule". If set to 1, lets you navigate the search tree by hand in the terminal. Whee! This is for debugging the autoscheduler.

  HL_FEATURE_FILE -> output
  *** DEPRECATED *** use the 'featurization' output from Generator instead
  Write out a training featurization for the selected schedule into this file.
  Needs to be converted to a sample file with the runtime using featurization_to_sample before it can be used to train.

  HL_MACHINE_PARAMS
  An architecture description string. Used by Halide master to configure the cost model. We only use the first term. Set it to the number of cores to target.

  HL_PERMIT_FAILED_UNROLL
  Set to 1 to tell Halide not to freak out if we try to unroll a loop that doesn't have a constant extent. Should generally not be necessary, but sometimes the autoscheduler's model for what will and will not turn into a constant during lowering is inaccurate, because Halide isn't perfect at constant-folding.

  HL_SCHEDULE_FILE
    *** DEPRECATED *** use the 'schedule' output from Generator instead
    Write out a human-and-machine readable block of scheduling source code for the selected schedule into this file.

  HL_RANDOM_DROPOUT
  percent chance of accepting each state in the beam. Normalized by the number of decisions made, so 5 would be there's a 5 percent chance of never rejecting any states.

  HL_SEED
  Random seed used by the random dropout.

  HL_WEIGHTS_DIR
  When training or schedule, read weights from this directory or file
  (if path ends in `.weights` it is written as a single file, otherwise a directory of files)

  HL_NO_SUBTILING
  If set to 1, limits the search space to that of Mullapudi et al.

  HL_DEBUG_AUTOSCHEDULE
  If set, is used for the debug log level for auto-schedule generation (overriding the
  value of HL_DEBUG_CODEGEN, if any).

  HL_AUTOSCHEDULE_MEMORY_LIMIT
  If set, only consider schedules that allocate at most this much memory (measured in bytes).

  TODO: expose these settings by adding some means to pass args to
  generator plugins instead of environment vars.
*/
#include "HalidePlugin.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "ASLog.h"
#include "AutoSchedule.h"
#include "CostModel.h"
#include "DefaultCostModel.h"
#include "Errors.h"
#include "Featurization.h"
#include "FunctionDAG.h"
#include "Halide.h"
#include "LoopNest.h"
#include "NetworkSize.h"
#include "PerfectHashMap.h"

#ifdef _WIN32
#include <io.h>
#define _isatty isatty;
#endif

namespace Halide {
namespace Internal {
namespace Autoscheduler {

using std::map;
using std::pair;
using std::string;
using std::vector;

struct ProgressBar {
    void set(double progress) {
        if (!draw_progress_bar) {
            return;
        }
        counter++;
        const int bits = 11;
        if (counter & ((1 << bits) - 1)) {
            return;
        }
        const int pos = (int)(progress * 78);
        aslog(0) << "[";
        for (int j = 0; j < 78; j++) {
            if (j < pos) {
                aslog(0) << ".";
            } else if (j - 1 < pos) {
                aslog(0) << "/-\\|"[(counter >> bits) % 4];
            } else {
                aslog(0) << " ";
            }
        }
        aslog(0) << "]";
        for (int j = 0; j < 80; j++) {
            aslog(0) << "\b";
        }
    }

    void clear() {
        if (counter) {
            for (int j = 0; j < 80; j++) {
                aslog(0) << " ";
            }
            for (int j = 0; j < 80; j++) {
                aslog(0) << "\b";
            }
        }
    }

private:
    uint32_t counter = 0;
    const bool draw_progress_bar = isatty(2);
};

// Get the HL_RANDOM_DROPOUT environment variable. Purpose of this is described above.
uint32_t get_dropout_threshold() {
    string random_dropout_str = get_env_variable("HL_RANDOM_DROPOUT");
    if (!random_dropout_str.empty()) {
        return atoi(random_dropout_str.c_str());
    } else {
        return 100;
    }
}

// Decide whether or not to drop a beam search state. Used for
// randomly exploring the search tree for autotuning and to generate
// training data.
bool random_dropout(std::mt19937 &rng, size_t num_decisions) {
    static double random_dropout_threshold = get_dropout_threshold();
    if (random_dropout_threshold >= 100) {
        return false;
    }

    // The random dropout threshold is the chance that we operate
    // entirely greedily and never discard anything.
    double t = random_dropout_threshold;
    t /= 100;
    t = std::pow(t, 1.0f / num_decisions);
    t *= 100;

    uint32_t r = rng();
    bool drop_it = (r % 100) >= t;
    return drop_it;
}

struct State {
    mutable RefCount ref_count;
    IntrusivePtr<const LoopNest> root;
    IntrusivePtr<const State> parent;
    double cost = 0;
    int num_decisions_made = 0;
    bool penalized = false;

    State() = default;
    State(const State &) = delete;
    State(State &&) = delete;
    void operator=(const State &) = delete;
    void operator=(State &&) = delete;

    static int cost_calculations;

    uint64_t structural_hash(int depth) const {
        uint64_t h = num_decisions_made;
        internal_assert(root.defined());
        root->structural_hash(h, depth);
        return h;
    }

    // Compute the parent and depth of every loop nest node
    void compute_loop_nest_parents(map<const LoopNest *, pair<const LoopNest *, int>> &p,
                                   const LoopNest *here, int depth) {
        for (const auto &c : here->children) {
            p.emplace(c.get(), pair<const LoopNest *, int>{here, depth});
            compute_loop_nest_parents(p, c.get(), depth + 1);
        }
    }

    const LoopNest *deepest_common_ancestor(const map<const LoopNest *, pair<const LoopNest *, int>> &parent,
                                            const LoopNest *a, const LoopNest *b) {
        if (a->is_root()) {
            return a;
        }
        if (b->is_root()) {
            return b;
        }
        if (a == b) {
            return a;
        }

        // Walk the deeper one up until they're at the same depth
        auto it_a = parent.find(a);
        auto it_b = parent.find(b);
        internal_assert(it_a != parent.end() && it_b != parent.end());
        while (it_a->second.second > it_b->second.second) {
            a = it_a->second.first;
            it_a = parent.find(a);
        }
        while (it_b->second.second > it_a->second.second) {
            b = it_b->second.first;
            it_b = parent.find(b);
        }

        while (1) {
            // Walk each up one
            a = it_a->second.first;
            b = it_b->second.first;
            if (a == b) {
                return a;
            }
            it_a = parent.find(a);
            it_b = parent.find(b);
            internal_assert(it_a != parent.end() && it_b != parent.end());
        }

        // unreachable
        return nullptr;
    }

    void compute_featurization(const FunctionDAG &dag, const MachineParams &params, StageMap<ScheduleFeatures> *features) {
        StageMap<LoopNest::Sites> sites;
        sites.make_large(dag.nodes[0].stages[0].max_id);
        features->make_large(dag.nodes[0].stages[0].max_id);
        internal_assert(root.defined());
        root->get_sites(sites);

        // For the input nodes and unscheduled outputs, the compute
        // and store sites are root, and the produce and innermost
        // sites are unset (nullptr)
        for (const auto &n : dag.nodes) {
            if (n.is_input || n.is_output) {
                for (const auto &stage : n.stages) {
                    auto &s = sites.get_or_create(&stage);
                    if (s.compute == nullptr) {
                        s.compute = root.get();
                        s.store = root.get();
                    }
                }
            }
        }

        // For the unscheduled nodes, give them sites as deep as they
        // could possibly be. We'll ignore the possibility of inlining
        // them for now.
        map<const LoopNest *, pair<const LoopNest *, int>> parent;
        compute_loop_nest_parents(parent, root.get(), 0);
        for (const auto &n : dag.nodes) {
            if (sites.contains(&(n.stages[0]))) {
                continue;
            }
            const LoopNest *loop = nullptr;
            for (const auto *e : n.outgoing_edges) {
                const auto &consumer_site = sites.get(e->consumer);
                const LoopNest *l = consumer_site.innermost;
                if (!l) {
                    l = consumer_site.compute;
                }
                if (!l) {
                    if (aslog::aslog_level() > 0) {
                        dump();
                    }
                    internal_error << e->producer->func.name() << " -> " << e->consumer->name << "\n";
                }
                if (loop) {
                    loop = deepest_common_ancestor(parent, l, loop);
                } else {
                    loop = l;
                }
            }
            internal_assert(loop)
                << "Could not compute plausible site for unscheduled Func: "
                << n.func.name() << "\n";
            for (auto &stage : n.stages) {
                auto &site = sites.get_or_create(&stage);
                site.compute = loop;
                site.store = loop;
            }
        }

        root->compute_features(dag, params, sites, 1, 1, nullptr, nullptr, *root, nullptr, features);

        for (const auto &n : dag.nodes) {
            if (sites.get(&(n.stages[0])).produce == nullptr) {
                internal_assert(!features->contains(&(n.stages[0])))
                    << "Somehow an input or unscheduled node ended up in the featurization: "
                    << n.func.name() << "\n";
            }
        }
    }

    void save_featurization(const FunctionDAG &dag, const MachineParams &params, std::ostream &out) {
        StageMap<ScheduleFeatures> features;
        compute_featurization(dag, params, &features);

        for (const auto &n : dag.nodes) {
            if (n.is_input) {
                continue;
            }
            for (size_t stage_idx = n.stages.size(); stage_idx > 0; stage_idx--) {
                const auto &s = n.stages[stage_idx - 1];
                const size_t num_schedule_features = ScheduleFeatures::num_features();
                const size_t num_pipeline_features = PipelineFeatures::num_features();
                const auto &sched_feat = features.get(&s);

                float buf[num_schedule_features + num_pipeline_features];
                // Save them as floats
                for (size_t i = 0; i < num_schedule_features; i++) {
                    buf[i] = sched_feat[i];
                }

                for (size_t i = 0; i < num_pipeline_features; i++) {
                    buf[i + num_schedule_features] = s.features[i];
                }

                out.write((const char *)buf, sizeof(buf));
            }
        }
    }

    bool calculate_cost(const FunctionDAG &dag, const MachineParams &params,
                        CostModel *cost_model, int64_t memory_limit, bool verbose = false) {
        StageMap<ScheduleFeatures> features;
        compute_featurization(dag, params, &features);

        cost = 0;

        if (verbose) {
            for (auto it = features.begin(); it != features.end(); it++) {
                auto &stage = *(it.key());
                const auto &feat = it.value();
                aslog(0) << "Schedule features for " << stage.stage.name() << "\n";
                feat.dump();
            }
        }

        internal_assert(cost_model);

        // Perform some addition pruning before burdening the cost model with silly states
        for (auto it = features.begin(); it != features.end(); it++) {
            if (!it.key()->node->is_wrapper) {  // It's OK to repeatedly stage data
                auto &feat = it.value();
                if (feat.points_computed_total + feat.inlined_calls > 8 * feat.points_computed_minimum) {
                    cost = 1e50;
                    return false;
                }
            }
        }

        // Avoid code size explosion from recursive inlining.
        if (root->max_inlined_calls() >= 256) {
            cost = 1e50;
            return false;
        }

        // Apply the hard limit on memory use
        if (memory_limit >= 0) {
            int64_t mem_used = (int64_t)features.begin().value().working_set_at_root;
            for (auto it = features.begin(); it != features.end(); it++) {
                if (it.key()->node->is_output ||
                    it.key()->node->is_input) {
                    // Not allocated by this pipeline
                    mem_used -= it.value().bytes_at_production;
                }
            }
            if (mem_used > memory_limit) {
                cost = 1e50;
                return false;
            }
        }

        // Tell the cost model about this state. It won't actually
        // evaluate it until we call evaluate_costs (or if it runs out
        // of internal buffer space), so that the evaluations can be
        // batched.
        cost_model->enqueue(dag, features, &cost);

        cost_calculations++;
        return true;
    }

    // Make a child copy of this state. The loop nest is const (we
    // make mutated copies of it, rather than mutating it), so we can
    // continue to point to the same one and so this is a cheap
    // operation.
    IntrusivePtr<State> make_child() const {
        State *s = new State;
        s->parent = this;
        s->root = root;
        s->cost = cost;
        s->num_decisions_made = num_decisions_made;
        return s;
    }

    // Generate the successor states to this state
    void generate_children(const FunctionDAG &dag,
                           const MachineParams &params,
                           CostModel *cost_model,
                           int64_t memory_limit,
                           std::function<void(IntrusivePtr<State> &&)> &accept_child) const {
        internal_assert(root.defined() && root->is_root());

        if (num_decisions_made == 2 * (int)dag.nodes.size()) {
            return;
        }

        int next_node = num_decisions_made / 2;
        int phase = num_decisions_made % 2;

        if (!may_subtile()) {
            // When emulating the older search space, we do all
            // parallelizing last, so that it is independent of the
            // tiling decisions.
            next_node = num_decisions_made % dag.nodes.size();
            phase = num_decisions_made / dag.nodes.size();
        }

        // Enumerate all legal ways to schedule the next Func
        const FunctionDAG::Node *node = &dag.nodes[next_node];
        for (const auto *e : node->outgoing_edges) {
            internal_assert(root->computes(e->consumer->node))
                << "Partially scheduled code doesn't compute " << e->consumer->name
                << ", which is one of the consumers of " << node->func.name();
        }

        if (node->is_input) {
            // We don't need to schedule nodes that represent inputs,
            // and there are no other decisions to be made about them
            // at this time.
            // aslog(0) << "Skipping over scheduling input node: " << node->func.name() << "\n";
            auto child = make_child();
            child->num_decisions_made++;
            accept_child(std::move(child));
            return;
        }

        if (!node->outgoing_edges.empty() && !root->calls(node)) {
            aslog(0) << "In state:\n";
            dump();
            aslog(0) << node->func.name() << " is consumed by:\n";
            for (const auto *e : node->outgoing_edges) {
                aslog(0) << e->consumer->name << "\n";
                aslog(0) << "Which in turn consumes:\n";
                for (const auto *e2 : e->consumer->incoming_edges) {
                    aslog(0) << "  " << e2->producer->func.name() << "\n";
                }
            }
            internal_error << "Pipeline so far doesn't use next Func: " << node->func.name() << "\n";
        }

        int num_children = 0;

        if (phase == 0) {
            // Injecting realizations
            {
                // 1) Inline it
                if (node->stages.size() == 1 && !node->is_output) {
                    auto child = make_child();
                    LoopNest *new_root = new LoopNest;
                    new_root->copy_from(*root);
                    new_root->inline_func(node);
                    child->root = new_root;
                    child->num_decisions_made++;
                    if (child->calculate_cost(dag, params, cost_model, memory_limit)) {
                        num_children++;
                        accept_child(std::move(child));
                    }
                }
            }

            // Some search-space pruning. If a node is pointwise, and
            // so are all its inputs and so is its sole output, and
            // inlining it is legal, just inline it. This saves time
            // on long chains of pointwise things.
            bool must_inline = (node->is_pointwise &&
                                (num_children > 0) &&
                                (node->outgoing_edges.size() == 1));
            if (must_inline) {
                for (const auto *e : node->stages[0].incoming_edges) {
                    must_inline &= e->producer->is_pointwise;
                }
                for (const auto *e : node->outgoing_edges) {
                    must_inline &= (e->consumer->node->is_pointwise ||
                                    e->consumer->node->is_boundary_condition);
                }
                if (must_inline) {
                    return;
                }
            }

            // Construct a list of plausible dimensions to vectorize
            // over. Currently all of them. TODO: Pre-prune the list
            // of sane dimensions to vectorize a Func over to reduce
            // branching factor.
            vector<int> vector_dims;
            if (!node->is_input && !node->is_output) {
                for (int v = 0; v < node->dimensions; v++) {
                    const auto &p = root->get_bounds(node)->region_computed(v);
                    if (p.extent() >= node->vector_size) {
                        vector_dims.push_back(v);
                    }
                }
            }

            // Outputs must be vectorized over their innermost
            // dimension, because we don't have control of the
            // storage. Infer which dimension(s) is(are) the innermost one(s) by
            // looking at the stride. Note that there can be more than one in
            // case some dimensions have an extent of 1.
            if (node->is_output && !node->func.output_buffers().empty()) {
                const Parameter &output = node->func.output_buffers()[0];
                int num_dims = output.dimensions();
                for (int i = 0; i < num_dims; ++i) {
                    const Expr stride = output.stride_constraint(i);
                    const int64_t *s = as_const_int(stride);
                    if (s && *s == 1) {
                        vector_dims.push_back(i);
                    }
                }
            }

            if (vector_dims.empty()) {
                // This can happen if the output strides aren't known, or if all
                // the dimensions are smaller than the vector size.
                // TBD: consider extending compute_in_tiles to support -1 as a
                // vector dim to indicate no vectorization.
                for (int v = 0; v < node->dimensions; v++) {
                    vector_dims.push_back(v);
                }
                // Handle the case of full reductions that generate a scalar.
                // We need at least one vector dimension to call cmopute_in_tiles
                // below.
                // TBD: figure out a better fallback strategy.
                if (vector_dims.empty()) {
                    vector_dims.push_back(0);
                }
            }

            // 2) Realize it somewhere
            for (int vector_dim : vector_dims) {
                auto tile_options = root->compute_in_tiles(node, nullptr, params, vector_dim, false);
                for (IntrusivePtr<const LoopNest> &n : tile_options) {
                    auto child = make_child();
                    child->root = std::move(n);
                    child->num_decisions_made++;
                    if (child->calculate_cost(dag, params, cost_model, memory_limit)) {
                        num_children++;
                        accept_child(std::move(child));
                    }
                }
            }
        } else {
            // We are parallelizing the loops of the func we just injected a realization for.

            bool should_parallelize = false;
            const vector<int64_t> *pure_size = nullptr;
            if (params.parallelism > 1) {
                for (auto &c : root->children) {
                    if (c->node == node && node->dimensions > 0) {
                        if (c->stage->index == 0) {
                            pure_size = &(c->size);
                        }
                        should_parallelize = true;
                    }
                }
            }

            if (!should_parallelize) {
                // The Func must be scalar, or not compute_root, or
                // we're not asking to use multiple cores.  Just
                // return a copy of the parent state
                num_children++;
                auto child = make_child();
                child->num_decisions_made++;
                accept_child(std::move(child));
            } else {
                internal_assert(pure_size);

                // Generate some candidate parallel task shapes.
                auto tilings = generate_tilings(*pure_size, node->dimensions - 1, 2, true);

                // We could also just parallelize the outer loop entirely
                std::vector<int64_t> ones;
                ones.resize(pure_size->size(), 1);
                tilings.emplace_back(std::move(ones));

                // Sort / filter the options
                struct Option {
                    vector<int64_t> tiling;
                    double idle_core_wastage;
                    bool entire;
                    bool operator<(const Option &other) const {
                        return idle_core_wastage < other.idle_core_wastage;
                    }
                    // Ensure we don't accidentally copy this type
                    Option() = default;
                    Option(Option &&) = default;
                    Option &operator=(Option &&) = default;
                    Option(const Option &) = delete;
                    Option &operator=(const Option &) = delete;
                };

                vector<Option> options;
                for (size_t i = 0; i < tilings.size(); i++) {
                    auto &t = tilings[i];
                    Option o;
                    o.entire = (i == tilings.size() - 1);

                    for (size_t j = 0; j < pure_size->size(); j++) {
                        t[j] = ((*pure_size)[j] + t[j] - 1) / t[j];
                    }
                    t.swap(o.tiling);

                    // Compute max idle cores across the other stages of the Func
                    int64_t min_total = 0, max_total = 0;
                    o.idle_core_wastage = 1;
                    for (const auto &c : root->children) {
                        if (c->node == node) {
                            int64_t total = 1;
                            for (auto &l : c->stage->loop) {
                                if (!l.rvar) {
                                    total *= o.tiling[l.pure_dim];
                                }
                            }
                            if (min_total != 0) {
                                min_total = std::min(min_total, total);
                            } else {
                                min_total = total;
                            }
                            max_total = std::max(max_total, total);
                            const double tasks_per_core = ((double)total) / params.parallelism;
                            o.idle_core_wastage = std::max(o.idle_core_wastage,
                                                           std::ceil(tasks_per_core) /
                                                               tasks_per_core);
                        }
                    }

                    // Filter out the less useful options
                    bool ok =
                        ((o.entire || min_total >= params.parallelism) &&
                         (max_total <= params.parallelism * 16));

                    if (!ok) {
                        continue;
                    }

                    options.emplace_back(std::move(o));
                }
                std::sort(options.begin(), options.end());

                // If none of the options were acceptable, don't
                // parallelize. This tends to happen for things like
                // compute_root color matrices.
                if (options.empty()) {
                    num_children++;
                    auto child = make_child();
                    child->num_decisions_made++;
                    accept_child(std::move(child));
                    return;
                }

                for (const auto &o : options) {
                    if (num_children >= 1 && (o.idle_core_wastage > 1.2 || !may_subtile())) {
                        // We have considered several options, and the
                        // remaining ones leave lots of cores idle.
                        break;
                    }

                    auto child = make_child();
                    LoopNest *new_root = new LoopNest;
                    new_root->copy_from(*root);
                    for (auto &c : new_root->children) {
                        if (c->node == node) {
                            if (may_subtile()) {
                                c = c->parallelize_in_tiles(params, o.tiling, new_root);
                            } else {
                                // We're emulating the old
                                // autoscheduler for an ablation, so
                                // emulate its parallelism strategy:
                                // just keep parallelizing outer loops
                                // until enough are parallel.
                                vector<int64_t> tiling = c->size;
                                int64_t total = 1;
                                for (size_t i = c->size.size(); i > 0; i--) {
                                    if (!c->stage->loop[i - 1].pure || total >= params.parallelism) {
                                        tiling[i - 1] = 1;
                                    }
                                    while (tiling[i - 1] > 1 &&
                                           total * tiling[i - 1] > params.parallelism * 8) {
                                        tiling[i - 1] /= 2;
                                    }
                                    total *= tiling[i - 1];
                                }
                                c = c->parallelize_in_tiles(params, tiling, new_root);
                            }
                        }
                    }
                    child->root = new_root;
                    child->num_decisions_made++;
                    if (child->calculate_cost(dag, params, cost_model, memory_limit)) {
                        num_children++;
                        accept_child(std::move(child));
                    }
                }
            }
        }

        if (num_children == 0) {
            aslog(0) << "Warning: Found no legal way to schedule "
                     << node->func.name() << " in the following State:\n";
            dump();
            // All our children died. Maybe other states have had
            // children. Carry on.
        }
    }

    void dump() const {
        aslog(0) << "State with cost " << cost << ":\n";
        root->dump("", nullptr);
        aslog(0) << schedule_source;
    }

    string schedule_source;

    // Apply the schedule represented by this state to a Halide
    // Pipeline. Also generate source code for the schedule for the
    // user to copy-paste to freeze this schedule as permanent artifact.
    void apply_schedule(const FunctionDAG &dag, const MachineParams &params) {
        StageMap<std::unique_ptr<LoopNest::StageScheduleState>> state_map;
        root->apply(LoopLevel::root(), state_map, params.parallelism, 0, nullptr, nullptr);

        std::ostringstream src;

        // Print handles for all the Funcs
        int i = (int)(dag.nodes.size() - 1);
        for (const auto &n : dag.nodes) {
            if (!n.is_input) {
                src << "Func " << n.func.name() << " = pipeline.get_func(" << i << ");\n";
            }
            i--;
        }

        // Gather all Vars and RVars so that we can declare them in the emitted source
        map<string, string> vars, rvars;
        for (auto &p : state_map) {
            for (auto &v : p.second->vars) {
                if (v.exists) {
                    if (v.var.is_rvar) {
                        rvars.emplace(v.var.name(), v.accessor);
                    } else {
                        vars.emplace(v.var.name(), v.accessor);
                    }
                }
            }
        }
        if (!vars.empty()) {
            for (const auto &p : vars) {
                if (p.second.empty()) {
                    src << "Var " << p.first << "(\"" << p.first << "\");\n";
                } else {
                    src << "Var " << p.first << "(" << p.second << ");\n";
                }
            }
        }
        if (!rvars.empty()) {
            for (const auto &p : rvars) {
                if (p.second.empty()) {
                    src << "RVar " << p.first << "(\"" << p.first << "\");\n";
                } else {
                    src << "RVar " << p.first << "(" << p.second << ");\n";
                }
            }
        }

        for (auto &p : state_map) {
            if (p.first->node->is_input) {
                continue;
            }

            Stage stage(p.first->stage);

            // Do all the reorders and pick which vars to
            // parallelize.
            vector<VarOrRVar> vars;
            int64_t parallel_tasks = 1;
            vector<VarOrRVar> parallel_vars;
            bool any_parallel_vars = false, any_parallel_rvars = false;
            for (auto it = p.second->vars.rbegin(); it != p.second->vars.rend(); it++) {
                if (!it->exists || it->extent == 1) {
                    continue;
                }
                if (!it->parallel) {
                    break;
                }
                any_parallel_rvars |= it->var.is_rvar;
                any_parallel_vars |= !it->var.is_rvar;
                parallel_tasks *= it->extent;
                parallel_vars.push_back(it->var);
            }

            if (p.second->vars.size() > 1) {
                p.second->schedule_source << "\n    .reorder(";
                bool first = true;
                for (auto &v : p.second->vars) {
                    if (v.exists) {
                        vars.push_back(v.var);
                        if (!first) {
                            p.second->schedule_source << ", ";
                        } else {
                            p.second->schedule_source << "{";
                        }
                        first = false;
                        p.second->schedule_source << v.var.name();
                    }
                }
                p.second->schedule_source << "})";
                stage.reorder(vars);
            }

            // Halide doesn't let you fuse an RVar with a Var, even if
            // they are both pure.
            bool can_fuse = !(any_parallel_vars && any_parallel_rvars);
            if (can_fuse) {
                for (size_t i = 1; i < parallel_vars.size(); i++) {
                    // Outermost, and next outermost. Preserve the inner
                    // name to not invalidate any compute_ats.
                    p.second->schedule_source << "\n    .fuse(" << parallel_vars[i].name()
                                              << ", " << parallel_vars[i - 1].name()
                                              << ", " << parallel_vars[i].name() << ")";
                    stage.fuse(parallel_vars[i], parallel_vars[i - 1], parallel_vars[i]);
                }
                if (!parallel_vars.empty()) {
                    p.second->schedule_source << "\n    .parallel(" << parallel_vars.back().name() << ")";
                    stage.parallel(parallel_vars.back());
                }
            } else {
                for (const auto &v : parallel_vars) {
                    p.second->schedule_source << "\n    .parallel(" << v.name() << ")";
                    stage.parallel(v);
                }
            }

            // Reorder the vector dimension innermost
            if (p.first->index == 0 && p.second->vector_dim > 0) {
                vector<Var> storage_vars = Func(p.first->node->func).args();
                for (int i = p.second->vector_dim; i > 0; i--) {
                    std::swap(storage_vars[i], storage_vars[i - 1]);
                }
                p.second->schedule_source << "\n    .reorder_storage(";
                bool first = true;
                for (auto v : storage_vars) {
                    if (!first) {
                        p.second->schedule_source << ", ";
                    }
                    first = false;
                    p.second->schedule_source << v.name();
                }
                p.second->schedule_source << ")";
                Func(p.first->node->func).reorder_storage(storage_vars);
            }

            // Dump the schedule source string
            src << p.first->name
                << p.second->schedule_source.str()
                << ";\n";
        }
        // Sanitize the names of things to make them legal source code.
        schedule_source = src.str();
        bool in_quotes = false;
        for (auto &c : schedule_source) {
            in_quotes ^= (c == '"');
            if (!in_quotes && c == '$') {
                c = '_';
            }
        }
    }
};

// Keep track of how many times we evaluated a state.
int State::cost_calculations = 0;

// A priority queue of states, sorted according to increasing
// cost. Never shrinks, to avoid reallocations.
// Can't use std::priority_queue because it doesn't support unique_ptr.
class StateQueue {
private:
    struct CompareStates {
        bool operator()(const IntrusivePtr<State> &a, const IntrusivePtr<State> &b) const {
            return a->cost > b->cost;
        }
    };

    std::vector<IntrusivePtr<State>> storage;
    size_t sz = 0;

public:
    void emplace(IntrusivePtr<State> &&s) {
        if (sz >= storage.size()) {
            storage.resize(std::max(sz * 2, (size_t)64));
        }
        internal_assert(sz < storage.size()) << sz << " " << storage.size() << "\n";
        storage[sz] = std::move(s);
        sz++;
        std::push_heap(storage.begin(), storage.begin() + sz, CompareStates{});
    }

    IntrusivePtr<State> pop() {
        internal_assert(sz <= storage.size()) << sz << " " << storage.size() << "\n";
        std::pop_heap(storage.begin(), storage.begin() + sz, CompareStates{});
        sz--;
        return std::move(storage[sz]);
    }

    const IntrusivePtr<State> &top() {
        return storage[0];
    }

    bool empty() const {
        return sz == 0;
    }

    size_t size() const {
        return sz;
    }

    void swap(StateQueue &other) {
        storage.swap(other.storage);
        std::swap(sz, other.sz);
    }

    IntrusivePtr<State> operator[](int idx) const {
        return storage[idx];
    }

    void resort() {
        std::make_heap(storage.begin(), storage.begin() + sz, CompareStates{});
    }

    void clear() {
        for (size_t i = 0; i < sz; i++) {
            storage[i] = IntrusivePtr<State>{};
        }
        sz = 0;
    }
};

// Configure a cost model to process a specific pipeline.
void configure_pipeline_features(const FunctionDAG &dag,
                                 const MachineParams &params,
                                 CostModel *cost_model) {
    cost_model->reset();
    cost_model->set_pipeline_features(dag, params);
}

// A single pass of coarse-to-fine beam search.
IntrusivePtr<State> optimal_schedule_pass(FunctionDAG &dag,
                                          const vector<Function> &outputs,
                                          const MachineParams &params,
                                          CostModel *cost_model,
                                          std::mt19937 &rng,
                                          int beam_size,
                                          int64_t memory_limit,
                                          int pass_idx,
                                          int num_passes,
                                          ProgressBar &tick,
                                          std::unordered_set<uint64_t> &permitted_hashes) {

    if (cost_model) {
        configure_pipeline_features(dag, params, cost_model);
    }

    StateQueue q, pending;

    // The initial state, with no decisions made
    {
        IntrusivePtr<State> initial{new State};
        initial->root = new LoopNest;
        q.emplace(std::move(initial));
    }

    int expanded = 0;

    std::function<void(IntrusivePtr<State> &&)> enqueue_new_children =
        [&](IntrusivePtr<State> &&s) {
            // aslog(0) << "\n** Generated child: ";
            // s->dump();
            // s->calculate_cost(dag, params, nullptr, true);

            // Each child should have one more decision made than its parent state.
            internal_assert(s->num_decisions_made == s->parent->num_decisions_made + 1);

            int progress = s->num_decisions_made * beam_size + expanded;
            size_t max_progress = dag.nodes.size() * beam_size * 2;

            // Update the progress bar
            tick.set(double(progress) / max_progress);
            s->penalized = false;

            // Add the state to the list of states to evaluate
            q.emplace(std::move(s));
        };

    string cyos_str = get_env_variable("HL_CYOS");

    // This loop is beam search over the sequence of decisions to make.
    for (int i = 0;; i++) {
        std::unordered_map<uint64_t, int> hashes;
        q.swap(pending);

        if (pending.empty()) {
            if ((false) && beam_size < 1000) {  // Intentional dead code. Extra parens to pacify clang-tidy.
                // Total mortality. Double the beam size and
                // restart. Disabled for now because total mortality
                // may indicate a bug.
                return optimal_schedule_pass(dag,
                                             outputs,
                                             params,
                                             cost_model,
                                             rng,
                                             beam_size * 2,
                                             memory_limit,
                                             pass_idx,
                                             num_passes,
                                             tick,
                                             permitted_hashes);
            } else {
                internal_error << "Ran out of legal states with beam size " << beam_size << "\n";
            }
        }

        if ((int)pending.size() > beam_size * 10000) {
            aslog(0) << "Warning: Huge number of states generated (" << pending.size() << ").\n";
        }

        expanded = 0;
        while (expanded < beam_size && !pending.empty()) {

            IntrusivePtr<State> state{pending.pop()};

            if (beam_size > 1 && num_passes > 1) {
                // We are doing coarse-to-fine beam search using the
                // hashing strategy mentioned in the paper.
                //
                // We will lazily apply cost penalties to the queue
                // according to structural uniqueness.
                if (!state->penalized) {
                    uint64_t h1 = state->structural_hash(pass_idx + 1);
                    uint64_t h0 = state->structural_hash(pass_idx - 1);
                    // We penalize the cost of a state proportionately
                    // to how many states we've already seen with that
                    // hash.
                    int penalty = ++hashes[h1];
                    if (pass_idx > 0 && !permitted_hashes.count(h0)) {
                        // It's possible to get yourself into a state
                        // where the only things in the beam that match
                        // the hash were quick-rejected due to details not
                        // captured in the hash, so we apply a huge
                        // penalty, but leave the impermissible state in
                        // the beam.
                        penalty += 10;
                    }
                    if (penalty > 1) {
                        state->penalized = true;
                        state->cost *= penalty;
                        // After penalizing this state, if it's no
                        // longer the best, defer it. We set the
                        // 'penalized' flag so that we know not to
                        // penalize and defer it again.
                        if (!pending.empty() && state->cost > pending.top()->cost) {
                            pending.emplace(std::move(state));
                            continue;
                        }
                    }
                }
            }

            // Random dropout
            if (pending.size() > 1 && random_dropout(rng, dag.nodes.size() * 2)) {
                continue;
            }

            if (state->num_decisions_made == 2 * (int)dag.nodes.size()) {
                // We've reached the end of the pass. The first state
                // must be the best, because we're pulling off a
                // priority queue.
                auto best = state;

                // Bless the reasonable stuff in the beam as
                // permissible states to visit again. We define
                // reasonable as having a cost no more than 20% higher
                // than the cost of the best thing. Only do this if
                // there are more coarse-to-fine passes yet to come.
                if (pass_idx + 1 < num_passes) {
                    int blessed = 0;
                    while (state->cost <= 1.2 * best->cost && blessed < beam_size) {
                        const State *s = state.get();
                        while (s) {
                            uint64_t h1 = s->structural_hash(pass_idx);
                            permitted_hashes.insert(h1);
                            s = s->parent.get();
                        }
                        if (pending.empty()) {
                            break;
                        }
                        state = pending.pop();
                        blessed++;
                    }
                }

                return best;
            }

            state->generate_children(dag, params, cost_model, memory_limit, enqueue_new_children);
            expanded++;
        }

        // Drop the other states unconsidered.
        pending.clear();

        if (cost_model) {
            // Now evaluate all the costs and re-sort them in the priority queue
            cost_model->evaluate_costs();
            q.resort();
        }

        if (cyos_str == "1") {
            // The user has set HL_CYOS, and wants to navigate the
            // search space manually.  Discard everything in the queue
            // except for the user-chosen option.
            aslog(0) << "\n--------------------\n";
            aslog(0) << "Select a schedule:\n";
            for (int choice_label = (int)q.size() - 1; choice_label >= 0; choice_label--) {
                auto state = q[choice_label];
                aslog(0) << "\n[" << choice_label << "]:\n";
                state->dump();
                state->calculate_cost(dag, params, cost_model, memory_limit, true);
            }
            cost_model->evaluate_costs();

            // Select next partial schedule to expand.
            int selection = -1;
            while (selection < 0 || selection >= (int)q.size()) {
                aslog(0) << "\nEnter selection: ";
                std::cin >> selection;
            }

            auto selected = q[selection];
            selected->dump();
            q.clear();
            q.emplace(std::move(selected));
        }
    }
}

// Performance coarse-to-fine beam search and return the best state found.
IntrusivePtr<State> optimal_schedule(FunctionDAG &dag,
                                     const vector<Function> &outputs,
                                     const MachineParams &params,
                                     CostModel *cost_model,
                                     std::mt19937 &rng,
                                     int beam_size,
                                     int64_t memory_limit) {

    IntrusivePtr<State> best;

    std::unordered_set<uint64_t> permitted_hashes;

    // If the beam size is one, it's pointless doing multiple passes.
    int num_passes = (beam_size == 1) ? 1 : 5;

    string cyos_str = get_env_variable("HL_CYOS");
    if (cyos_str == "1") {
        // If the user is manually navigating the search space, don't
        // ask them to do more than one pass.
        num_passes = 1;
    }

    string num_passes_str = get_env_variable("HL_NUM_PASSES");
    if (!num_passes_str.empty()) {
        // The user has requested a non-standard number of passes.
        num_passes = std::atoi(num_passes_str.c_str());
    }

    for (int i = 0; i < num_passes; i++) {
        ProgressBar tick;

        auto pass = optimal_schedule_pass(dag, outputs, params, cost_model,
                                          rng, beam_size, memory_limit,
                                          i, num_passes, tick, permitted_hashes);

        tick.clear();

        if (aslog::aslog_level() == 0) {
            aslog(0) << "Pass " << i << " of " << num_passes << ", cost: " << pass->cost << "\n";
        } else {
            aslog(0) << "Pass " << i << " result: ";
            pass->dump();
        }

        if (i == 0 || pass->cost < best->cost) {
            // Track which pass produced the lowest-cost state. It's
            // not necessarily the final one.
            best = pass;
        }
    }

    aslog(0) << "Best cost: " << best->cost << "\n";

    return best;
}

// The main entrypoint to generate a schedule for a pipeline.
void generate_schedule(const std::vector<Function> &outputs,
                       const Target &target,
                       const MachineParams &params,
                       AutoSchedulerResults *auto_scheduler_results) {
    aslog(0) << "generate_schedule for target=" << target.to_string() << "\n";

    // Start a timer
    HALIDE_TIC;

    State::cost_calculations = 0;

    // Get the seed for random dropout
    string seed_str = get_env_variable("HL_SEED");
    // Or use the time, if not set.
    int seed = (int)time(NULL);
    if (!seed_str.empty()) {
        seed = atoi(seed_str.c_str());
    }
    aslog(1) << "Dropout seed = " << seed << "\n";
    std::mt19937 rng((uint32_t)seed);

    // Get the beam size
    string beam_size_str = get_env_variable("HL_BEAM_SIZE");
    // Defaults to 32
    size_t beam_size = 32;
    if (!beam_size_str.empty()) {
        beam_size = atoi(beam_size_str.c_str());
    }

    string weights_in_path = get_env_variable("HL_WEIGHTS_DIR");
    string weights_out_path;  // deliberately empty

    string randomize_weights_str = get_env_variable("HL_RANDOMIZE_WEIGHTS");
    bool randomize_weights = randomize_weights_str == "1";

    string memory_limit_str = get_env_variable("HL_AUTOSCHEDULE_MEMORY_LIMIT");
    int64_t memory_limit = memory_limit_str.empty() ? (uint64_t)(-1) : std::atoll(memory_limit_str.c_str());

    // Analyse the Halide algorithm and construct our abstract representation of it
    FunctionDAG dag(outputs, params, target);
    if (aslog::aslog_level() > 0) {
        dag.dump();
    }

    // Construct a cost model to use to evaluate states. Currently we
    // just have the one, but it's an abstract interface, so others
    // can be slotted in for experimentation.
    std::unique_ptr<CostModel> cost_model = make_default_cost_model(weights_in_path, weights_out_path, randomize_weights);
    internal_assert(cost_model != nullptr);

    IntrusivePtr<State> optimal;

    // Run beam search
    optimal = optimal_schedule(dag, outputs, params, cost_model.get(), rng, beam_size, memory_limit);

    HALIDE_TOC;

    aslog(1) << "Cost evaluated this many times: " << State::cost_calculations << "\n";

    // Dump the schedule found
    aslog(1) << "** Optimal schedule:\n";

    // Just to get the debugging prints to fire
    optimal->calculate_cost(dag, params, cost_model.get(), memory_limit, aslog::aslog_level() > 0);

    // Apply the schedules to the pipeline
    optimal->apply_schedule(dag, params);

    // Print out the schedule
    if (aslog::aslog_level() > 0) {
        optimal->dump();
    }

    string schedule_file = get_env_variable("HL_SCHEDULE_FILE");
    if (!schedule_file.empty()) {
        user_warning << "HL_SCHEDULE_FILE is deprecated; use the schedule output from Generator instead\n";
        aslog(1) << "Writing schedule to " << schedule_file << "...\n";
        std::ofstream f(schedule_file);
        f << "// --- BEGIN machine-generated schedule\n"
          << optimal->schedule_source
          << "// --- END machine-generated schedule\n";
        f.close();
        internal_assert(!f.fail()) << "Failed to write " << schedule_file;
    }

    // Save the featurization, so that we can use this schedule as
    // training data (once we've benchmarked it).
    string feature_file = get_env_variable("HL_FEATURE_FILE");
    if (!feature_file.empty()) {
        user_warning << "HL_FEATURE_FILE is deprecated; use the featurization output from Generator instead\n";
        std::ofstream binfile(feature_file, std::ios::binary | std::ios_base::trunc);
        optimal->save_featurization(dag, params, binfile);
        binfile.close();
        internal_assert(!binfile.fail()) << "Failed to write " << feature_file;
    }

    if (auto_scheduler_results) {
        auto_scheduler_results->scheduler_name = "Adams2019";
        auto_scheduler_results->schedule_source = optimal->schedule_source;
        {
            std::ostringstream out;
            optimal->save_featurization(dag, params, out);
            auto_scheduler_results->featurization.resize(out.str().size());
            memcpy(auto_scheduler_results->featurization.data(), out.str().data(), out.str().size());
        }
    }
}

struct Adams2019 {
    void operator()(const Pipeline &p, const Target &target, const MachineParams &params, AutoSchedulerResults *results) {
        std::vector<Function> outputs;
        for (Func f : p.outputs()) {
            outputs.push_back(f.function());
        }
        Autoscheduler::generate_schedule(outputs, target, params, results);
    }
};

REGISTER_AUTOSCHEDULER(Adams2019)

// An alternative entrypoint for other uses
void find_and_apply_schedule(FunctionDAG &dag,
                             const std::vector<Function> &outputs,
                             const MachineParams &params,
                             CostModel *cost_model,
                             int beam_size,
                             int64_t memory_limit,
                             StageMap<ScheduleFeatures> *schedule_features) {

    std::mt19937 rng(12345);
    IntrusivePtr<State> optimal = optimal_schedule(dag, outputs, params, cost_model, rng, beam_size, memory_limit);

    // Apply the schedules
    optimal->apply_schedule(dag, params);

    if (schedule_features) {
        optimal->compute_featurization(dag, params, schedule_features);
    }
}

}  // namespace Autoscheduler

// Intrusive shared ptr helpers.
template<>
RefCount &ref_count<Autoscheduler::LoopNest>(const Autoscheduler::LoopNest *t) noexcept {
    return t->ref_count;
}

template<>
void destroy<Autoscheduler::LoopNest>(const Autoscheduler::LoopNest *t) {
    delete t;
}

template<>
RefCount &ref_count<Autoscheduler::State>(const Autoscheduler::State *t) noexcept {
    return t->ref_count;
}

template<>
void destroy<Autoscheduler::State>(const Autoscheduler::State *t) {
    delete t;
}

}  // namespace Internal
}  // namespace Halide
