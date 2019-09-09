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

  TODO: expose these settings by adding some means to pass args to
  generator plugins instead of environment vars.
*/
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

#include "Halide.h"
#include "ASLog.h"
#include "CostModel.h"
#include "DefaultCostModel.h"
#include "Featurization.h"
#include "FunctionDAG.h"
#include "PerfectHashMap.h"
#include "Errors.h"
#include "NetworkSize.h"
#include "AutoSchedule.h"

#ifdef _WIN32
#include <io.h>
#define _isatty isatty;
#endif

namespace Halide {
namespace Internal {
namespace Autoscheduler {

// How small should an innermost loop cluster be before you just
// entirely unroll the thing. Sized for an architecture with 16 vector
// registers.
const int kUnrollLimit = 12;

using std::string;
using std::vector;
using std::map;
using std::set;
using std::pair;

struct ProgressBar {
    void set(double progress) {
        if (!draw_progress_bar) return;
        counter++;
        const int bits = 11;
        if (counter & ((1 << bits) - 1)) return;
        const int pos = (int) (progress * 78);
        aslog(0) << '[';
        for (int j = 0; j < 78; j++) {
            if (j < pos) {
                aslog(0) << '.';
            } else if (j - 1 < pos) {
                aslog(0) << "/-\\|"[(counter >> bits) % 4];
            } else {
                aslog(0) << ' ';
            }
        }
        aslog(0) << ']';
        for (int j = 0; j < 80; j++) {
            aslog(0) << '\b';
        }
    }

