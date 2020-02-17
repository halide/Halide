#include "State.h"

using std::set;
using std::vector;

namespace Halide {
namespace Internal {
namespace Autoscheduler {

bool verify_memoized_features() {
    static bool var = get_env_variable("HL_VERIFY_MEMOIZED_FEATURES") == "1";
    return var;
}

bool is_memoize_blocks_enabled() {
    static bool var = get_env_variable("HL_MEMOIZE_BLOCKS") == "1";
    return var;
}

double get_stack_memory_adjustment_factor() {
    string stack_factor_str = get_env_variable("HL_STACK_FACTOR");
    if (stack_factor_str.empty()) {
        return 0.95;
    }

    return std::atof(stack_factor_str.c_str());
}

int64_t get_stack_memory_limit() {
    static double stack_factor = get_stack_memory_adjustment_factor();
    return stack_factor * 103232;
}

bool use_adjusted_tilings() {
    static bool var = get_env_variable("HL_USE_ADJUSTED_TILINGS") == "1";
    return var;
}

bool compute_root_and_inline_only() {
    static bool only = get_env_variable("HL_COMPUTE_ROOT_AND_INLINE_ONLY") == "1";
    return only;
}

uint64_t State::structural_hash(int depth) const {
    uint64_t h = num_decisions_made;
    internal_assert(root.defined());
    root->structural_hash(h, depth);
    return h;
}

// Compute the parent and depth of every loop nest node
void State::compute_loop_nest_parents(map<const LoopNest *, pair<const LoopNest *, int>> &p,
                                      const LoopNest *here, int depth) const {
    for (const auto &c : here->children) {
        p.emplace(c.get(), pair<const LoopNest *, int>{here, depth});
        compute_loop_nest_parents(p, c.get(), depth + 1);
    }
}

const LoopNest *State::deepest_common_ancestor(const map<const LoopNest *, pair<const LoopNest *, int>> &parent, const LoopNest *a, const LoopNest *b) const {
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

bool State::has_loop_nest_without_thread_loops() const {
    for (const auto& c : root->children) {
        if (c->gpu_label != block) {
            continue;
        }

        for (const auto& block_c : c->children) {
            if (!block_c->all_paths_to_leaves_have_thread_loop()) {
                return true;
            }
        }
    }

    return false;
}

bool State::has_compute_root_loops_without_blocks() const {
    for (const auto& c : root->children) {
        if (c->gpu_label == none) {
            return true;
        }
    }

    return false;
}

void State::FeatureLoopNestMutator::operator()(LoopNest* new_loop_nest) const {
    split_compute_root_loops(new_loop_nest);
    add_outer_thread_loops(new_loop_nest);
}

// In phase 2, any compute_root loop marked 'none' will be split into
// blocks, threads, and serial loops. To enable the cost model to make a
// meaningful prediction on these pre-split loops, we assume a split into
// blocks and threads with a single full warp (if possible)
void State::FeatureLoopNestMutator::split_compute_root_loops(LoopNest* loop_nest) const {
    if (!loop_nest || !loop_nest->is_root()) {
        return;
    }

    for (auto it = loop_nest->children.rbegin(); it != loop_nest->children.rend(); ++it) {
        auto& c = *it;
        if (c->gpu_label != none) {
            continue;
        }

        int vectorized_loop_index = c->vectorized_loop_index;

        if (c->size.size() == 0) {
            continue;
        }

        // Make the vectorized dimension of the inner loop 32 (or as
        // close as possible)
        int64_t inner_extent = std::min(c->size[vectorized_loop_index], (int64_t)32);

        if (c->stage->index == 0) {
            vector<int64_t> tiling(c->node->dimensions, 1);

            // Mark as 'parallelized' so this loop is split into blocks and threads
            c->gpu_label = parallelized;
            if (vectorized_loop_index >= 0) {
                tiling[vectorized_loop_index] = inner_extent;
            }
            c = c->parallelize_in_tiles(params, tiling, loop_nest, target, true, false);
        } else {
            // An update stage may have more or fewer dimensions than
            // the pure stage, but the tiling requires its dimensions to
            // be equal to the number of dimensions in the pure stage
            vector<int64_t> tiling(c->node->dimensions, 1);
            for (size_t i = 0; i < c->stage->loop.size(); i++) {
                int l = c->stage->loop[i].pure_dim;
                if (l == -1) {
                    continue;
                }

                tiling[l] = c->size[i];
            }

            // For update stages, split into parallelized and serial
            // (parallelize_in_tiles will move any RVars inwards and
            // make them serial)
            c = c->parallelize_in_tiles(params, tiling, loop_nest, target, false, true);

            // If vectorized_loop_index < 0, then this update stage
            // likely does not loop over the vectorized loop of the
            // pure stage, so it should not be split by the
            // outer_vec_extent and instead only have a single thread
            vector<int64_t> thread_tiling(c->node->dimensions, 1);
            if (vectorized_loop_index >= 0) {
                thread_tiling[c->stage->loop[vectorized_loop_index].pure_dim] = inner_extent;
            }

            // Now that the RVars have been moved inwards, we can
            // split the outer loop into blocks and threads
            c = c->parallelize_in_tiles(params, thread_tiling, loop_nest, target, true, false);
        }
    }
}

// If a loop nest does not have thread loops, split the outermost serial
// loops to create thread loops with extents 1
void State::FeatureLoopNestMutator::add_outer_thread_loops(LoopNest* loop_nest) const {
    if (!loop_nest) {
        return;
    }

    if (loop_nest->gpu_label == block) {
        // Example:
        // block
        //  serial (a)
        //   all serial descendants
        //
        //  (a) should be surrounded by a thread loop
        for (auto& c : loop_nest->children) {
            if (c->has_thread_loop_descendant()) {
                continue;
            }

            internal_assert(c->gpu_label == serial);

            // We want outer thread loops with extents 1
            vector<int64_t> tiling(c->node->dimensions, 1);

            // Mark as 'thread' so this loop is split into threads and
            // serial
            c->gpu_label = thread;
            c = c->parallelize_in_tiles(params, tiling, loop_nest, target, false, true);
        }
        return;
    }

    if (loop_nest->gpu_label == serial) {
        bool has_child_with_thread_descendant = false;

        for (const auto& c : loop_nest->children) {
            if (c->has_thread_loop_descendant()) {
                has_child_with_thread_descendant = true;
                break;
            }
        }

        // If there are no children with thread descendants, then this must be an all
        // serial hierarchy. This may require an outer thread loop to be
        // added, but if so, this will occur when this method is called
        // on the nodes higher in the loop nest
        if (!has_child_with_thread_descendant) {
            return;
        }

        // Example:
        // serial
        //  thread
        //  serial (a)
        //
        //  (a) should be surrounded by a thread loop
        for (auto& c : loop_nest->children) {
            if (c->has_thread_loop_descendant()) {
                continue;
            }

            // We want outer thread loops with extents 1
            vector<int64_t> tiling(c->node->dimensions, 1);

            // Mark as 'thread' so this loop is split into threads and
            // serial
            c->gpu_label = thread;
            c = c->parallelize_in_tiles(params, tiling, loop_nest, target, false, true);
        }

    }
}

IntrusivePtr<const LoopNest> State::get_root_for_features(const MachineParams &params, const Target& target) const {
    if (!has_compute_root_loops_without_blocks() && !has_loop_nest_without_thread_loops()) {
        return root;
    }

    FeatureLoopNestMutator mutator{params, target};

    // We copy the loop nest in 2 cases:
    // - If the current loop nest has compute root loops without blocks (it is
    // in phase 1 and the outer loops are marked 'none'), we split the loop into blocks and threads so we can compute meaningful features
    // - If there are serial loops inside blocks without a surrounding
    // thread loop nest, we create a surrounding thread loop nest with
    // extents 1 (which Halide will do when the schedule is compiled) so
    // that we can more easily compute features
    auto new_root = create_feature_root(mutator);
    return new_root;
}

void State::set_gpu_store_site(const map<const LoopNest *, pair<const LoopNest *, int>>& parent, const LoopNest* loop, LoopNest::Sites& site) const {
    // If site.store is inside a block but outside a loop, the
    // GPU store site should instead be the block because the shared
    // mem allocation will be hoisted
    bool type_has_been_set = false;
    const LoopNest *candidate_block = loop;
    while (candidate_block) {
        if (candidate_block->gpu_label == thread) {
            site.gpu_store_memory_type = GPUMemoryType::local;
            type_has_been_set = true;
            break;
        }

        if (candidate_block->is_root()) {
            site.gpu_store_memory_type = GPUMemoryType::global;
            type_has_been_set = true;
            break;
        }

        if (candidate_block->gpu_label == block) {
            site.store = candidate_block;
            site.gpu_store_memory_type = GPUMemoryType::shared;
            type_has_been_set = true;
            break;
        }

        candidate_block = parent.at(candidate_block).first;
    }

    internal_assert(type_has_been_set);
}

void State::compute_featurization(const FunctionDAG &dag, const MachineParams &params, const Target& target, StageMap<ScheduleFeatures> *features, Statistics& stats) const {
    auto feature_root = get_root_for_features(params, target);

    StageMap<LoopNest::Sites> sites;
    sites.make_large(dag.nodes[0].stages[0].max_id);
    features->make_large(dag.nodes[0].stages[0].max_id);
    internal_assert(feature_root.defined());
    StageMap<int64_t> total_shared_mem_alloc_sizes;
    total_shared_mem_alloc_sizes.make_large(dag.nodes[0].stages[0].max_id);
    feature_root->get_sites(target, sites, total_shared_mem_alloc_sizes);

    // For the input nodes and unscheduled outputs, the compute
    // and store sites are root, and the produce and innermost
    // sites are unset (nullptr)
    for (const auto &n : dag.nodes) {
        if (n.is_input || n.is_output) {
            for (const auto &stage : n.stages) {
                auto &s = sites.get_or_create(&stage);
                if (s.compute == nullptr) {
                    s.compute = feature_root.get();
                    s.store = feature_root.get();
                    s.gpu_store_memory_type = GPUMemoryType::global;
                }
            }
        }
    }

    // For the unscheduled nodes, give them sites as deep as they
    // could possibly be. We'll ignore the possibility of inlining
    // them for now.
    map<const LoopNest *, pair<const LoopNest *, int>> parent;
    compute_loop_nest_parents(parent, feature_root.get(), 0);
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
            if (target.has_gpu_feature()) {
                set_gpu_store_site(parent, loop, site);
            }
        }
    }