    void clear() {
        if (counter) {
            for (int j = 0; j < 80; j++) aslog(0) << ' ';
            for (int j = 0; j < 80; j++) aslog(0) << '\b';
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
    if (random_dropout_threshold >= 100) return false;

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

// Get the HL_NO_SUBTILING environment variable. Purpose described above.
bool get_may_subtile() {
    string no_subtiling_str = get_env_variable("HL_NO_SUBTILING");
    if (no_subtiling_str == "1") {
        return false;
    } else {
        return true;
    }
}
bool may_subtile() {
    static bool b = get_may_subtile();
    return b;
}

// Given a multi-dimensional box of dimensionality d, generate a list
// of candidate tile sizes for it, logarithmically spacing the sizes
// using the given factor. If 'allow_splits' is false, every dimension
// must either be one, or the full extent of the box. This function is
// used to generate candidate tilings when tiling for
// producer-consumer fusion, or tiling for parallelism.
vector<vector<int64_t>> generate_tilings(const vector<int64_t> &s, int d, int factor, bool allow_splits) {
    vector<vector<int64_t>> result;
    if (d == -1) {
        result.push_back(vector<int64_t>());
    } else {
        vector<vector<int64_t>> v = generate_tilings(s, d - 1, factor, allow_splits);
        // If we're already generated too many tiling configurations
        // for the inner loops, search the outer loops with coarser
        // granularity.
        while (v.size() > (size_t)factor * 100) {
            factor *= 2;
        }

        for (auto &t : v) {
            bool is_full = false, is_one = false;
            // Skip trivial tilings
            if ((size_t)d == s.size() - 1) {
                is_one = is_full = true;
                for (int i = 0; i < d; i++) {
                    is_one &= (t[i] == 1);
                    is_full &= (t[i] == s[i]);
                }
            }
            t.push_back(0);
            if (!allow_splits) {
                if (!is_one) {
                    t.back() = 1;
                    result.push_back(t);
                }
                if (s[d] != 1 && !is_full) {
                    t.back() = s[d];
                    result.push_back(t);
                }
            } else {
                int max_inner = 0;
                for (int inner = 1; inner < s[d]; inner *= factor) {
                    int outer = (s[d] + inner - 1) / inner;
                    if (is_one && outer == 1) continue;
                    if (is_full && outer == s[d]) continue;
                    // Stop when we hit inner sizes that would do too much recompute
                    if (inner > 1 && inner * outer * 7 > s[d] * 8) break;
                    max_inner = inner;
                    t.back() = outer;
                    result.push_back(t);
                }
                for (int outer = 1; outer <= s[d]; outer *= factor) {
                    int inner = (s[d] + outer - 1) / outer;
                    if (is_one && outer == 1) continue;
                    if (is_full && outer == s[d]) continue;
                    // Stop when we get into the regime covered by the loop above.
                    if (outer > 1 && inner < max_inner * 2) break;
                    // Or when the wasted compute gets too bad.
                    if (inner * outer * 7 > s[d] * 8) break;
                    t.back() = outer;
                    result.push_back(t);
                }

                // The sequence above (in terms of the inner loop)
                // goes 1 2 4 8 16 ...  but 3 is an important inner
                // tiling factor for matrix multiply/gemm-type loops
                // which try to use 12 vector registers.
                int inner3 = 3;
                int outer3 = (s[d] + inner3 - 1) / inner3;
                if (factor == 2 && inner3 < s[d] && outer3 < s[d] && outer3 > 1) {
                    if (inner3 * outer3 * 7 <= s[d] * 8) {
                        t.back() = outer3;
                        result.push_back(t);
                    }
                }
            }
        }
    }
    return result;
}

template<typename T>
using NodeMap = PerfectHashMap<FunctionDAG::Node, T>;

template<typename T>
using StageMap = PerfectHashMap<FunctionDAG::Node::Stage, T>;

// We're going to do a tree search over possible schedules to find an
// optimal one. A tree search requires a state, and a function that
// gives you children of the state (with costs). The following struct
// represents the state, which is a partial schedule.
//
// A partial schedule is a tree. Each node is some portion of the for
// loop nest of some Func. If there are no children, it's the
// innermost set of loops. If there are children, it's a loop over
// tiles of that Func.
struct LoopNest {
    mutable RefCount ref_count;

    // The extents of this loop. Put another way, the number of tiles,
    // not the size of each tile.
    vector<int64_t> size;

    // The nodes inside the loop body
    vector<IntrusivePtr<const LoopNest>> children;

    // Funcs inlined into this inner loop, and the number of times
    // each is called. Only valid if children is empty.
    NodeMap<int64_t> inlined;

    // Funcs stored inside this loop
    set<const FunctionDAG::Node *> store_at;

    // The total bounds required of any given Func over all iterations
    // of this loop. In the paper, this is represented using the
    // little boxes to the left of the loop nest tree figures.
    mutable NodeMap<Bound> bounds;

    // The Func this loop nest belongs to
    const FunctionDAG::Node *node = nullptr;

    // The stage of the Func
    const FunctionDAG::Node::Stage *stage = nullptr;

    // Is this the innermost loop of this func (the SIMD loop)?
    bool innermost = false;

    // Are we permitted to tile this loop?
    bool tileable = false;

    // Is this the parallel outer loop?
    bool parallel = false;

    // What dimension is this Func vectorized over, in terms of the pure args of the Func?
    int vector_dim = -1;

    // Which loop corresponds to the innermost storage dimension and will be vectorized. -1 means none of them.
    int vectorized_loop_index = -1;

    void copy_from(const LoopNest &n) {
        size = n.size;
        children = n.children;
        inlined = n.inlined;
        store_at = n.store_at;
        bounds = n.bounds;
        node = n.node;
        stage = n.stage;
        innermost = n.innermost;
        tileable = n.tileable;
        parallel = n.parallel;
        vector_dim = n.vector_dim;
        vectorized_loop_index = n.vectorized_loop_index;
    };

    static void hash_combine(uint64_t &h, uint64_t next) {
        // From boost
        h ^= (next + 0x9e3779b9 + (h<<6) + (h>>2));
    }

    // Hash the loop structure and sizes up to a fixed depth. This is
    // used as the hash function for the coarse-to-fine beam search in
    // the paper.
    void structural_hash(uint64_t &h, int depth) const {
        if (depth < 0) return;

        // Which Funcs are store_at this level?
        for (const auto *n : store_at) {
            hash_combine(h, n->id);
        }

        hash_combine(h, -1);

        // Which Funcs are compute_at this level?
        for (const auto &c : children) {
            hash_combine(h, c->stage->id);
        }

        // Add a barrier to ensure that moving something from the last
        // compute_at to the first inlined doesn't result in the same
        // hash.
        hash_combine(h, -1);

        // Which Funcs are inlined at this level?
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            hash_combine(h, it.key()->id);
        }

        hash_combine(h, -1);

        if (depth > 0) {
            // What are the loop sizes of the children?
            for (const auto &c : children) {
                for (int64_t s : c->size) {
                    if (depth == 1) {
                        // Just take the most significant bit: is it one or not?
                        s = (s > 1) ? 1 : 0;
                    }
                    hash_combine(h, s);
                }
            }

            // Which dimension are we vectorized over?
            hash_combine(h, vectorized_loop_index);
        }

        if (depth > 1) {
            // Descend into children
            for (const auto &c : children) {
                c->structural_hash(h, depth - 2);
            }
        }
    }

    // How many funcs are scheduled inside this loop level. Used in
    // the structural hash.
    size_t funcs_realized_or_inlined() const {
        size_t count = inlined.size() + store_at.size();
        for (const auto &c : children) {
            count += c->funcs_realized_or_inlined();
        }
        return count;
    }

    // All of a stage's interesting locations in the loop nest. Used to help compute the featurization of a stage.
    struct Sites {
        const LoopNest *compute = nullptr;   // Its containing compute_at site
        const LoopNest *store = nullptr;     // Its containing store_at site
        const LoopNest *produce = nullptr;   // Its own outermost node
        const LoopNest *innermost = nullptr; // Its innermost node - usually a SIMD loop
        const LoopNest *task = nullptr;      // The parallel for loop it belongs to
        bool inlined = false;                // Is the Func inlined?
    };

    // Compute all the sites of interest for each pipeline stage
    void get_sites(StageMap<Sites> &sites,
                   const LoopNest *task = nullptr,
                   const LoopNest *parent = nullptr) const {
        if (!task && !is_root()) {
            task = this;
        }
        for (const auto &c : children) {
            c->get_sites(sites, task, this);
        }
        if (parent && node != parent->node) {
            auto &s = sites.get_or_create(stage);
            s.compute = parent;
            s.produce = this;
            s.task = task;
        }
        for (auto f : store_at) {
            for (const auto &s : f->stages) {
                sites.get_or_create(&s).store = this;
            }
        }
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            auto &s = sites.get_or_create(&(it.key()->stages[0]));
            s.inlined = true;
            s.compute = s.store = s.produce = s.innermost = this;
            s.task = task;
        }
        if (innermost) {
            sites.get_or_create(stage).innermost = this;
        }
    }

    // A helper for the working_set_at_task feature. Most features are
    // computed in the recursive pass 'compute_features' below, but
    // this one must be done in a second separate recursive pass.
    void set_working_set_at_task_feature(int64_t working_set,
                                         StageMap<ScheduleFeatures> *features) const {
        for (const auto &c : children) {
            c->set_working_set_at_task_feature(working_set, features);
            features->get(c->stage).working_set_at_task = working_set;
        }
    }

    // Do a recursive walk over the loop nest computing features to feed the cost model.
    void compute_features(const FunctionDAG &dag,
                          const MachineParams &params,
                          const StageMap<Sites> &sites,
                          int64_t instances,
                          int64_t parallelism,
                          const LoopNest *parent,
                          const LoopNest *grandparent,
                          const LoopNest &root,
                          int64_t *working_set,
                          StageMap<ScheduleFeatures> *features) const {

        int64_t working_set_here = 0;

        int64_t loop_instances = 1, parallel_tasks = 1;
        bool in_impure = false;
        for (int idx = (int)size.size() - 1; idx >= 0; idx--) {
            size_t i = size[idx];
            loop_instances *= i;
            if (stage->loop[idx].pure && !in_impure) {
                if (params.parallelism > 1 &&
                    (parallel || (parent->is_root() && parallel_tasks < params.parallelism))) {
                    // Either we've picked our parallel tiling, or
                    // it's not yet determined. Assume we'll not split
                    // any loops and just stop after we hit the
                    // required number of cores
                    parallel_tasks *= i;
                    // If we haven't picked out parallel tiling yet,
                    // assume that we'll target 8*cores when we do,
                    // which is a common rule of thumb.
                    if (!parallel && parallel_tasks > params.parallelism * 8) {
                        // We would split this loop
                        parallel_tasks = params.parallelism * 8;
                    }
                }
            } else if (i != 1) {
                in_impure = true;
            }
        }

        int64_t subinstances = instances * loop_instances;

        for (const auto *node : store_at) {
            // Figure out the features at the store_at level
            const auto &bounds = get_bounds(node);

            for (size_t s = 0; s < node->stages.size(); s++) {
                // TODO: Lift invariants from this loop. Most of it's the same for every stage.
                internal_assert(!node->is_input);
                ScheduleFeatures &feat = features->get_or_create(&(node->stages[s]));

                feat.num_realizations = subinstances;

                feat.points_computed_per_realization = 1;
                feat.num_scalars = feat.num_vectors = subinstances;
                bool vectorized = false;
                for (int i = 0; i < (int)node->stages[s].loop.size(); i++) {
                    const auto &p = bounds->loops(s, i);
                    int64_t extent = p.extent();
                    feat.points_computed_per_realization *= extent;
                    if (i == sites.get(&(node->stages[s])).produce->vectorized_loop_index) {
                        // Assumes that we're not going to split
                        // things such that non-native-width
                        // vectorization is a problem, except for the
                        // tail.
                        feat.num_vectors *= extent / node->stages[s].vector_size;
                        feat.num_scalars *= extent % node->stages[s].vector_size;
                        vectorized = true;
                    } else {
                        feat.num_vectors *= extent;
                        feat.num_scalars *= extent;
                    }
                }
                if (!vectorized) {
                    feat.num_vectors = 0;
                }
                feat.points_computed_total = feat.points_computed_per_realization * feat.num_realizations;

                feat.bytes_at_realization = node->bytes_per_point;
                for (int i = 0; i < node->dimensions; i++) {
                    const auto &p = bounds->region_computed(i);
                    feat.bytes_at_realization *= p.extent();
                }
                int64_t innermost_storage_extent = 1;
                int v = sites.get(&(node->stages[s])).produce->vector_dim;
                if (v >= 0 && node->dimensions > 0) {
                    innermost_storage_extent = bounds->region_computed(v).extent();
                }
                feat.innermost_bytes_at_realization = node->bytes_per_point * innermost_storage_extent;

                if (!is_root()) {
                    feat.bytes_at_task = feat.bytes_at_realization;
                    feat.innermost_bytes_at_task = feat.innermost_bytes_at_realization;
                }
            }
        }

        if (is_root()) {
            // TODO: This block of code is repeated below. Refactor
            for (const auto &c : children) {
                c->compute_features(dag, params, sites, subinstances, parallelism, this, parent, root, &working_set_here, features);
            }

            for (const auto *node : store_at) {
                auto &feat = features->get(&(node->stages[0]));
                working_set_here += feat.bytes_at_production;
            }
            for (const auto *node : store_at) {
                for (const auto &s : node->stages) {
                    auto &feat = features->get(&s);
                    feat.working_set_at_realization = working_set_here;
                }
            }
            for (const auto &c : children) {
                if (c->node != node) {
                    auto &feat = features->get(c->stage);
                    feat.working_set_at_production = working_set_here;
                }
            }

            // Figure out the root-level features for every Func
            for (auto it = features->begin(); it != features->end(); it++) {
                const auto *stage = it.key();
                const auto *node = stage->node;
                auto &feat = it.value();
                const auto &root_bounds = root.get_bounds(node);

                feat.bytes_at_root = node->bytes_per_point;
                for (int i = 0; i < node->dimensions; i++) {
                    const auto &p = root_bounds->region_computed(i);
                    feat.bytes_at_root *= p.extent();
                }

                feat.working_set_at_root = working_set_here;

                auto *p = sites.get(stage).produce;
                if (p) {
                    // Extent of the innermost dimension in the storage layout
                    int64_t innermost_storage_extent = 1;
                    int v = p->vector_dim;
                    if (v >= 0 && node->dimensions > 0) {
                        innermost_storage_extent = root_bounds->region_computed(v).extent();
                    }
                    feat.innermost_bytes_at_root = node->bytes_per_point * innermost_storage_extent;
                } else {
                    feat.innermost_bytes_at_root = 0;
                }

                feat.points_computed_minimum = 1;
                for (int i = 0; i < (int)stage->loop.size(); i++) {
                    const auto &p = root_bounds->loops(stage->index, i);
                    feat.points_computed_minimum *= p.extent();
                }

                if (node->stages.size() == 1 && !node->is_output) {
                    int64_t points_computed_minimum_if_inlined = 0;
                    for (auto *e : node->outgoing_edges) {
                        points_computed_minimum_if_inlined += features->get(e->consumer).points_computed_minimum * e->calls;
                    }
                    feat.points_computed_minimum = std::min(feat.points_computed_minimum, (double)points_computed_minimum_if_inlined);
                }
            }

            return;
        }

        int64_t subparallelism = parallel_tasks * parallelism;

        // Figure out the features at the compute_at level
        internal_assert(!stage->node->is_input);
        ScheduleFeatures &feat = features->get_or_create(stage);

        if (innermost) {
            if (vectorized_loop_index >= 0 && vectorized_loop_index < (int) size.size()) {
                feat.vector_size = size[vectorized_loop_index];
            } else {
                feat.vector_size = 1;
            }
            if (feat.vector_size == 1) {
                // They're all scalars
                feat.num_scalars += feat.num_vectors;
                feat.num_vectors = 0;
            }
        } else {
            // We want these features just outside the innermost loop,
            // so just set them at every level and let them get
            // progressively overwritten as we descend the loop nest
            // tree.
            size_t idx = 0;
            feat.innermost_loop_extent = 1;
            feat.innermost_pure_loop_extent = 1;
            for (const auto &l : stage->loop) {
                feat.innermost_loop_extent *= size[idx];
                if (!l.rvar) {
                    feat.innermost_pure_loop_extent *= size[idx];
                }
                idx++;
            }
        }

        const bool at_task = parent->is_root();
        const bool at_production = parent->node != node;
        const bool at_pure_production = at_production && stage->index == 0;

        if (at_task) {
            if (parallel) {
                const auto &bounds = get_bounds(node);
                feat.bytes_at_task = node->bytes_per_point;
                int64_t innermost_storage_extent = 1;
                for (int i = 0; i < node->dimensions; i++) {
                    int64_t outer = 1;
                    for (size_t l = 0; l < stage->loop.size(); l++) {
                        if (stage->loop[l].var == node->func.args()[i]) {
                            outer = size[l];
                            break;
                        }
                    }
                    const auto &p = bounds->region_computed(i);
                    int64_t extent = p.extent();
                    extent /= outer;
                    feat.bytes_at_task *= extent;
                    if (i == vector_dim) {
                        innermost_storage_extent = extent;
                    }
                }
                feat.innermost_bytes_at_task = node->bytes_per_point * innermost_storage_extent;
            } else {
                // How this loop will be parallelized is not yet
                // determined. Use optimistic values for the features.
                feat.bytes_at_task = (feat.bytes_at_realization + params.parallelism - 1) / params.parallelism;
                feat.innermost_bytes_at_task = std::min(feat.bytes_at_task, feat.innermost_bytes_at_realization);
            }

            feat.unique_bytes_read_per_task = 0;
            feat.unique_lines_read_per_task = 0;

            // We're at a parallel for loop. Check all the accesses
            // done by Funcs inside this loop to values computed
            // outside of it to figure out how much data we'll be
            // streaming onto the core.
            vector<const FunctionDAG::Edge *> pending;
            set<const FunctionDAG::Node *> done;
            for (const auto *e : stage->incoming_edges) {
                pending.push_back(e);
            }
            while (!pending.empty()) {
                const auto *e = pending.back();
                pending.pop_back();
                if (done.count(e->producer)) continue;
                done.insert(e->producer);
                const auto &site = sites.get(&(e->producer->stages[0]));
                if (site.store->is_root()) {
                    const auto &b = get_bounds(e->producer);
                    int64_t bytes = e->producer->bytes_per_point, lines = 1;
                    int64_t max_extent = 1;
                    int vector_dim = (e->producer->is_input ? 0 :
                                      site.produce != nullptr ? site.produce->vector_dim :
                                      -1);
                    for (int i = 0; i < e->producer->dimensions; i++) {
                        int64_t extent = b->region_required(i).extent();
                        max_extent = std::max(extent, max_extent);
                        bytes *= extent;
                        if (i != vector_dim) {
                            lines *= extent;
                        }
                    }
                    if (!e->producer->is_input && site.produce == nullptr) {
                        // We haven't scheduled the producer so we
                        // don't know the memory layout yet. Assume
                        // the best case.
                        lines /= max_extent;
                    }
                    feat.unique_bytes_read_per_task += bytes;
                    feat.unique_lines_read_per_task += lines;

                } else if (site.produce != nullptr) {
                    // Computation must be nested inside this task or inlined into it.
                    for (const auto &s : e->producer->stages) {
                        for (const auto *e2 : s.incoming_edges) {
                            pending.push_back(e2);
                        }
                    }
                }
            }
        }

        if (at_production) {
            feat.num_productions = instances;
            feat.inner_parallelism = parallel_tasks;
            feat.outer_parallelism = parallelism;
            feat.native_vector_size = stage->vector_size;

            const auto &bounds = parent->get_bounds(node);

            feat.bytes_at_production = node->bytes_per_point;
            for (int i = 0; i < node->dimensions; i++) {
                const auto &p = bounds->region_computed(i);
                feat.bytes_at_production *= p.extent();
            }
            int64_t innermost_storage_extent = 1;
            if (vector_dim >= 0 && node->dimensions > 0) {
                innermost_storage_extent = bounds->region_computed(vector_dim).extent();
            }
            feat.innermost_bytes_at_production = node->bytes_per_point * innermost_storage_extent;
        }

        // Recurse inwards
        for (const auto &c : children) {
            c->compute_features(dag, params, sites, subinstances, subparallelism, this, parent, root, &working_set_here, features);
        }
        for (const auto *node : store_at) {
            auto &feat = features->get(&(node->stages[0]));
            working_set_here += feat.bytes_at_production;
        }
        for (const auto *node : store_at) {
            for (const auto &s : node->stages) {
                auto &feat = features->get(&s);
                feat.working_set_at_realization = working_set_here;
            }
        }
        for (const auto &c : children) {
            if (c->node != node) {
                auto &feat = features->get(c->stage);
                feat.working_set_at_production = working_set_here;
            }
        }

        if (at_task) {
            set_working_set_at_task_feature(working_set_here, features);
        }

        if (at_production) {
            feat.working_set = working_set_here;
        }

        if (innermost) {
            bool parent_unrolled =
                (feat.innermost_pure_loop_extent <= kUnrollLimit &&
                 parent->node == node);

            if (parent_unrolled) {
                const auto &grandparent_bounds = grandparent->get_bounds(node);
                for (size_t i = 0; i < parent->size.size(); i++) {
                    if (!stage->loop[i].rvar) {
                        const auto &l = grandparent_bounds->loops(parent->stage->index, i);
                        parent_unrolled &= l.constant_extent();
                    }
                }
            }

            if (parent_unrolled) {
                feat.unrolled_loop_extent = feat.innermost_pure_loop_extent;
            } else {
                feat.unrolled_loop_extent = 1;
            }
        }

        *working_set += working_set_here;

        // Analyze all memory dependencies of this stage, looking
        // through any Funcs inlined into it. This is where we track
        // things like vector gathers.
        int64_t bytes_loaded = 0, lines_loaded = 0, allocation_bytes_loaded = 0;
        double num_dense_loads = 0, num_broadcasts = 0,
            num_gathers = 0, num_stride_2_loads = 0,
            num_stride_3_loads = 0, num_stride_4_loads = 0,
            num_loads = 0;
        if (innermost || at_production) { // These are the sites at which we compute load footprints
            // Pick the site at which we will compute the footprint relationship
            const auto &consumer_site = sites.get(stage);

            // The store_at location of the consumer
            const auto *consumer_store_site = innermost ? parent : consumer_site.store;

            // The parallel loop of the consumer
            const auto *consumer_task_site = consumer_site.task;

            int64_t consumer_instances = innermost ? instances : feat.num_realizations;
            internal_assert(consumer_instances != 0);

            vector<const FunctionDAG::Node::Stage *> pending;
            pending.emplace_back(stage);
            vector<pair<LoadJacobian, FunctionDAG::Node *>> jacobians;
            set<const FunctionDAG::Node *> done;
            while (!pending.empty()) {
                auto p = pending.back();
                pending.pop_back();
                const auto &next_edges = p->incoming_edges;
                for (const auto *e : next_edges) {
                    internal_assert(sites.contains(&(e->producer->stages[0])))
                        << "No site found for " << e->producer->func.name() << "\n";

                    const auto &site = sites.get(&(e->producer->stages[0]));

                    bool producer_has_been_scheduled = e->producer->is_input || (site.produce != nullptr);

                    if (innermost) {
                        if (e->consumer == stage) {
                            for (auto &j : e->load_jacobians) {
                                jacobians.emplace_back(j, e->producer);
                            }
                        } else {
                            // Consumer was inlined. Multiply the Jacobians to look through it.
                            decltype(jacobians) new_jacobians;
                            for (auto &j1 : jacobians) {
                                if (e->consumer->node == j1.second) {
                                    for (auto &j2 : e->load_jacobians) {
                                        LoadJacobian j = j2 * j1.first;
                                        new_jacobians.emplace_back(j, e->producer);
                                    }
                                } else {
                                    new_jacobians.emplace_back(std::move(j1));
                                }
                            }
                            jacobians.swap(new_jacobians);
                        }
                    }

                    if (site.inlined) {
                        // Recursively examine the inputs
                        pending.emplace_back(&(e->producer->stages[0]));
                        continue;
                    }

                    // The producer's compute_at site
                    const auto *producer_compute_site = site.compute;

                    // The producer's store_at site
                    const auto *producer_store_site = site.store;

                    // The region required of the producer at various sites.
                    const auto &bounds = consumer_store_site->get_bounds(e->producer);
                    const auto &task_bounds = consumer_task_site->get_bounds(e->producer);
                    const auto &producer_compute_bounds = producer_compute_site->get_bounds(e->producer);
                    const auto &producer_store_bounds = producer_store_site->get_bounds(e->producer);

                    // Compute memory footprints in terms of the
                    // number of contiguous lines, and the number of
                    // bytes.
                    int64_t footprint = e->producer->bytes_per_point;
                    int64_t compute_footprint = footprint;
                    int64_t store_footprint = footprint;
                    int64_t task_footprint = footprint;
                    int64_t line_footprint = 1;
                    int64_t compute_line_footprint = 1;
                    int64_t store_line_footprint = 1;
                    int64_t task_line_footprint = 1;

                    if (e->producer->is_input) {
                        // This node represents an input. Its sites
                        // should be at the root level.
                        internal_assert(producer_store_site->is_root());
                        internal_assert(producer_compute_site->is_root());
                    }

                    if (innermost) {

                        // Grab the Jacobians that describe the memory dependence
                        for (const auto &jac : jacobians) {
                            if (jac.second != e->producer) continue;
                            double n = jac.first.count();

                            // Classify them to figure out what's going on in the vector dimension.
                            bool vector_broadcast = true;
                            bool dense_vector_load = true;
                            bool stride_2_vector_load = true;
                            bool stride_3_vector_load = true;
                            bool stride_4_vector_load = true;
                            int producer_innermost_dim =
                                (e->producer->is_input ? 0 : // Assume default storage layout for inputs
                                 !producer_has_been_scheduled ? -1 :
                                 site.produce->vector_dim);
                            if (vectorized_loop_index >= 0) {
                                if (!producer_has_been_scheduled) {
                                    // Operate optimistically and just
                                    // see if *any* dimension of the
                                    // producer would make for a good
                                    // load.
                                    int count[5] = {0, 0, 0, 0, 0};
                                    for (int i = 0; i < e->producer->dimensions; i++) {
                                        auto stride = jac.first(i, vectorized_loop_index);
                                        // stride is a rational. Check to see if it's a small integer.
                                        if (stride == 0) count[0]++;
                                        else if (stride == 1) count[1]++;
                                        else if (stride == 2) count[2]++;
                                        else if (stride == 3) count[3]++;
                                        else if (stride == 4) count[4]++;
                                    }
                                    vector_broadcast = (count[0] == e->producer->dimensions);
                                    dense_vector_load = (count[0] == e->producer->dimensions - 1 && count[1] == 1);
                                    stride_2_vector_load = (count[0] == e->producer->dimensions - 1 && count[2] == 1);
                                    stride_3_vector_load = (count[0] == e->producer->dimensions - 1 && count[3] == 1);
                                    stride_4_vector_load = (count[0] == e->producer->dimensions - 1 && count[4] == 1);
                                } else {
                                    for (int i = 0; i < e->producer->dimensions; i++) {
                                        auto stride = jac.first(i, vectorized_loop_index);
                                        vector_broadcast &= stride == 0;
                                        if (i == producer_innermost_dim) {
                                            dense_vector_load &= stride == 1;
                                            stride_2_vector_load &= stride == 2;
                                            stride_3_vector_load &= stride == 3;
                                            stride_4_vector_load &= stride == 4;
                                        } else {
                                            dense_vector_load &= stride == 0;
                                            stride_2_vector_load &= stride == 0;
                                            stride_3_vector_load &= stride == 0;
                                            stride_4_vector_load &= stride == 0;
                                            // TODO: Check for strided
                                            // loads across non-innermost
                                            // dims, and use to count the
                                            // number of pages, cache
                                            // lines, cache conflict misses, etc.
                                        }
                                    }
                                }
                            }

                            // Is this load loop-invariant over an
                            // unrolled block? If so, we amortize the
                            // number of loads to account for
                            // LICM. This is the key performance
                            // optimization you get from unrolling the
                            // inner loop of a gemm or conv, so it's
                            // important to capture it in the
                            // featurization.
                            int64_t amortization = 1;
                            if (feat.unrolled_loop_extent > 1) {
                                for (size_t idx = 0; idx < stage->loop.size(); idx++) {
                                    if (!stage->loop[idx].rvar) {
                                        bool loop_invariant = true;
                                        for (int i = 0; i < e->producer->dimensions; i++) {
                                            if (!(jac.first(i, idx) == 0)) {
                                                loop_invariant = false;
                                                break;
                                            }
                                        }
                                        if (loop_invariant) {
                                            amortization *= parent->size[idx];
                                        }
                                    }
                                }
                            }
                            n /= amortization;

                            num_loads += n;
                            if (vector_broadcast) {
                                num_broadcasts += n;
                            } else if (dense_vector_load) {
                                num_dense_loads += n;
                            } else if (stride_2_vector_load) {
                                num_stride_2_loads += n;
                            } else if (stride_3_vector_load) {
                                num_stride_3_loads += n;
                            } else if (stride_4_vector_load) {
                                num_stride_4_loads += n;
                            } else {
                                num_gathers += n;
                            }
                        }
                    }

                    // Already dealt with the footprints for this producer via some other path
                    if (done.find(e->producer) != done.end()) {
                        continue;
                    }

                    done.insert(e->producer);

                    // Now look at the shapes of the regions read from
                    // the producer at various sites.
                    int64_t max_extent = 1, max_compute_extent = 1, max_store_extent = 1, max_task_extent = 1;
                    for (int i = 0; i < e->producer->dimensions; i++) {
                        auto p = bounds->region_required(i);
                        auto compute_p = producer_compute_bounds->region_computed(i);
                        auto store_p = producer_store_bounds->region_required(i);
                        auto task_p = task_bounds->region_required(i);

                        // Check some invariants
                        internal_assert(store_p.min() <= store_p.max()) << store_p.min() << " " << store_p.max() << "\n";
                        internal_assert(compute_p.min() <= compute_p.max()) << compute_p.min() << " " << compute_p.max() << "\n";
                        internal_assert(task_p.min() <= task_p.max()) << task_p.min() << " " << task_p.max() << "\n";

                        int64_t extent = p.extent();
                        int64_t compute_extent = compute_p.extent();
                        int64_t store_extent = store_p.extent();
                        int64_t task_extent = task_p.extent();

                        max_extent = std::max(extent, max_extent);
                        max_compute_extent = std::max(compute_extent, max_compute_extent);
                        max_store_extent = std::max(store_extent, max_store_extent);
                        max_task_extent = std::max(task_extent, max_task_extent);

                        footprint *= extent;
                        compute_footprint *= compute_extent;
                        store_footprint *= store_extent;
                        task_footprint *= task_extent;

                        bool dense = ((e->producer->is_input && i == 0) ||
                                      (site.produce != nullptr && i == site.produce->vector_dim));
                        if (!dense) {
                            line_footprint *= extent;
                            compute_line_footprint *= compute_extent;
                            store_line_footprint *= store_extent;
                            task_line_footprint *= task_extent;
                        }
                    }

                    if (!producer_has_been_scheduled) {
                        // Optimistically assume it gets vectorized
                        // along whatever dimension makes these
                        // numbers the smallest.
                        line_footprint /= max_extent;
                        compute_line_footprint /= max_compute_extent;
                        store_line_footprint /= max_store_extent;
                        task_line_footprint /= max_task_extent;
                    }

                    int64_t store_instances_per_consumption = 1;

                    if (producer_has_been_scheduled && !e->producer->is_input) {
                        const auto &producer_feat = features->get_or_create(&(e->producer->stages[0]));

                        if (producer_feat.num_realizations) {
                            // The producer's realization is nested inside this Func's realization
                            const int64_t producer_store_instances = producer_feat.num_realizations;
                            if (producer_store_instances > consumer_instances) {
                                store_instances_per_consumption = producer_store_instances / consumer_instances;
                            }
                        }
                    }

                    allocation_bytes_loaded += compute_footprint;

                    if (store_instances_per_consumption > 1) {
                        // The producer is nested inside the consumer
                        bytes_loaded += store_footprint;
                        // Due to folding, the actual buffer size is smaller than the bounds at the store level
                        lines_loaded += store_line_footprint;
                    } else {
                        // The consumer is consuming some portion of a larger producer computed earlier
                        bytes_loaded += footprint;
                        lines_loaded += line_footprint;
                    }

                    // We compute (but never use) these; computing them is cheap,
                    // so let's leave in for future reference, but mark as 'ignore me'
                    // to avoid clang-tidy warnings.
                    (void) compute_line_footprint;
                    (void) task_line_footprint;
                }
            }
        }

        if (at_production) {
            // Properties of the realization, but the values are
            // computable at the production site because that's where
            // the consumers are.
            internal_assert(bytes_loaded >= 0) << "Negative bytes loaded: " << bytes_loaded << "\n";
            feat.allocation_bytes_read_per_realization = allocation_bytes_loaded;
            feat.unique_bytes_read_per_realization = bytes_loaded;
            feat.unique_lines_read_per_realization = lines_loaded;

            if (!at_pure_production) {
                // Also pessimistically assume this update definition relies on the entirety of the produced region so far.
                // TODO: This overbills scatters, or writes to a sub-window.
                internal_assert(bytes_loaded >= 0) << "Negative bytes at production: " << feat.bytes_at_production << "\n";
                feat.unique_bytes_read_per_realization += feat.bytes_at_production;
                feat.unique_lines_read_per_realization += feat.bytes_at_production / feat.innermost_bytes_at_production;
                feat.allocation_bytes_read_per_realization += feat.bytes_at_production;
            }
        }

        if (innermost) {
            feat.points_computed_per_production = subinstances / feat.num_productions;
            // Halide codegens strided loads for small strides as a
            // large dense vector load and a cheap swizzle. ARM even
            // has instructions that do this for free on load
            // (e.g. vld4).
            feat.vector_loads_per_vector = (num_dense_loads +
                                            2 * num_stride_2_loads +
                                            3 * num_stride_3_loads +
                                            4 * num_stride_4_loads);
            feat.scalar_loads_per_vector = num_broadcasts + feat.vector_size * num_gathers;
            feat.scalar_loads_per_scalar = num_loads;
            if (stage->index > 0) {
                // Assume at update definitions we do a self-load
                feat.vector_loads_per_vector++;
                feat.scalar_loads_per_scalar++;
            }
            feat.unique_bytes_read_per_vector = bytes_loaded;
            feat.unique_lines_read_per_vector = lines_loaded;
        }

        // Track features for inlined Funcs
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            const auto *f = it.key();
            internal_assert(f);
            auto &inlined_feat = features->get_or_create(&(f->stages[0]));
            inlined_feat.inlined_calls += it.value() * subinstances;
            inlined_feat.num_vectors += it.value() * feat.num_vectors;
            inlined_feat.num_scalars += it.value() * feat.num_scalars;
            inlined_feat.native_vector_size = stage->vector_size;
            if (inlined_feat.vector_size > 0) {
                inlined_feat.vector_size = std::min(inlined_feat.vector_size, (double)stage->vector_size);
            } else {
                inlined_feat.vector_size = feat.vector_size;
            }
            if (inlined_feat.innermost_pure_loop_extent > 0) {
                inlined_feat.innermost_pure_loop_extent =
                    std::min(inlined_feat.innermost_pure_loop_extent,
                             feat.innermost_pure_loop_extent);
            } else {
                inlined_feat.innermost_pure_loop_extent = feat.innermost_pure_loop_extent;
            }
            inlined_feat.inner_parallelism = 1;
            inlined_feat.outer_parallelism = parallelism;
        }
    }

    bool is_root() const {
        // The root is the sole node without a Func associated with
        // it.
        return node == nullptr;
    }

    // Set the region required of a Func at this site.
    const Bound &set_bounds(const FunctionDAG::Node *f, BoundContents *b) const {
        return bounds.emplace(f, b);
    }

    // Get the region required of a Func at this site, from which we
    // know what region would be computed if it were scheduled here,
    // and what its loop nest would be.
    const Bound &get_bounds(const FunctionDAG::Node *f) const {
        if (bounds.contains(f)) {
            const Bound &b = bounds.get(f);
            // Expensive validation for debugging
            // b->validate();
            return b;
        }
        auto bound = f->make_bound();

        // Compute the region required
        if (f->is_output && is_root()) {
            internal_assert(f->outgoing_edges.empty()) << "Outputs that access other outputs not yet supported\n";
            // It's an output. Use the bounds estimate.
            for (int i = 0; i < f->dimensions; i++) {
                bound->region_required(i) = f->estimated_region_required[i];
            }
        } else {
            internal_assert(!f->outgoing_edges.empty())
                << "No consumers of " << f->func.name()
                << " at loop over " << (is_root() ? "root" : node->func.name()) << '\n';
            auto init = Span::empty_span();
            for (int i = 0; i < f->dimensions; i++) {
                bound->region_required(i) = init;
            }

            for (const auto *e : f->outgoing_edges) {
                // Ignore consumers outside of this loop nest
                if (!is_root() &&
                    (stage != e->consumer) &&
                    !stage->downstream_of(*(e->consumer->node))) {
                    continue;
                }
                const auto &c_bounds = get_bounds(e->consumer->node);

                // Get the concrete sizes of the consuming loop
                const auto *consumer_loop = &(c_bounds->loops(e->consumer->index, 0));

                // Use the bounds relationship between the nodes to
                // map from the consumer's loop to the required region
                // of the producer.
                e->expand_footprint(consumer_loop, &(bound->region_required(0)));
            }
        }

        // Given a required region of this producer, use the bounds
        // analysis to figure out what region actually gets
        // computed. For most funcs, these are the same. Some things,
        // like histograms or scans, you can only really compute all
        // of at once.
        f->required_to_computed(&(bound->region_required(0)), &(bound->region_computed(0)));

        // Finally, figure out what loop nests will be used to compute
        // this region.
        for (int i = 0; i < (int)f->stages.size(); i++) {
            f->loop_nest_for_region(i, &(bound->region_computed(0)), &(bound->loops(i, 0)));
        }

        const Bound &b = set_bounds(f, bound);
        // b->validate();
        return b;
    }

    // Recursively print a loop nest representation to stderr
    void dump(string prefix, const LoopNest *parent) const {
        if (!is_root()) {
            // code elsewhere in this clause assumes that non-root
            // LoopNests have a parent
            internal_assert(parent != nullptr);

            aslog(0) << prefix << node->func.name();
            prefix += " ";

            for (size_t i = 0; i < size.size(); i++) {
                aslog(0) << " " << size[i];
                // The vectorized loop gets a 'v' suffix
                if (innermost && i == (size_t) vectorized_loop_index) {
                    aslog(0) << 'v';
                }
                // Loops that have a known constant size get a
                // 'c'. Useful for knowing what we can unroll.
                if (parent->get_bounds(node)->loops(stage->index, i).constant_extent()) {
                    aslog(0) << 'c';
                }
            }

            // Uncomment when debugging the representative loop bounds selected.
            /*
            const auto &bounds = get_bounds(node);
            for (size_t i = 0; i < size.size(); i++) {
                const auto &p = bounds->loops(stage->index, i);
                aslog(0) << " [" << p.first << ", " << p.second << "]";
            }
            */

            aslog(0) << " (" << vectorized_loop_index << ", " << vector_dim << ")";
        }

        if (tileable) {
            aslog(0) << " t";
        }
        if (innermost) {
            aslog(0) << " *\n";
        } else if (parallel) {
            aslog(0) << " p\n";
        } else {
            aslog(0) << '\n';
        }
        for (auto p : store_at) {
            aslog(0) << prefix << "realize: " << p->func.name() << '\n';
        }
        for (size_t i = children.size(); i > 0; i--) {
            children[i-1]->dump(prefix, this);
        }
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            aslog(0) << prefix << "inlined: " << it.key()->func.name() << " " << it.value() << '\n';
        }
    }

    // Does this loop nest access the given Func
    bool calls(const FunctionDAG::Node *f) const {
        for (const auto &c : children) {
            if (c->calls(f)) return true;
        }
        for (const auto *e : f->outgoing_edges) {
            if (e->consumer == stage) {
                return true;
            }
            if (inlined.contains(e->consumer->node)) {
                return true;
            }
        }
        return false;
    }

    // What is the maximum number of inlined calls to a Func that
    // occur within this loop. Used to prune states that would
    // generate too much code.
    int64_t max_inlined_calls() const {
        int64_t result = 0;
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            result = std::max(result, it.value());
        }
        for (const auto &c : children) {
            result = std::max(result, c->max_inlined_calls());
        }
        return result;
    }

    // Does this loop nest access an input buffer? Used to select
    // trail strategies when splitting loops. We don't want to read
    // out of bounds on inputs, even if we don't intend to use the
    // values read. It could create annoying assertion failures for
    // the user. It's OK to read out of range of the values computed
    // on internal Funcs though. Allocation bounds inference just pads
    // out the bounds so that it won't fault.
    bool accesses_input_buffer() const {
        for (const auto &c : children) {
            if (c->accesses_input_buffer()) return true;
        }
        if (is_root()) return false;

        auto check = [&](const FunctionDAG::Node::Stage *s) {
            for (const auto *e : s->incoming_edges) {
                if (e->producer->is_input) return true;
            }

            for (int t = 0; t < (int)PipelineFeatures::ScalarType::NumScalarTypes; t++) {
                if (s->features.op_histogram[(int)PipelineFeatures::OpType::ImageCall][t] > 0) return true;
            }
            return false;
        };

        if (check(stage)) return true;
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            if (check(&(it.key()->stages[0]))) return true;
        }
        return false;
    }

    // Does this loop nest contain a computation of the given Func.
    bool computes(const FunctionDAG::Node *f) const {
        if (f == node) {
            return true;
        }
        if (inlined.contains(f)) {
            return true;
        }
        for (const auto &c : children) {
            if (c->computes(f)) return true;
        }
        return false;
    }

    // Above here most methods query the loop nest. Below we have
    // methods that mutate the loop nest.

    // Inline a Func into all consumers within this loop.
    void inline_func(const FunctionDAG::Node *f) {
        // Inline it into the children
        for (size_t i = 0; i < children.size(); i++) {
            if (children[i]->calls(f)) {
                std::unique_ptr<LoopNest> new_child{new LoopNest};
                new_child->copy_from(*children[i]);
                new_child->inline_func(f);
                children[i] = new_child.release();
            }
        }

        // Inline it here if there are any direct calls
        if (innermost) {
            int64_t calls = 0;
            for (const auto *e : f->outgoing_edges) {
                if (inlined.contains(e->consumer->node)) {
                    calls += inlined.get(e->consumer->node) * e->calls;
                }
                if (e->consumer == stage) {
                    calls += e->calls;
                }
            }
            if (calls) {
                inlined.insert(f, calls);
            }
        }
    }

    // Compute a Func at this site.
    void compute_here(const FunctionDAG::Node *f, bool tileable, int v) {
        const auto &bounds = get_bounds(f);

        if (!may_subtile()) {
            // If we are restricting ourselves to the Mullapudi et al
            // scheduling space, then once something is computed here
            // we may not subtile this loop.
            this->tileable = false;
        }

        for (int s = (int)f->stages.size() - 1; s >= 0; s--) {
            LoopNest *node = new LoopNest;
            node->node = f;
            node->stage = &f->stages[s];
            node->innermost = true;
            node->vectorized_loop_index = -1;
            node->tileable = tileable && (is_root() || may_subtile());
            // Set up a bound for the inside of the
            // loop. computed/required is still the full region, but
            // the loop nest will be a single representative point.
            auto single_point = bounds->make_copy();
            size_t loop_dim = f->stages[s].loop.size();
            node->size.resize(loop_dim);

            int64_t total_extent = 1;
            int64_t vector_size = 1;
            for (size_t i = 0; i < loop_dim; i++) {
                const auto &l = bounds->loops(s, i);
                // Initialize the loop nest
                node->size[i] = l.extent();
                total_extent *= node->size[i];

                // Use the first loop iteration to represent the inner
                // loop. We'll shift it to a later one once we decide
                // on vectorization.
                single_point->loops(s, i) = Span(l.min(), l.min(), true);

                internal_assert(l.max() >= l.min()) << i << " " << l.max() << " " << l.min() << "\n";

                if (f->dimensions &&
                    node->size[i] >= 1 &&
                    f->stages[s].loop[i].var == f->func.args()[v]) {
                    node->vectorized_loop_index = (int)i;
                    vector_size = (int64_t)(node->stage->vector_size);
                    single_point->loops(s, i).set_extent(vector_size);
                    node->size[i] += vector_size - 1;
                    node->size[i] /= vector_size;

                    // Shift the loops along by some multiple of the
                    // vector size, to pick a more representative vector
                    // than the first. We use the middle-most.
                    int64_t shift = vector_size * (node->size[i] / 2);
                    single_point->loops(s, i).translate(shift);
                } else {
                    int64_t shift = node->size[i] / 2;
                    single_point->loops(s, i).translate(shift);
                }
            }

            // Leave region required blank inside the computation of a Func
            node->set_bounds(f, std::move(single_point));
            node->vector_dim = v;

            if (node->vectorized_loop_index >= 0) {
                // Split off the single vector as an inner loop nest.
                node->innermost = false;

                LoopNest *one_vector = new LoopNest;
                one_vector->node      = node->node;
                one_vector->stage     = node->stage;
                one_vector->tileable  = false;
                one_vector->vectorized_loop_index = node->vectorized_loop_index;
                one_vector->vector_dim = v;
                one_vector->size.resize(loop_dim, 1);
                one_vector->innermost = true;
                auto b = node->get_bounds(f)->make_copy();
                // Set the region computed inside this node to be the first vector lane
                b->loops(s, node->vectorized_loop_index).set_extent(1);
                one_vector->set_bounds(f, b);
                one_vector->size[node->vectorized_loop_index] = vector_size;

                node->children.emplace_back(one_vector);
            }
            children.emplace_back(node);
        }
    }

    // Parallelize this loop according to the given tiling.
    IntrusivePtr<const LoopNest> parallelize_in_tiles(const MachineParams &params,
                                                      const vector<int64_t> &tiling,
                                                      const LoopNest *parent) const {

        // Split this loop and move factors to the inner loop
        LoopNest *inner = new LoopNest, *outer = new LoopNest;
        inner->node      = outer->node      = node;
        inner->stage     = outer->stage     = stage;
        inner->tileable  = outer->tileable  = tileable && may_subtile();
        inner->vector_dim = outer->vector_dim = vector_dim;
        inner->vectorized_loop_index = outer->vectorized_loop_index = vectorized_loop_index;
        outer->size = size;
        outer->innermost = false;
        outer->parallel = true;
        outer->tileable = may_subtile();

        // First make an inner loop representing a 1x1x1... tile
        inner->size.resize(size.size(), 1);
        inner->innermost = innermost;
        inner->children = children;
        inner->inlined = inlined;
        inner->bounds = bounds;
        inner->store_at = store_at;

        auto b = inner->get_bounds(node)->make_copy();

        // Then move factors from the outer loop to the inner loop
        auto parent_bounds = parent->get_bounds(node);

        for (size_t i = 0; i < stage->loop.size(); i++) {
            int l = stage->loop[i].pure_dim;

            int64_t outer_extent;
            if (l >= 0) {
                internal_assert(l < (int)tiling.size()) << l << " " << tiling.size() << "\n";
                outer_extent = tiling[l];
            } else {
                // RVars are moved inwards
                outer_extent = 1;
            }

            inner->size[i] = (outer->size[i] + outer_extent - 1) / outer_extent;

            // Recompute the outer size given the selected inner size
            outer_extent = (outer->size[i] + inner->size[i] - 1) / inner->size[i];

            outer->size[i] = outer_extent;
            const auto &p = parent_bounds->loops(stage->index, i);
            int64_t min = p.min();
            int64_t extent = p.extent();
            extent = (extent + outer_extent - 1) / outer_extent;

            // Pick a better representative loop iteration for the
            // inner loops.
            min += (outer_extent / 2) * extent;
            bool compile_time_constant_bounds = p.constant_extent() || ((outer_extent > 1) && stage->loop[i].pure);
            b->loops(stage->index, i) = Span(min, min + extent - 1, compile_time_constant_bounds);
        }
        outer->set_bounds(node, b);

        outer->children.emplace_back(inner);
        return outer;
    }

    // Return all possible ways to compute f in tiles somewhere within
    // this loop nest.
    vector<IntrusivePtr<const LoopNest>> compute_in_tiles(const FunctionDAG::Node *f,
                                                          const LoopNest *parent,
                                                          const MachineParams &params,
                                                          int v,
                                                          bool in_realization) const {
        internal_assert(f);

        vector<IntrusivePtr<const LoopNest>> result;

        // Some pruning to not waste time on terrible states
        if (parent) {
            const auto &bounds_here = get_bounds(f);
            const auto &bounds_at_parent = parent->get_bounds(f);

            // Don't descend into loops that break our ability to
            // vectorize if we could have vectorized one level up.
            const auto &p = bounds_here->region_computed(v);
            const auto &p_parent = bounds_at_parent->region_computed(v);
            int64_t e = p.extent();
            int64_t ep = p_parent.extent();
            if (ep >= f->vector_size && e < f->vector_size) return result;

            // Don't descend into loops if the bounds required don't
            // shrink.
            int64_t total_here = 1, total_at_parent = 1;
            for (int i = 0; i < f->dimensions; i++) {
                const auto &range_here = bounds_here->region_computed(i);
                const auto &range_at_parent = bounds_at_parent->region_computed(i);
                total_here *= range_here.extent();
                total_at_parent *= range_at_parent.extent();
            }
            if (total_here >= total_at_parent) return result;

        }

        // Figure out which child we can fuse this into
        int child = -1;
        bool called_by_multiple_children = false;
        for (int i = 0; i < (int)children.size(); i++) {
            if (children[i]->calls(f)) {
                if (child != -1) {
                    called_by_multiple_children = true;
                }
                child = i;
            }
        }

        // Place the computation directly inside this loop (provided it's not a SIMD loop)
        if (!innermost &&
            (!in_realization ||
             size.empty() ||
             vector_dim == -1 ||
             size[vector_dim] == 1)) {

            std::unique_ptr<LoopNest> r{new LoopNest};
            r->copy_from(*this);
            r->compute_here(f, true, v);
            if (!in_realization) {
                r->store_at.insert(f);
            } else {
                r->tileable = false;
            }
            result.emplace_back(r.release());
        }

        if (f->is_output) {
            // Outputs must be compute_root, so we're done.
            return result;
        }

        if (tileable) {
            // Code elsewhere in this clause assumes we have a parent if we are tileable
            internal_assert(parent != nullptr);

            // Generate a list of tile sizes to try
            auto tilings = generate_tilings(size, (int)(size.size() - 1), 2, !in_realization);

            if (tilings.size() > 10000) {
                aslog(0) << "Warning: lots of tilings: " << tilings.size() << "\n";
            }

            for (auto t : tilings) {
                if (parallel) {
                    const auto &l = stage->loop;
                    // More pruning. Skip root-level tilings that
                    // would leave too many cores idle, and root-level
                    // tilings that would force serialization of
                    // dimensions we have decided to parallelize over
                    // in an earlier pass.
                    int total = 1;
                    size_t idx = 0;
                    for (auto s : t) {
                        if (l[idx].pure) {
                            total *= s;
                        }
                        idx++;
                    }

                    const double tasks_per_core = (double)total / params.parallelism;
                    const double idle_cores = std::ceil(tasks_per_core) / tasks_per_core;
                    if (idle_cores > 1.1) continue;
                }

                // Tile this loop and place the computation at some coarser granularity
                LoopNest *inner = new LoopNest, *outer = new LoopNest;
                inner->node      = outer->node      = node;
                inner->stage     = outer->stage     = stage;
                inner->tileable  = outer->tileable  = tileable && may_subtile();
                inner->vector_dim = outer->vector_dim = vector_dim;
                inner->vectorized_loop_index = outer->vectorized_loop_index = vectorized_loop_index;
                outer->size = size;
                outer->innermost = false;
                outer->parallel = parallel;
                inner->parallel = false;

                // First make an inner loop representing a 1x1x1... tile
                inner->size.resize(size.size(), 1);
                inner->innermost = innermost;
                inner->children = children;
                inner->inlined = inlined;
                inner->bounds = bounds;
                inner->store_at = store_at;


                {
                    auto b = inner->get_bounds(node)->make_copy();

                    // Then move factors from the outer loop to the inner loop
                    auto parent_bounds = parent->get_bounds(node);

                    for (size_t i = 0; i < t.size(); i++) {
                        int64_t outer_extent = t[i];
                        inner->size[i] = (outer->size[i] + outer_extent - 1) / outer_extent;
                        outer->size[i] = outer_extent;
                        const auto &p = parent_bounds->loops(stage->index, i);
                        int64_t min = p.min();
                        int64_t original_extent = p.extent();
                        int64_t inner_extent = (original_extent + outer_extent - 1) / outer_extent;
                        // Pick a more representative loop iteration
                        min += (outer_extent / 2) * inner_extent;
                        bool compile_time_constant_extent =
                            (p.constant_extent() || outer_extent > 1) &&
                            (inner_extent == 1 || outer_extent == 1 || stage->index == 0);
                        b->loops(stage->index, i) = Span(min, min + inner_extent - 1, compile_time_constant_extent);
                    }

                    // Region_{computed/required} on outer is now
                    // wrong, but it doesn't matter because consumers
                    // only look at the loops in get_bounds. Still,
                    // this is weird.
                    outer->set_bounds(node, b);
                }

                if (!in_realization) {
                    outer->store_at.insert(f);
                }
                outer->children.emplace_back(inner);

                // HACK
                // bool may_slide = false;
                bool may_slide = (!in_realization &&
                                  f->stages.size() == 1);
                if (may_slide) {
                    // Store here, but compute further in. Currently
                    // don't have to worry about the constraints this
                    // places on parallelism, as we forced all the
                    // parallelism to the outer loop.
                    auto opts = inner->compute_in_tiles(f, outer, params, v, true);
                    for (IntrusivePtr<const LoopNest> &n : opts) {
                        LoopNest *store_at_outer_compute_further_in = new LoopNest;
                        store_at_outer_compute_further_in->copy_from(*outer);
                        store_at_outer_compute_further_in->children.pop_back();
                        store_at_outer_compute_further_in->children.emplace_back(std::move(n));
                        result.emplace_back(store_at_outer_compute_further_in);
                    }
                }

                // Site the computation inside the outer loop
                outer->compute_here(f, true, v);
                outer->tileable &= !in_realization;
                result.emplace_back(outer);
            }
        }

        if (child >= 0 && !called_by_multiple_children && !in_realization &&
            (may_subtile() || is_root())) {
            // Push the Func further inwards in the loop nest

            // See if it's appropriate to slide over this loop Can't
            // slide at the root level if we intend to parallelize it.
            // HACK
            // bool may_slide = false;
            bool may_slide = (params.parallelism == 1) || !is_root();

            const auto &c = children[child];
            int num_ones = 0;
            for (size_t i = 0; i < c->size.size(); i++) {
                int64_t s = c->size[i];
                num_ones += (s == 1) ? 1 : 0;
            }

            // Some pruning:

            // Only slide over single-dimensional loops
            may_slide &= num_ones == ((int)c->size.size() - 1);

            // Don't slide funcs with update stages
            may_slide &= f->stages.size() == 1;

            // Don't slide over the vector dimension
            may_slide &= (c->vectorized_loop_index == -1 ||
                          c->size[c->vectorized_loop_index] == 1);

            for (int store_here = 0; store_here < 2; store_here++) {
                if (store_here && !may_slide) {
                    // We place all our parallel loops at the root
                    // level, so this would constrain parallelism.
                    continue;
                }
                if (is_root() && num_ones == (int)c->size.size() && params.parallelism > 1) {
                    // Don't fuse into serial loops, or we could never parallelize this Func.
                    continue;
                }
                auto opts = children[child]->compute_in_tiles(f, this, params, v, store_here);
                for (IntrusivePtr<const LoopNest> &n : opts) {
                    // (Only valid if one child calls f) Push the
                    // computation into the child. Possibly leaving
                    // the storage out here.
                    LoopNest *r = new LoopNest;
                    r->copy_from(*this);
                    if (store_here) {
                        r->store_at.insert(f);
                    }
                    r->children[child] = n;
                    result.emplace_back(r);
                }
            }
        }

        return result;
    }


    // Below here we have methods that apply a schedule to a Halide pipeline.

    // A model of the state of the loop nest of a Func while applying
    // Halide's scheduling directives.

    // Note that StageScheduleState is movable-but-not-copyable thanks
    // to its ostringstream member.
    struct StageScheduleState {
        // How much parallelism do we need to exploit with this Func?
        double num_cores = 0;

        // Which storage dimension is vectorized? We need to reorder it innermost
        int vector_dim = -1;
        int vectorized_loop_index = -1;

        // The various Vars and RVars used for scheduling a Func.
        struct FuncVar {
            // The top-level var or rvar this was split off from
            VarOrRVar orig;

            // This var.
            VarOrRVar var;

            // Source code to access this Var/RVar. Used for printing
            // valid Halide source for this schedule.
            string accessor;

            // Our estimate of the extent of this var. This is exact
            // when constant_extent flag is true.
            int64_t extent = 0;

            // Which index in the symbolic loop nest does this var
            // belong to.
            size_t index = 0;

            // Some flags.
            bool innermost_pure_dim = false,
                outermost = false,
                parallel = false,
                exists = false,
                pure = false,
                constant_extent = false;
            FuncVar() : orig(Var()), var(Var()) {}
        };

        // In order from innermost to outermost. Each group of d is one tiling level.
        vector<FuncVar> vars;

        std::ostringstream schedule_source;
    };

    // Apply the schedule represented by this loop nest to a Halide pipeline.
    void apply(LoopLevel here,
               StageMap<std::unique_ptr<StageScheduleState>> &state_map,
               double num_cores,
               int depth,
               const LoopNest *parent,
               const LoopNest *compute_site) const {
        if (is_root()) {
            for (auto &c : children) {
                Func(c->node->func).compute_root();
                c->apply(LoopLevel::root(), state_map, num_cores, 1, this, c.get());
                if (c->stage->index == 0) {
                    auto &state = state_map.get(c->stage);
                    state->schedule_source << "\n    .compute_root()";
                    // TODO: Omitting logic for printing store_root() assumes everything store_root is also compute root
                }
            }
        } else {
            // Code elsewhere in this clause assumes we have a parent if we are non-root
            internal_assert(parent != nullptr);

            if (parent->node != node) {
                compute_site = this;
            }

            const auto &symbolic_loop = stage->loop;
            const auto &parent_bounds = parent->get_bounds(node);
            if (!state_map.contains(stage)) {
                StageScheduleState *state = new StageScheduleState;
                state->num_cores = num_cores;
                state->vector_dim = vector_dim;
                state->vectorized_loop_index = vectorized_loop_index;
                for (size_t i = 0; i < symbolic_loop.size(); i++) {
                    StageScheduleState::FuncVar fv;
                    const auto &l = symbolic_loop[i];
                    fv.var = VarOrRVar(l.var, !l.pure);
                    fv.orig = fv.var;
                    fv.accessor = l.accessor;
                    const auto &p = parent_bounds->loops(stage->index, i);
                    fv.extent = p.extent();
                    fv.constant_extent = p.constant_extent();
                    fv.outermost = true;
                    fv.parallel = l.pure && parallel;
                    fv.exists = true;
                    fv.pure = l.pure;
                    fv.index = i;
                    fv.innermost_pure_dim = (i == (size_t) vectorized_loop_index);
                    state->vars.push_back(fv);
                }
                // Bubble the innermost pure dimension to the front of the pure dimensions
                for (int i = vectorized_loop_index - 1;
                     i >= 0 && state->vars[i].pure; i--) {
                    std::swap(state->vars[i], state->vars[i+1]);
                }
                state_map.emplace(stage, std::unique_ptr<StageScheduleState>(state));
            }
            auto &state = *(state_map.get(stage));

            // The getter for grabbing Func handles is reverse topological order
            Stage s = Func(node->func);
            if (stage->index > 0) {
                s = Func(node->func).update(stage->index - 1);
            }

            if (stage->index == 0 && parent->node != node) {
                // Pick a memory type
                double bytes = node->bytes_per_point;
                for (int i = 0; i < node->dimensions; i++) {
                    const auto &p = parent_bounds->region_computed(i);
                    bytes *= p.extent();
                }
                if (bytes < 64000 && depth > 2) {
                    // If it's probably a small allocation, and it's
                    // made more than once, use stack-scoped
                    // storage. Otherwise let the compiler pick heap
                    // or stack as it likes.
                    Func(node->func).store_in(MemoryType::Stack);
                    state.schedule_source << "\n    .store_in(MemoryType::Stack)";
                }
            }

            // Pick a tail strategy for any splits of pure vars. RVars always use guardwithif
            auto pure_var_tail_strategy = TailStrategy::Auto;
            if (!compute_site->accesses_input_buffer() && !node->is_output) {
                // Roundup is lowest overhead, provided it doesn't
                // expand the bounds read on the input or written on
                // the output. However, you can only really use it on
                // pure stages that don't access the input anywhere in
                // their loop nest.
                pure_var_tail_strategy = TailStrategy::RoundUp;
            } else if (stage->index == 0) {
                // Pure stages that access the input use shiftinwards
                pure_var_tail_strategy = TailStrategy::ShiftInwards;
            } else {
                // For pure vars in update stages that access the
                // input, it's not safe to round up or redundantly
                // recompute
                pure_var_tail_strategy = TailStrategy::GuardWithIf;
            }

            if (!size.empty()) {
                if (innermost) {
                    if (vectorized_loop_index >= 0) {
                        size_t i = 0;
                        while (!state.vars[i].innermost_pure_dim) i++;
                        auto &v = state.vars[i];
                        internal_assert(v.innermost_pure_dim && v.exists) << v.var.name() << "\n";
                        // Is the result of a split
                        state.schedule_source
                            << "\n    .vectorize(" << v.var.name() << ")";
                        s.vectorize(v.var);
                    }
                } else {
                    // Grab the innermost loop for this node
                    const LoopNest *innermost_loop = this, *child = nullptr;
                    while (!innermost_loop->innermost) {
                        for (const auto &c : innermost_loop->children) {
                            if (c->node == node) {
                                if (!child) {
                                    child = c.get();
                                }
                                innermost_loop = c.get();
                                break;
                            }
                        }
                    }

                    // Do the implied splits
                    vector<StageScheduleState::FuncVar> new_inner;
                    for (size_t i = 0; i < symbolic_loop.size(); i++) {
                        StageScheduleState::FuncVar v;
                        StageScheduleState::FuncVar &parent = state.vars[i];

                        int64_t factor = (parent.extent + size[parent.index] - 1) / size[parent.index];
                        int64_t innermost_size = innermost_loop->size[parent.index];

                        if (child && parent.innermost_pure_dim) {
                            // Ensure the split is a multiple of the
                            // vector size. With all these rounded
                            // divs going on it can drift.
                            factor = ((factor + innermost_size - 1) / innermost_size) * innermost_size;
                        }

                        if (child && innermost_size > factor) {
                            factor = innermost_size;
                        }

                        if (!parent.exists || factor == 1) {
                            v.exists = false;
                            v.extent = 1;
                        } else if (size[parent.index] == 1 && !(child &&
                                                                child->innermost &&
                                                                parent.innermost_pure_dim &&
                                                                parent.var.name() == parent.orig.name())) {
                            // Not split in this dimension
                            v = parent;
                            v.parallel = false;
                            parent.exists = false;
                            parent.extent = 1;
                        } else {
                            VarOrRVar inner(Var(parent.var.name() + "i"));
                            if (parent.var.is_rvar) {
                                inner = RVar(parent.var.name() + "i");
                            }

                            auto tail_strategy = pure_var_tail_strategy;
                            // If it's an RVar, or not the outermost split and we're in an update, we need a guard with if instead.
                            if (parent.var.is_rvar || (stage->index != 0 && !parent.outermost)) {
                                tail_strategy = TailStrategy::GuardWithIf;
                            }

                            if (factor > parent.extent && tail_strategy == TailStrategy::ShiftInwards) {
                                // Don't shift all the way off the image.
                                tail_strategy = TailStrategy::GuardWithIf;
                            }

                            s.split(parent.var, parent.var, inner, (int)factor, tail_strategy);
                            state.schedule_source
                                << "\n    .split("
                                << parent.var.name() << ", "
                                << parent.var.name() << ", "
                                << inner.name() << ", "
                                << factor << ", "
                                << "TailStrategy::" << tail_strategy << ")";
                            v = parent;
                            parent.extent = size[parent.index];
                            v.constant_extent = (tail_strategy != TailStrategy::GuardWithIf);
                            v.var = inner;
                            v.accessor.clear();
                            v.extent = factor;
                            v.parallel = false;
                            v.outermost = false;
                        }
                        new_inner.push_back(v);
                    }

                    if (child->innermost) {
                        // Maybe do some unrolling

                        int64_t product_of_pure_loops = 1;
                        bool all_pure_loops_constant_size = true;
                        for (size_t i = 0; i < symbolic_loop.size(); i++) {
                            if (state.vars[i].pure) {
                                product_of_pure_loops *= state.vars[i].extent;
                                all_pure_loops_constant_size &= state.vars[i].constant_extent;
                            }
                        }

                        if (product_of_pure_loops <= kUnrollLimit && all_pure_loops_constant_size) {
                            // There's a hope we can fit anything compute-at this level into registers if we fully unroll
                            // TODO: 16 should be the number of vector registers in the architecture
                            std::stable_sort(state.vars.begin(), state.vars.begin() + symbolic_loop.size(),
                                             [](const StageScheduleState::FuncVar &a, const StageScheduleState::FuncVar &b) {
                                                 return a.pure && !b.pure;
                                             });

                            for (size_t i = 0; i < symbolic_loop.size(); i++) {
                                if (state.vars[i].pure && state.vars[i].exists && state.vars[i].extent > 1) {
                                    s.unroll(state.vars[i].var);
                                    state.schedule_source << "\n    .unroll(" << state.vars[i].var.name() << ")";
                                }
                            }
                        }
                    }

                    bool found = false;
                    for (const auto &v : state.vars) {
                        if (!v.exists) continue;
                        here = LoopLevel(node->func, v.var);
                        found = true;
                        break;
                    }
                    if (!found) {
                        here = LoopLevel(node->func, Var::outermost());
                    }
                    // internal_assert(found) << "Could not find appropriate compute_at location for children of " << node->func.name() << "\n";
                    state.vars.insert(state.vars.begin(), new_inner.begin(), new_inner.end());
                }
            }
            if (innermost) {
                internal_assert(store_at.empty());
                internal_assert(children.empty());
                return;
            }


            for (auto f : store_at) {
                Func(f->func).store_at(here);
            }
            for (auto s : size) {
                num_cores /= s;
            }
            here.lock();
            string loop_level;
            if (here.is_root()) {
                loop_level = "_root()";
            } else {
                loop_level = "_at(" + here.func() + ", " + here.var().name() + ")";
            }
            for (auto &c : children) {
                if (c->node != node) {
                    Func(c->node->func).compute_at(here);
                }
                c->apply(here, state_map, num_cores, depth + 1, this, compute_site);
                if (c->node != node && c->stage->index == 0) {
                    auto &state = *(state_map.get(c->stage));
                    state.schedule_source << "\n    .compute" << loop_level;
                }
            }
            for (auto f : store_at) {
                bool computed_here = false;
                for (auto &c : children) {
                    if (c->node == f) {
                        computed_here = true;
                        break;
                    }
                }
                if (!computed_here) {
                    auto &state = *(state_map.get(&(f->stages[0])));
                    state.schedule_source << "\n    .store" << loop_level;
                }
            }
        }
    }

};

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
            compute_loop_nest_parents(p, c.get(), depth+1);
        }
    }

    const LoopNest *deepest_common_ancestor(const map<const LoopNest *, pair<const LoopNest *, int>> &parent,
                                            const LoopNest *a, const LoopNest *b) {
        if (a->is_root()) return a;
        if (b->is_root()) return b;
        if (a == b) return a;

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
            if (a == b) return a;
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
                if (!l) l = consumer_site.compute;
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
            if (n.is_input) continue;
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

    bool calculate_cost(const FunctionDAG &dag, const MachineParams &params, CostModel *cost_model, bool verbose = false) {
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
            if (!it.key()->node->is_wrapper) { // It's OK to repeatedly stage data
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

        int num_stages = (int)features.size();

        Runtime::Buffer<float> schedule_features;

        // Tell the cost model about this state. It won't actually
        // evaluate it until we call evaluate_costs (or if it runs out
        // of internal buffer space), so that the evaluations can be
        // batched.
        cost_model->enqueue(num_stages, &schedule_features, &cost);

        // index of current stage whose features we are reading
        int stage = 0;
        // load schedule features into input buffer
        for (const auto &n : dag.nodes) {

            // Inputs are computed outside of the pipeline and don't count.
            if (n.is_input) continue;

            // The remaining stage are not yet
            // scheduled. Optimistically assume their internal costs
            // will not depend on the decisions made already, so
            // there's no point adding it on to the total because it's
            // the same across all states.  An underestimate of the
            // cost for loading from these unscheduled stages is
            // already baked into the scheduled stages that consume
            // them.
            if (stage >= num_stages) break;

            // Load up the schedule features for all stages of this Func.
            for (auto it = n.stages.rbegin(); it != n.stages.rend(); it++) {
                internal_assert(features.contains(&*it)) << n.func.name() << "\n";
                const auto &feat = features.get(&*it);
                for (size_t i = 0; i < ScheduleFeatures::num_features(); i++) {
                    schedule_features(i, stage) = feat[i];
                }
                stage += 1;
            }
        }
        // Check we considered everything we were supposed to.
        internal_assert(stage == num_stages);

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
                           std::function<void(IntrusivePtr<State> &&)> &accept_child) const {
        internal_assert(root.defined() && root->is_root());

        if (num_decisions_made == 2*(int)dag.nodes.size()) {
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
            internal_error << "Pipeline so far doesn't use next Func: " << node->func.name() << '\n';
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
                    if (child->calculate_cost(dag, params, cost_model)) {
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
            // storage. TODO: Go inspect to see which dimension has a
            // stride==1 constraint instead of assuming 0.
            if (vector_dims.empty()) {
                vector_dims.push_back(0);
            }

            // 2) Realize it somewhere
            for (int vector_dim : vector_dims) {
                auto tile_options = root->compute_in_tiles(node, nullptr, params, vector_dim, false);
                for (IntrusivePtr<const LoopNest> &n : tile_options) {
                    auto child = make_child();
                    child->root = std::move(n);
                    child->num_decisions_made++;
                    if (child->calculate_cost(dag, params, cost_model)) {
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

                    if (!ok) continue;

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
                                    if (!c->stage->loop[i-1].pure || total >= params.parallelism) {
                                        tiling[i-1] = 1;
                                    }
                                    while (tiling[i-1] > 1 &&
                                           total * tiling[i-1] > params.parallelism * 8) {
                                        tiling[i-1] /= 2;
                                    }
                                    total *= tiling[i-1];
                                }
                                c = c->parallelize_in_tiles(params, tiling, new_root);
                            }
                        }
                    }
                    child->root = new_root;
                    child->num_decisions_made++;
                    if (child->calculate_cost(dag, params, cost_model)) {
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
            if (p.first->node->is_input) continue;

            Stage stage(p.first->stage);

            // Do all the reorders and pick which vars to
            // parallelize.
            vector<VarOrRVar> vars;
            int64_t parallel_tasks = 1;
            vector<VarOrRVar> parallel_vars;
            bool any_parallel_vars = false, any_parallel_rvars = false;
            for (auto it = p.second->vars.rbegin(); it != p.second->vars.rend(); it++) {
                if (!it->exists || it->extent == 1) continue;
                if (!it->parallel) break;
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
                        }
                        first = false;
                        p.second->schedule_source << v.var.name();
                    }
                }
                p.second->schedule_source << ")";
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
                                              << ", " << parallel_vars[i-1].name()
                                              << ", " << parallel_vars[i].name() << ")";
                    stage.fuse(parallel_vars[i], parallel_vars[i-1], parallel_vars[i]);
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
                    std::swap(storage_vars[i], storage_vars[i-1]);
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
            if (!in_quotes && c == '$') c = '_';
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
    const int pipeline_feat_size = head1_w * head1_h;
    // We ignore the first seven pipeline features in the cost
    // model. It's just a mask of which types are in use.
    static_assert(sizeof(PipelineFeatures) - 7 * sizeof(int) ==
                  sizeof(int) * pipeline_feat_size,
                  "Incorrect size for pipeline features");
    int num_stages = 0;
    for (const auto &n : dag.nodes) {
        if (!n.is_input) num_stages += (int)n.stages.size();
    }
    Runtime::Buffer<float> pipeline_features(head1_w, head1_h, num_stages);
    int stage = 0;
    for (const auto &n : dag.nodes) {
        if (n.is_input) continue;
        for (auto it = n.stages.rbegin(); it != n.stages.rend(); it++) {
            const auto &s = *it;
            const int *pipeline_feats = (const int *)(&(s.features)) + 7;
            // skip the first 7 features
            for (int i = 0; i < pipeline_feat_size; i++) {
                int x = i/7;
                int y = i%7;
                pipeline_features(x, y, stage) = pipeline_feats[i];
            }
            stage += 1;
        }
    }
    internal_assert(stage == num_stages);
    cost_model->set_pipeline_features(pipeline_features, params.parallelism);
}

// A single pass of coarse-to-fine beam search.
IntrusivePtr<State> optimal_schedule_pass(FunctionDAG &dag,
                                          vector<Function> outputs,
                                          const MachineParams &params,
                                          CostModel *cost_model,
                                          std::mt19937 &rng,
                                          int beam_size,
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
    for (int i = 0; ; i++) {
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

            IntrusivePtr<State> state {pending.pop()};

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

            if (state->num_decisions_made == 2*(int)dag.nodes.size()) {
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
                        if (pending.empty()) break;
                        state = pending.pop();
                        blessed++;
                    }
                }

                return best;
            }

            state->generate_children(dag, params, cost_model, enqueue_new_children);
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
                state->calculate_cost(dag, params, cost_model, true);
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
                                     vector<Function> outputs,
                                     const MachineParams &params,
                                     CostModel *cost_model,
                                     std::mt19937 &rng,
                                     int beam_size) {

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
            rng, beam_size, i, num_passes, tick, permitted_hashes);

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
    aslog(1) << "Dropout seed = " << seed << '\n';
    std::mt19937 rng((uint32_t) seed);

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
    optimal = optimal_schedule(dag, outputs, params, cost_model.get(), rng, beam_size);

    HALIDE_TOC;

    aslog(1) << "Cost evaluated this many times: " << State::cost_calculations << '\n';

    // Dump the schedule found
    aslog(1) << "** Optimal schedule:\n";

    // Just to get the debugging prints to fire
    optimal->calculate_cost(dag, params, cost_model.get(), aslog::aslog_level() > 0);

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
        auto_scheduler_results->scheduler_name = "apps/autoscheduler/AutoSchedule";  // TODO: find a better name (https://github.com/halide/Halide/issues/4057)
        auto_scheduler_results->schedule_source = optimal->schedule_source;
        {
            std::ostringstream out;
            optimal->save_featurization(dag, params, out);
            auto_scheduler_results->featurization.resize(out.str().size());
            memcpy(auto_scheduler_results->featurization.data(), out.str().data(), out.str().size());
        }
    }
}

// Halide uses a plugin architecture for registering custom
// autoschedulers. We register our autoscheduler using a static
// constructor.
struct RegisterAutoscheduler {
    RegisterAutoscheduler() {
        aslog(1) << "Registering autoscheduler...\n";
        Pipeline::set_custom_auto_scheduler(*this);
    }

    void operator()(Pipeline p, const Target &target, const MachineParams &params, AutoSchedulerResults *results) {
        std::vector<Function> outputs;
        for (Func f : p.outputs()) {
            outputs.push_back(f.function());
        }
        Autoscheduler::generate_schedule(outputs, target, params, results);
    }
} register_auto_scheduler;


// An alternative entrypoint for other uses
void find_and_apply_schedule(FunctionDAG& dag,
                             const std::vector<Function> &outputs,
                             const MachineParams &params,
                             CostModel* cost_model,
                             int beam_size,
                             StageMap<ScheduleFeatures>* schedule_features) {

    std::mt19937 rng(12345);
    IntrusivePtr<State> optimal = optimal_schedule(dag, outputs, params, cost_model, rng, beam_size);

    // Apply the schedules
    optimal->apply_schedule(dag, params);

    if (schedule_features) {
        optimal->compute_featurization(dag, params, schedule_features);
    }
}

}


// Intrusive shared ptr helpers.
template<>
RefCount &ref_count<Autoscheduler::LoopNest>(const Autoscheduler::LoopNest *t) noexcept {return t->ref_count;}

template<>
void destroy<Autoscheduler::LoopNest>(const Autoscheduler::LoopNest *t) {delete t;}

template<>
RefCount &ref_count<Autoscheduler::State>(const Autoscheduler::State *t) noexcept {return t->ref_count;}

template<>
void destroy<Autoscheduler::State>(const Autoscheduler::State *t) {delete t;}

}
}