    for (const auto& c : feature_root->children) {
        sites.get(c->stage).hash_of_producers_stored_at_root = c->compute_hash_of_producers_stored_at_root(sites);
    }

    if (verify_memoized_features()) {
        StageMap<ScheduleFeatures> base_features;
        base_features.make_large(dag.nodes[0].stages[0].max_id);
        feature_root->compute_features(dag, params, target, sites, 1, 1, nullptr, nullptr, *feature_root, nullptr, nullptr, nullptr, &base_features, {feature_root.get()}, false, total_shared_mem_alloc_sizes, stats);

        StageMap<ScheduleFeatures> verification_features;
        verification_features.make_large(dag.nodes[0].stages[0].max_id);
        feature_root->compute_features(dag, params, target, sites, 1, 1, nullptr, nullptr, *feature_root, nullptr, nullptr, nullptr, &verification_features, {feature_root.get()}, true, total_shared_mem_alloc_sizes, stats);

        for (auto it = base_features.begin(); it != base_features.end(); it++) {
            auto &stage = *(it.key());
            const auto &feat = it.value();

            if (!feat.equal(verification_features.get(&stage))) {
                feature_root->dump("", nullptr);
                std::cerr << "Feature Mismatch: " << stage.node->func.name() << "\n";
                feat.dump();
                std::cerr << "\n";
                verification_features.get(&stage).dump();
                std::cerr << "\n";

                internal_assert(false);
            }
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    feature_root->compute_features(dag, params, target, sites, 1, 1, nullptr, nullptr, *feature_root, nullptr, nullptr, nullptr, features, {feature_root.get()}, use_memoized_features(), total_shared_mem_alloc_sizes, stats);

    stats.featurization_time += std::chrono::high_resolution_clock::now() - t1;
    ++stats.num_featurizations;

    for (const auto &n : dag.nodes) {
        if (sites.get(&(n.stages[0])).produce == nullptr) {
            internal_assert(!features->contains(&(n.stages[0])))
                << "Somehow an input or unscheduled node ended up in the featurization: "
                << n.func.name() << "\n";
        }
    }
}

void State::save_featurization(const FunctionDAG &dag, const MachineParams &params, const Target& target, std::ostream &out) const {
    StageMap<ScheduleFeatures> features;
    Statistics stats;
    compute_featurization(dag, params, target, &features, stats);

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

bool State::contains_store_at(const set<const FunctionDAG::Node *>& outermost_store_at, const IntrusivePtr<const LoopNest>& parent) const {
    for (const auto& c : parent->children) {
        if (c->store_at.size() > 0) {
            return true;
        }

        // At production for c: if not store_at root or outermost, then it
        // must implicitly be store_at parent's level, so reject it
        bool at_production = c->node != parent->node;
        if (at_production && root->store_at.count(c->node) == 0 && outermost_store_at.count(c->node) == 0) {
            return true;
        }

        if (contains_store_at(outermost_store_at, c)) {
            return true;
        }
    }

    return false;
}

// For GPU, only allow store_at root or inside the outermost loop nest. Any
// store_ats further in will be hoisted and expanded, increasing the
// amount of shared memory required.
bool State::contains_store_at_further_in_than_outermost() const {
    for (const auto& child : root->children) {
        for (const auto& grandchild : child->children) {
            if (contains_store_at(child->store_at, grandchild)) {
                return true;
            }
        }
    }
    return false;
}


bool State::has_dynamic_allocation_inside_thread() const {
    return root->has_dynamic_allocation_inside_thread(false);
}

bool State::exceeds_serial_extents_limit(const Target &target) const {
    if (!target.has_gpu_feature()) {
        return false;
    }

    return root->exceeds_serial_extents_limit(target, nullptr, false);
}

int64_t State::get_shared_mem_alloc_size(const LoopNest* loop) const {
    int64_t result = 0;

    if (loop->gpu_label == thread) {
        return result;
    }

    for (const auto *node : loop->store_at) {
        const auto &bounds = loop->get_bounds(node);

        int64_t alloc_size = node->bytes_per_point;
        for (int i = 0; i < node->dimensions; i++) {
            const auto &p = bounds->region_computed(i);
            alloc_size *= p.extent();
        }

        if (node->dimensions > 0) {
            result += alloc_size;
        }
    }

    for (const auto& c : loop->children) {
        result += get_shared_mem_alloc_size(c.get());
    }

    return result;
}

bool State::exceeds_shared_memory_limit(const Target &target) const {
    if (!target.has_gpu_feature()) {
        return false;
    }

    static int64_t limit = get_shared_memory_limit();

    if (limit == 0) {
        return false;
    }

    for (const auto& c : root->children) {
        // If the working set is too large on the GPU, shared memory will be
        // exhausted, so reject any such schedules
        if (get_shared_mem_alloc_size(c.get()) > limit) {
            return true;
        }
    }

    return false;
}

bool State::exceeds_local_memory_limit(const Target &target) const {
    if (!target.has_gpu_feature()) {
        return false;
    }

    for (const auto& c : root->children) {
        if (c->get_total_constant_local_mem_alloc_size() > get_stack_memory_limit()) {
            return true;
        }

        if (c->get_total_local_mem_alloc_size() > kLocalMemoryLimit) {
            return true;
        }
    }

    return false;
}

bool State::calculate_cost(const FunctionDAG &dag, const MachineParams &params, const Target& target, CostModel *cost_model, Statistics& stats, bool verbose) {
    auto t1 = std::chrono::high_resolution_clock::now();
    if (!root->has_valid_thread_extents()) {
        return false;
    }

    if (exceeds_shared_memory_limit(target)) {
        return false;
    }

    if (exceeds_local_memory_limit(target)) {
        return false;
    }

    if (exceeds_serial_extents_limit(target)) {
        return false;
    }

    stats.calculate_cost_time += std::chrono::high_resolution_clock::now() - t1;

    StageMap<ScheduleFeatures> features;

    compute_featurization(dag, params, target, &features, stats);

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

    int num_stages = (int)features.size();

    Runtime::Buffer<float> schedule_features;

    // Tell the cost model about this state. It won't actually
    // evaluate it until we call evaluate_costs (or if it runs out
    // of internal buffer space), so that the evaluations can be
    // batched.
    t1 = std::chrono::high_resolution_clock::now();
    cost_model->enqueue(num_stages, &schedule_features, &cost, &cost_per_stage);
    stats.enqueue_time += std::chrono::high_resolution_clock::now() - t1;
    ++stats.num_schedules_enqueued;

    t1 = std::chrono::high_resolution_clock::now();
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
    stats.feature_write_time += std::chrono::high_resolution_clock::now() - t1;
    // Check we considered everything we were supposed to.
    internal_assert(stage == num_stages);

    return true;
}

// Make a child copy of this state. The loop nest is const (we
// make mutated copies of it, rather than mutating it), so we can
// continue to point to the same one and so this is a cheap
// operation.
IntrusivePtr<State> State::make_child() const {
    State *s = new State;
    s->parent = this;
    s->root = root;
    s->cost = cost;
    s->cost_per_stage = cost_per_stage;
    s->num_decisions_made = num_decisions_made;
    return s;
}

vector<State::ParallelTileOption> State::filter_parallel_tile_options(const MachineParams &params, const Target &target, const FunctionDAG::Node *node, vector<vector<int64_t>>& inner_tilings, const vector<int64_t>& pure_size) const {
    vector<State::ParallelTileOption> options;
    for (size_t i = 0; i < inner_tilings.size(); i++) {
        auto &t = inner_tilings[i];
        State::ParallelTileOption o;
        o.inner_tiling = t;
        o.entire = (i == inner_tilings.size() - 1);

        for (size_t j = 0; j < pure_size.size(); j++) {
            t[j] = (pure_size[j] + t[j] - 1) / t[j];
        }

        t.swap(o.outer_tiling);

        // Compute max idle cores across the other stages of the Func
        int64_t min_total = 0, max_total = 0;
        o.idle_core_wastage = 1;
        for (const auto &c : root->children) {
            if (c->node == node) {
                int64_t total = 1;
                for (auto &l : c->stage->loop) {
                    if (!l.rvar) {
                        total *= o.outer_tiling[l.pure_dim];
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
            ((o.entire || min_total >= params.parallelism * 2) &&
             (max_total <= params.parallelism * 16 || target.has_gpu_feature()));

        if (!ok) {
            continue;
        }

        options.emplace_back(std::move(o));
    }

    std::sort(options.begin(), options.end());

    return options;
}

vector<ThreadTileOption> State::filter_thread_tile_options(const MachineParams &params, const Target &target, vector<IntrusivePtr<const LoopNest>>& loop_nests) const {
    vector<ThreadTileOption> options;
    for (const auto& loop_nest : loop_nests) {
        if (!loop_nest->has_valid_thread_extents()) {
            continue;
        }

        ThreadTileOption o;
        o.loop_nest = loop_nest;
        o.max_idle_lane_wastage = loop_nest->max_idle_lane_wastage(target, {loop_nest.get()});
        options.emplace_back(std::move(o));
    }

    std::sort(options.begin(), options.end());

    return options;
}

void State::memoize_blocks(const FunctionDAG::Node *node, LoopNest* new_root, NodeMap<std::map<int, std::vector<IntrusivePtr<const LoopNest>>>>& memoized_compute_root_blocks, Statistics& stats) const {
    if (!is_memoize_blocks_enabled()) {
        return;
    }

    int vector_dim = -1;
    bool loop_nest_found = false;
    for (auto &c : new_root->children) {
        if (c->node == node && c->stage->index == 0) {
            vector_dim = c->vector_dim;
            loop_nest_found = true;
            break;
        }
    }

    internal_assert(loop_nest_found);

    auto& blocks = memoized_compute_root_blocks.get_or_create(node)[vector_dim];

    for (auto &c : new_root->children) {
        if (c->node == node) {
            LoopNest *new_block = new LoopNest;
            new_block->copy_from_including_features(*c.get());
            blocks.push_back(new_block);
            ++stats.num_block_memoization_misses;
        }
    }
}

bool State::add_states_from_memoized_blocks(const FunctionDAG &dag,
                                     const MachineParams &params,
                                     const Target &target,
                                     CostModel *cost_model,
                                     std::function<void(IntrusivePtr<State> &&)> &accept_child,
                                     Statistics& stats,
                                     const FunctionDAG::Node *node,
                                     const NodeMap<std::map<int, std::vector<IntrusivePtr<const LoopNest>>>>& memoized_compute_root_blocks,
                                     int& num_children) const {
    if (!is_memoize_blocks_enabled() || !memoized_compute_root_blocks.contains(node)) {
        return false;
    }

    int vector_dim = -1;
    for (const auto& c : root->children) {
        if (c->node == node && c->stage->index == 0) {
            vector_dim = c->vector_dim;
            break;
        }
    }

    if (memoized_compute_root_blocks.get(node).count(vector_dim) == 0) {
        return false;
    }

    auto blocks = memoized_compute_root_blocks.get(node).at(vector_dim);

    size_t num_stages = node->stages.size();
    for (size_t i = 0; i < blocks.size(); i += num_stages) {
        auto child = make_child();
        LoopNest *new_root = new LoopNest;
        new_root->copy_from(*root);
        child->root = new_root;
        child->num_decisions_made++;

        int block_index = 0;
        for (const auto& c : new_root->children) {
            if (c->node == node) {
                break;
            }
            ++block_index;
        }

        for (size_t j = 0; j < num_stages; ++j) {
            LoopNest* new_block = new LoopNest;
            new_block->copy_from_including_features(*blocks[i + j]);
            new_root->children[block_index++] = new_block;
        }

        if (child->calculate_cost(dag, params, target, cost_model, stats)) {
            num_children++;
            accept_child(std::move(child));
            ++stats.num_block_memoization_hits;
        }
    }

    return true;
}

// Generate the successor states to this state
void State::generate_children(const FunctionDAG &dag,
                       const MachineParams &params,
                       const Target &target,
                       CostModel *cost_model,
                       std::function<void(IntrusivePtr<State> &&)> &accept_child,
                       Statistics& stats,
                       bool is_pre_pass,
                       const NodeMap<bool>& inlined_nodes,
                       const NodeMap<std::vector<IntrusivePtr<const LoopNest>>>& compute_root_nodes,
                       NodeMap<std::map<int, std::vector<IntrusivePtr<const LoopNest>>>>& memoized_compute_root_blocks) const {
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

    //ScopedTimer scoped_timer{"generate_children() for " + node->func.name()};
    bool must_inline = inlined_nodes.contains(node);
    bool must_compute_root = compute_root_nodes.contains(node);

    if (node->is_input || (phase == 1 && must_compute_root)) {
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
    //ScopedStatistic<int> num_children_stat{num_children, "end phase " + std::to_string(phase) + "; num_children generated for " + node->func.name()};

    if (phase == 0) {
        // Injecting realizations
        {
            // 1) Inline it
            if (node->stages.size() == 1 && !node->is_output && !must_compute_root) {
                auto child = make_child();
                LoopNest *new_root = new LoopNest;
                new_root->copy_from(*root);
                new_root->inline_func(node);
                child->root = new_root;
                child->num_decisions_made++;
                if (child->calculate_cost(dag, params, target, cost_model, stats)) {
                    num_children++;
                    accept_child(std::move(child));
                }
            }
        }

        if (must_inline && num_children > 0) {
            std::cerr << "Must inline success: " << node->func.name() << "\n";
            return;
        }

        if (must_inline) {
            std::cerr << "Unable to inline: " << node->func.name() << "\n";
        }

        // Some search-space pruning. If a node is pointwise, and
        // so are all its inputs and so is its sole output, and
        // inlining it is legal, just inline it. This saves time
        // on long chains of pointwise things.
        must_inline = (node->is_pointwise &&
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

        if (must_compute_root) {
            LoopNest *new_root = new LoopNest;
            new_root->copy_from(*root);
            const auto &nodes = compute_root_nodes.get(node);
            for (const auto &n : nodes) {
                const auto* compute_root_loop = deep_copy_loop_nest(n.get(), NoOpMutator{});
                new_root->children.push_back(compute_root_loop);
            }
            new_root->store_at.insert(node);

            auto child = make_child();
            child->root = std::move(new_root);
            child->num_decisions_made++;
            if (child->calculate_cost(dag, params, target, cost_model, stats)) {
                num_children++;
                accept_child(std::move(child));
            }
            return;
        }

        // Construct a list of plausible dimensions to vectorize
        // over. Currently all of them. TODO: Pre-prune the list
        // of sane dimensions to vectorize a Func over to reduce
        // branching factor.
        vector<int> vector_dims;
        if (!node->is_input && !node->is_output) {
            for (int v = 0; v < node->dimensions; v++) {
                const auto &p = root->get_bounds(node)->region_computed(v);
                if (p.extent() >= 16) {
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
            auto t1 = std::chrono::high_resolution_clock::now();
            auto tile_options = root->compute_in_tiles(node, nullptr, params, target, vector_dim, false, false, is_pre_pass);
            stats.compute_in_tiles_time += std::chrono::high_resolution_clock::now() - t1;

            t1 = std::chrono::high_resolution_clock::now();
            auto options = filter_thread_tile_options(params, target, tile_options);
            stats.filter_thread_tiles_time += std::chrono::high_resolution_clock::now() - t1;

            for (const auto& o : options) {
                if (num_children >= 1 && o.max_idle_lane_wastage > 0.5) {
                    break;
                }

                auto child = make_child();
                child->root = std::move(o.loop_nest);
                child->num_decisions_made++;
                if (child->calculate_cost(dag, params, target, cost_model, stats)) {
                    num_children++;
                    accept_child(std::move(child));
                }
            }
        }
    } else {
        // We are parallelizing the loops of the func we just injected a realization for.

        bool should_parallelize = false;
        const vector<int64_t> *pure_size = nullptr;
        IntrusivePtr<const LoopNest> pure_stage;

        if (params.parallelism > 1) {
            for (auto &c : root->children) {
                if (c->node == node && node->dimensions > 0) {
                    if (c->stage->index == 0) {
                        pure_size = &(c->size);
                        pure_stage = c;
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

            if (target.has_gpu_feature()) {
                if (add_states_from_memoized_blocks(dag, params, target, cost_model, accept_child, stats, node, memoized_compute_root_blocks, num_children)) {
                    return;
                }

                // When GPU scheduling we approach tiling differently and in two steps.
                // step 1) convert (none, SIMD) loops to (parallel, serial, SIMD) loops with specialized serial sizes
                vector<int> vec_dim_serial_sizes;
                pure_stage->generate_vec_dim_serial_tilings(vec_dim_serial_sizes);

                auto parallel_tilings = generate_serial_tilings(*pure_size, node->dimensions-1, node->dimensions-1, pure_stage->vectorized_loop_index, vec_dim_serial_sizes, false, true);

                internal_assert(parallel_tilings.size() > 0) << " zero parallel tilings\n";

                for (auto &parallel_t: parallel_tilings) {
                    LoopNest *parallel_root = new LoopNest;
                    parallel_root->copy_from(*root);

                    // step 1) parallelize all loop nests for this node into (parallel, serial) with given serial tiles
                    for (auto &c : parallel_root->children) {
                        if (c->node == node) { // c is a reference to a IntrusivePtr<const LoopNest>
                            c = c->parallelize_in_tiles(params, parallel_t, parallel_root, target, false, true);
                        }
                    }
                    // step 2) split all parallel loops for this node into to (blocks, thread) loop
                    //const vector<int64_t> *parallel_size = nullptr;
                    //int vectorized_loop_i = -1;

                    vector<vector<int64_t>> stage_sizes;
                    vector<vector<int>> pure_dims;
                    vector<int> vectorized_indices;
                    parallel_root->get_stage_sizes(node, stage_sizes, pure_dims, vectorized_indices);
                    // at root level sibling thread counts are in separate blocks, extents are irrelevant
                    vector<int64_t> max_size((int)(stage_sizes[0].size()), 1);

                    auto block_tilings = generate_gpu_tilings(stage_sizes, pure_dims, max_size, node->dimensions-1, vectorized_indices, false);

                    // If no options, create a thread tiling as large as possible with block size (1,1,1).
                    // This can happen if the loops are too small to generate desired gpu tiles.
                    if (block_tilings.empty()) {
                        auto child = make_child();
                        LoopNest *new_root = new LoopNest;
                        new_root->copy_from(*parallel_root);
                        for (auto &c : new_root->children) {
                            if (c->node == node) {
                                vector<int64_t> tiling((int)(c->size.size()), 1);
                                c = c->parallelize_in_tiles(params, tiling, new_root, target, false, true);
                            }
                        }
                        child->root = new_root;
                        child->num_decisions_made++;
                        if (child->calculate_cost(dag, params, target, cost_model, stats)) {
                            num_children++;
                            accept_child(std::move(child));
                            memoize_blocks(node, new_root, memoized_compute_root_blocks, stats);
                        }
                        return;
                    }

                    auto t1 = std::chrono::high_resolution_clock::now();
                    auto options = filter_parallel_tile_options(params, target, node, block_tilings, stage_sizes[0]);
                    stats.filter_parallel_tiles_time += std::chrono::high_resolution_clock::now() - t1;

                    for (const auto &o : options) {
                        if (num_children >= 1 && o.idle_core_wastage > 1.2) {
                            // We have considered several options, and the
                            // remaining ones leave lots of cores idle.
                            break;
                        }

                        auto child = make_child();
                        LoopNest *new_root = new LoopNest;
                        new_root->copy_from(*parallel_root); // copies parallel_root's info and intrusive pointers for parallel_root's children
                        for (auto &c : new_root->children) {
                            if (c->node == node) {
                                c = c->parallelize_in_tiles(params, o.inner_tiling, new_root, target, true, false);
                            }
                        }
                        child->root = new_root;
                        child->num_decisions_made++;
                        if (child->calculate_cost(dag, params, target, cost_model, stats)) {
                            num_children++;
                            accept_child(std::move(child));
                            memoize_blocks(node, new_root, memoized_compute_root_blocks, stats);
                        }

                        if (!use_adjusted_tilings()) {
                            continue;
                        }

                        // make another child where tiling is adjusted in case it doesn't evenly divide
                        auto adjusted_child = make_child();
                        LoopNest *new_adjusted_root = new LoopNest;
                        new_adjusted_root->copy_from(*parallel_root); // copies parallel_root's info and intrusive pointers for parallel_root's children
                        bool create_child = false;
                        for (auto &c : new_adjusted_root->children) {
                            if (c->node == node) {

                                // If the tiling evenly divides the loop's
                                // extents, then this child will be
                                // identical to the one created above. Only
                                // create the child if it will produce a
                                // different state
                                int i = 0;
                                for (auto b : o.inner_tiling) {
                                    if (c->size[i++] % b != 0) {
                                        create_child = true;
                                    }
                                }
                                c = c->parallelize_in_tiles(params, o.inner_tiling, new_adjusted_root, target, true, true);
                            }
                        }
                        adjusted_child->root = new_adjusted_root;
                        adjusted_child->num_decisions_made++;
                        if (create_child && adjusted_child->calculate_cost(dag, params, target, cost_model, stats)) {
                            num_children++;
                            accept_child(std::move(adjusted_child));
                        }
                    }
                    delete parallel_root;
                }
            } else { // scheduling for CPU, just do regular tilings
                // Deciding on parallel task size/shape.
                auto tilings = generate_tilings(*pure_size, node->dimensions - 1, 2, true, target);
                // We could just parallelize the outer loop entirely
                std::vector<int64_t> ones;
                ones.resize(pure_size->size(), 1);
                tilings.emplace_back(std::move(ones));

                vector<ParallelTileOption> options = filter_parallel_tile_options(params, target, node, tilings, *pure_size);

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
                                c = c->parallelize_in_tiles(params, o.outer_tiling, new_root, target, false, true);
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
                                c = c->parallelize_in_tiles(params, tiling, new_root, target, false, true);
                            }
                        }
                    }
                    child->root = new_root;
                    child->num_decisions_made++;
                    if (child->calculate_cost(dag, params, target, cost_model, stats)) {
                        num_children++;
                        accept_child(std::move(child));
                    }
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

void State::dump() const {
    aslog(0) << "State with cost " << cost << ":\n";
    root->dump("", nullptr);
    aslog(0) << schedule_source;
}

void State::fuse_gpu_blocks(LoopNest::StageScheduleState* state, Stage& stage, const vector<VarOrRVar>& parallel_vars, const vector<int64_t>& parallel_extents) const {
    if (parallel_vars.empty() || parallel_extents.empty()) {
        return;
    }

    constexpr int max_blocks[3] = {2147483647, 65535, 65535};
    int block_extents[3] = {1, 1, 1};

    std::vector<size_t> block_var_assignments[3];

    int i = parallel_vars.size() - 1;
    for (size_t block_i = 0; block_i < 3; ++block_i) {
        for (; i >= 0 && parallel_extents[i] * block_extents[block_i] <= max_blocks[block_i]; --i) {
            block_extents[block_i] *= parallel_extents[i];
            block_var_assignments[block_i].push_back(i);
        }
    }

    for (size_t block_i = 0; block_i < 3; ++block_i) {
        for (size_t i = 1; i < block_var_assignments[block_i].size(); ++i) {
            auto inner_i = block_var_assignments[block_i][0];
            auto outer_i = block_var_assignments[block_i][i];
            state->schedule_source << "\n    .fuse(" << parallel_vars[inner_i].name()
                                      << ", " << parallel_vars[outer_i].name()
                                      << ", " << parallel_vars[inner_i].name() << ")";
            stage.fuse(parallel_vars[inner_i],
                       parallel_vars[outer_i],
                       parallel_vars[inner_i]);
        }

        if (block_var_assignments[block_i].size() > 0) {
            auto inner_i = block_var_assignments[block_i][0];
            state->schedule_source << "\n    .gpu_blocks(" << parallel_vars[inner_i].name() << ")";
            stage.gpu_blocks(parallel_vars[inner_i]);
            state->parallel = true;
        }
    }
}

void State::mark_gpu_blocks(LoopNest::StageScheduleState* state, Stage& stage, const vector<VarOrRVar>& parallel_vars, const vector<int64_t>& parallel_extents) const {
    int max_blocks[3] = {2147483647, 65535, 65535};
    uint8_t n_loops_tagged_gpu_blocks = 0;

    for (auto& v : parallel_vars) {
        if (n_loops_tagged_gpu_blocks >= 3 || parallel_extents[n_loops_tagged_gpu_blocks] > max_blocks[n_loops_tagged_gpu_blocks]) {
            break;
        }

        state->schedule_source << "\n    .gpu_blocks(" << v.name() << ")";
        stage.gpu_blocks(v);
        ++n_loops_tagged_gpu_blocks;
    }

    if (n_loops_tagged_gpu_blocks > 0) {
        state->parallel = true;
    }
}

bool State::mark_gpu_threads(LoopNest::StageScheduleState* state, Stage& stage, std::unordered_set<std::string>& new_serial_vars) const {
    uint8_t num_loops_tagged_gpu_thread = 0;
    int64_t total_threads = 1;
    int max_threads[3] = {1024, 1024, 64};

    for (const auto& v : state->vars) {
        if (!v.exists || !v.gpu_threads || v.extent == 1)  {
            continue;
        }

        if (num_loops_tagged_gpu_thread >= 3 || total_threads >= MAX_THREADS_PER_BLOCK || v.extent > max_threads[num_loops_tagged_gpu_thread]) {
            break;
        }

        Var new_outer(v.var.name() + "_serial_outer");
        new_serial_vars.insert(new_outer.name());
        stage.split(v.var, new_outer, v.var, (int)v.extent, TailStrategy::GuardWithIf);
        stage.gpu_threads(v.var);
        state->schedule_source << "\n    .split(" << v.var.name() << ", " << new_outer.name() << ", " << v.var.name() << ", " << v.extent << ", TailStrategy::GuardWithIf)";
        state->schedule_source << "\n    .gpu_threads(" << v.var.name() << ")";
        num_loops_tagged_gpu_thread++;
    }

    return num_loops_tagged_gpu_thread > 0;
}

bool State::can_fuse_gpu(const vector<int64_t>& parallel_extents) const {
    int64_t total = 1;
    for (auto extent : parallel_extents) {
        total *= extent;
    }

    // Max grid size in x dimension
    constexpr int64_t max_blocks = 2147483647;
    return total < max_blocks;
}

// Apply the schedule represented by this state to a Halide
// Pipeline. Also generate source code for the schedule for the
// user to copy-paste to freeze this schedule as permanent artifact.
void State::apply_schedule(const FunctionDAG &dag, const MachineParams &params, const Target &target) {
    StageMap<std::unique_ptr<LoopNest::StageScheduleState>> state_map;
    std::vector<LoopNest::StageScheduleState*> ancestors;

    root->apply(LoopLevel::root(), state_map, params.parallelism, 0, nullptr, nullptr, target, ancestors);

    std::ostringstream src;
    std::unordered_set<std::string> new_serial_vars;

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
        vector<int64_t> parallel_extents;
        bool any_parallel_vars = false, any_parallel_rvars = false;
        for (auto it = p.second->vars.rbegin(); it != p.second->vars.rend(); it++) {
            if (!it->exists || it->extent == 1) continue;
            if (!it->parallel) break;
            any_parallel_rvars |= it->var.is_rvar;
            any_parallel_vars |= !it->var.is_rvar;
            parallel_tasks *= it->extent;
            parallel_extents.push_back(it->extent);
            parallel_vars.push_back(it->var);
        }

        if (p.second->vars.size() > 1) {
            p.second->schedule_source << "\n    .reorder(";
            bool first = true;
            for (auto &v : p.second->vars) {
                if (v.exists) {
                    vars.push_back(v.var);
                    p.second->ordered_vars.push_back(v);
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
            fuse_gpu_blocks(p.second.get(), stage, parallel_vars, parallel_extents);
        } else {
            if (target.has_gpu_feature()) {
                mark_gpu_blocks(p.second.get(), stage, parallel_vars, parallel_extents);
            } else {
                for (const auto &v : parallel_vars) {
                    p.second->schedule_source << "\n    .parallel(" << v.name() << ")";
                    stage.parallel(v);
                }
            }
        }

        if (!parallel_vars.empty()) {
            p.second->parallel = true;
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
    }

    if (target.has_gpu_feature()) {
        std::set<const FunctionDAG::Node*> invalid;
        // Iterate from output backwards
        for (const auto &n : dag.nodes) {
            for (auto &p : state_map) {
                if (&n != p.second->node) {
                    continue;
                }

                if (p.first->node->is_input) continue;

                Stage stage(p.first->stage);

                // If at least one loop has been marked gpu_thread, we need to
                // ensure that it is enclosed by a gpu_block loop. Check if this
                // loop nest or one of its ancestors has been marked gpu_block
                bool has_enclosing_parallel = p.second->parallel;

                if (!has_enclosing_parallel) {
                    for (auto* ancestor : p.second->ancestors) {
                        if (ancestor->parallel) {
                            has_enclosing_parallel = true;
                            break;
                        }
                    }
                }

                if (!mark_gpu_threads(p.second.get(), stage, new_serial_vars) || has_enclosing_parallel) {
                    continue;
                }

                // There is no outer loop marked as gpu_block.
                // Split the outer loop to create a new outer var with
                // extent = 1 and mark it gpu_blocks()
                const auto& outer_var = p.second->ordered_vars.back();
                vector<VarOrRVar> vars;
                for (const auto& v : p.second->ordered_vars) {
                    vars.push_back(v.var);
                }

                Var new_outer(outer_var.var.name() + "_outer");
                stage.split(outer_var.var, new_outer, outer_var.var, (int)outer_var.extent);

                new_serial_vars.insert(new_outer.name());
                p.second->schedule_source
                    << "\n    .split("
                    << outer_var.var.name() << ", "
                    << new_outer.name() << ", "
                    << outer_var.var.name() << ", "
                    << outer_var.extent << ")";

                // If there are store_ats at Var::outermost(), we need to ensure
                // that those store_ats are retained at the Var::outermost level
                vars.push_back(new_outer);
                vars.push_back(Var::outermost());

                p.second->schedule_source << "\n    .reorder(";
                bool first = true;

                for (const auto& v : vars) {
                    if (!first) {
                        p.second->schedule_source << ", ";
                    }
                    if (v.name() == "__outermost") {
                        p.second->schedule_source << "Var::outermost()";
                    } else {
                        p.second->schedule_source << v.name();
                    }
                    first = false;
                }
                p.second->schedule_source << ")";

                stage.reorder(vars);
                stage.gpu_blocks(new_outer);
                p.second->parallel = true;
                p.second->schedule_source << "\n    .gpu_blocks(" << new_outer.name() << ")";
            }
        }
    }

    for (const auto& v : new_serial_vars) {
        src << "Var " << v << "(\"" << v << "\");\n";
    }

    for (auto &p : state_map) {
        if (p.first->node->is_input) continue;

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

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide
