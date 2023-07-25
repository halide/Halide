#include "State.h"

using std::set;
using std::vector;

namespace Halide {
namespace Internal {
namespace Autoscheduler {

int64_t get_stack_memory_limit(const Anderson2021Params &params) {
    return params.stack_factor * 103232;
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

const LoopNest *State::deepest_valid_compute_location(const Anderson2021Params &params, const map<const LoopNest *, pair<const LoopNest *, int>> &parent, const FunctionDAG::Node &node, const LoopNest *loop, const LoopNest *root, StageMap<int64_t> &total_shared_mem_alloc_sizes) const {
    std::vector<const LoopNest *> ancestors;

    // Innermost loop nests are never considered as compute locations
    if (!loop->innermost) {
        ancestors.push_back(loop);
    }

    const LoopNest *cur_loop = loop;
    while (parent.count(cur_loop) > 0) {
        ancestors.push_back(parent.at(cur_loop).first);
        cur_loop = ancestors.back();
    }

    if (ancestors.empty()) {
        return root;
    }

    const LoopNest *candidate = ancestors.back();
    bool first = true;

    int64_t new_shared_mem_alloc_size = 0;
    int64_t new_register_alloc_size = 0;

    for (auto it = ancestors.rbegin(); it != ancestors.rend(); it++) {
        if (first) {
            first = false;
            continue;
        }

        if ((*it)->gpu_label == GPU_parallelism::Block) {
            new_shared_mem_alloc_size = node.bytes_per_point;
            for (int i = 0; i < node.dimensions; ++i) {
                new_shared_mem_alloc_size *= (*it)->get_bounds(&node)->region_computed(i).extent();
            }

            int64_t total = new_shared_mem_alloc_size + total_shared_mem_alloc_sizes.get((*it)->stage);
            if (total > get_shared_memory_limit(params)) {
                continue;
            }
        }

        if ((*it)->gpu_label == GPU_parallelism::Thread || (*it)->gpu_label == GPU_parallelism::Serial) {
            int64_t total = node.bytes_per_point;
            for (int i = 0; i < node.dimensions; ++i) {
                total *= (*it)->get_bounds(&node)->region_computed(i).extent();
            }

            if (total > get_register_mem_alloc_limit()) {
                continue;
            }

            new_register_alloc_size = total;
        }

        // If the region_computed does not shrink, ancestors.at(i) (the loop
        // nest one level further in) will never be considered as a compute
        // location
        if (!(*it)->region_computed_shrinks(&node, candidate)) {
            break;
        }

        candidate = *it;
    }

    if (candidate->gpu_label == GPU_parallelism::Block) {
        total_shared_mem_alloc_sizes.get(candidate->stage) += new_shared_mem_alloc_size;
        internal_assert(total_shared_mem_alloc_sizes.get(candidate->stage) <= get_shared_memory_limit(params));
    }

    internal_assert(new_register_alloc_size <= get_register_mem_alloc_limit());
    internal_assert(!candidate->innermost);
    return candidate;
}

int64_t State::total_loop_extents_of_ancestors(const map<const LoopNest *, pair<const LoopNest *, int>> &parent, const LoopNest *loop) const {
    int64_t total = 1;

    if (loop->is_root()) {
        return total;
    }

    const LoopNest *cur_loop = loop;
    while (true) {
        for (long i : cur_loop->size) {
            total *= i;
        }

        if (parent.count(cur_loop) == 0) {
            break;
        }

        cur_loop = parent.at(cur_loop).first;
    }

    return total;
}

const LoopNest *State::deepest_common_ancestor(const map<const LoopNest *, pair<const LoopNest *, int>> &parent, const LoopNest *a, const LoopNest *b) const {
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

    while (true) {
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

bool State::has_loop_nest_without_thread_loops() const {
    for (const auto &c : root->children) {
        if (c->gpu_label != GPU_parallelism::Block) {
            continue;
        }

        for (const auto &block_c : c->children) {
            if (!block_c->all_paths_to_leaves_have_thread_loop()) {
                return true;
            }
        }
    }

    return false;
}

bool State::has_compute_root_loops_without_blocks() const {
    for (const auto &c : root->children) {
        if (c->gpu_label == GPU_parallelism::None) {
            return true;
        }
    }

    return false;
}

void State::FeatureLoopNestMutator::operator()(LoopNest *new_loop_nest) const {
    split_compute_root_loops(new_loop_nest);
    add_outer_thread_loops(new_loop_nest);
}

// In phase 2, any compute_root loop marked 'none' will be split into
// blocks, threads, and serial loops. To enable the cost model to make a
// meaningful prediction on these pre-split loops, we assume a split into
// blocks and threads with a single full warp (if possible)
void State::FeatureLoopNestMutator::split_compute_root_loops(LoopNest *loop_nest) const {
    if (!loop_nest || !loop_nest->is_root()) {
        return;
    }

    for (auto it = loop_nest->children.rbegin(); it != loop_nest->children.rend(); ++it) {
        auto &c = *it;
        if (c->gpu_label != GPU_parallelism::None) {
            continue;
        }

        int vectorized_loop_index = c->vectorized_loop_index;

        if (c->size.empty()) {
            continue;
        }

        if (c->stage->index == 0) {
            vector<int64_t> tiling(c->node->dimensions, 1);

            // Split into parallelized and serial
            c = c->parallelize_in_tiles(tiling, loop_nest, params, target, true, false);

            if (vectorized_loop_index >= 0) {
                // Make the vectorized dimension of the inner loop 32 (or as
                // close as possible)
                int64_t inner_extent = std::min(c->size[vectorized_loop_index], (int64_t)32);

                tiling[vectorized_loop_index] = inner_extent;
            }
            // Split parallelized into blocks and threads
            c = c->parallelize_in_tiles(tiling, loop_nest, params, target, true, false);
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
            c = c->parallelize_in_tiles(tiling, loop_nest, params, target, false, true);

            // If vectorized_loop_index < 0, then this update stage
            // likely does not loop over the vectorized loop of the
            // pure stage, so it should not be split by the
            // outer_vec_extent and instead only have a single thread
            vector<int64_t> thread_tiling(c->node->dimensions, 1);
            if (vectorized_loop_index >= 0) {
                // Make the vectorized dimension of the inner loop 32 (or as
                // close as possible)
                int64_t inner_extent = std::min(c->size[vectorized_loop_index], (int64_t)32);

                thread_tiling[c->stage->loop[vectorized_loop_index].pure_dim] = inner_extent;
            }

            // Now that the RVars have been moved inwards, we can
            // split the outer loop into blocks and threads
            c = c->parallelize_in_tiles(thread_tiling, loop_nest, params, target, true, false);
        }
    }
}

// If a loop nest does not have thread loops, split the outermost serial
// loops to create thread loops with extents 1
void State::FeatureLoopNestMutator::add_outer_thread_loops(LoopNest *loop_nest) const {
    if (!loop_nest) {
        return;
    }

    if (loop_nest->gpu_label == GPU_parallelism::Block) {
        // Example:
        // block
        //  serial (a)
        //   all serial descendants
        //
        //  (a) should be surrounded by a thread loop
        for (auto &c : loop_nest->children) {
            if (c->has_thread_loop_descendant()) {
                continue;
            }

            internal_assert(c->gpu_label == GPU_parallelism::Serial);

            // We want outer thread loops with extents 1
            vector<int64_t> tiling(c->node->dimensions, 1);

            // Mark as 'thread' so this loop is split into threads and
            // serial
            c->gpu_label = GPU_parallelism::Thread;
            c = c->parallelize_in_tiles(tiling, loop_nest, params, target, false, true);
        }
        return;
    }

    if (loop_nest->gpu_label == GPU_parallelism::Serial) {
        bool has_child_with_thread_descendant = false;

        for (const auto &c : loop_nest->children) {
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
        for (auto &c : loop_nest->children) {
            if (c->has_thread_loop_descendant()) {
                continue;
            }

            // We want outer thread loops with extents 1
            vector<int64_t> tiling(c->node->dimensions, 1);

            // Mark as 'thread' so this loop is split into threads and
            // serial
            c->gpu_label = GPU_parallelism::Thread;
            c = c->parallelize_in_tiles(tiling, loop_nest, params, target, false, true);
        }
    }
}

IntrusivePtr<const LoopNest> State::get_root_for_features(const Anderson2021Params &params, const Target &target) const {
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
    auto *new_root = create_feature_root(mutator);
    return new_root;
}

void State::set_gpu_store_site(const map<const LoopNest *, pair<const LoopNest *, int>> &parent, const LoopNest *loop, LoopNest::Sites &site) const {
    // If site.store is inside a block but outside a loop, the
    // GPU store site should instead be the block because the shared
    // mem allocation will be hoisted
    bool type_has_been_set = false;
    const LoopNest *candidate_block = loop;
    while (candidate_block) {
        if (candidate_block->gpu_label == GPU_parallelism::Thread) {
            site.gpu_store_memory_type = GPUMemoryType::Registers;
            type_has_been_set = true;
            break;
        }

        if (candidate_block->is_root()) {
            site.gpu_store_memory_type = GPUMemoryType::Global;
            type_has_been_set = true;
            break;
        }

        if (candidate_block->gpu_label == GPU_parallelism::Block) {
            site.store = candidate_block;
            site.gpu_store_memory_type = GPUMemoryType::Shared;
            type_has_been_set = true;
            break;
        }

        candidate_block = parent.at(candidate_block).first;
    }

    internal_assert(type_has_been_set);
}

bool State::compute_featurization(const FunctionDAG &dag, const Anderson2021Params &params, const Target &target, StageMap<ScheduleFeatures> *features, Statistics &stats, bool verbose) const {
    auto feature_root = get_root_for_features(params, target);

    StageMap<LoopNest::Sites> sites;
    sites.make_large(dag.nodes[0].stages[0].max_id);
    features->make_large(dag.nodes[0].stages[0].max_id);
    internal_assert(feature_root.defined());
    StageMap<int64_t> total_shared_mem_alloc_sizes;
    total_shared_mem_alloc_sizes.make_large(dag.nodes[0].stages[0].max_id);
    feature_root->get_sites(target, sites, total_shared_mem_alloc_sizes);
    if (!feature_root->promote_allocs_to_registers(target, sites)) {
        return false;
    }

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
                    s.gpu_store_memory_type = GPUMemoryType::Global;
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
                if (consumer_site.inlined) {
                    // If this func is inlined, find the deepest common ancestor
                    // of all its inlined locations
                    for (const auto *innermost : consumer_site.inlined_innermosts) {
                        loop = deepest_common_ancestor(parent, innermost, loop);
                    }
                } else {
                    loop = deepest_common_ancestor(parent, l, loop);
                }
            } else {
                if (consumer_site.inlined) {
                    bool first = true;
                    // If this func is inlined, find the deepest common ancestor
                    // of all its inlined locations
                    for (const auto *innermost : consumer_site.inlined_innermosts) {
                        if (first) {
                            first = false;
                            loop = innermost;
                            continue;
                        }

                        loop = deepest_common_ancestor(parent, innermost, loop);
                    }
                } else {
                    loop = l;
                }
            }
        }
        internal_assert(loop)
            << "Could not compute plausible site for unscheduled Func: "
            << n.func.name() << "\n";

        // If 'loop' would never be considered as a compute location (i.e. by
        // LoopNest::compute_in_tiles()), walk up the loop nest until we reach a
        // location that would be considered
        loop = deepest_valid_compute_location(params, parent, n, loop, feature_root.get(), total_shared_mem_alloc_sizes);
        int64_t num_realizations = total_loop_extents_of_ancestors(parent, loop);

        for (const auto &stage : n.stages) {
            auto &site = sites.get_or_create(&stage);
            site.compute = loop;
            site.store = loop;
            site.num_realizations = num_realizations;
            if (target.has_gpu_feature()) {
                set_gpu_store_site(parent, loop, site);
            }
        }
    }

    for (const auto &c : feature_root->children) {
        sites.get(c->stage).hash_of_producers_stored_at_root = c->compute_hash_of_producers_stored_at_root(sites);
    }

    Timer timer;
    feature_root->compute_features(dag, params, target, sites, 1, 1, nullptr, nullptr, *feature_root, GPULoopInfo(feature_root.get()), true, total_shared_mem_alloc_sizes, nullptr, nullptr, nullptr, features, stats, verbose);

    stats.featurization_time += timer.elapsed();
    ++stats.num_featurizations;

    for (const auto &n : dag.nodes) {
        if (sites.get(&(n.stages[0])).produce == nullptr) {
            internal_assert(!features->contains(&(n.stages[0])))
                << "Somehow an input or unscheduled node ended up in the featurization: "
                << n.func.name() << "\n";
        }
    }

    return true;
}

void State::save_featurization(const FunctionDAG &dag, const Anderson2021Params &params, const Target &target, std::ostream &out) const {
    StageMap<ScheduleFeatures> features;
    Statistics stats;
    compute_featurization(dag, params, target, &features, stats);

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

bool State::contains_store_at(const set<const FunctionDAG::Node *> &outermost_store_at, const IntrusivePtr<const LoopNest> &parent) const {
    for (const auto &c : parent->children) {
        if (!c->store_at.empty()) {
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
    for (const auto &child : root->children) {
        for (const auto &grandchild : child->children) {
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

int64_t State::get_shared_mem_alloc_size(const LoopNest *block, const LoopNest *loop) const {
    int64_t result = 0;

    if (loop->gpu_label == GPU_parallelism::Thread) {
        return result;
    }

    for (const auto *node : loop->store_at) {
        const auto &bounds = block->get_bounds(node);

        int64_t alloc_size = node->bytes_per_point;
        for (int i = 0; i < node->dimensions; i++) {
            const auto &p = bounds->region_computed(i);
            alloc_size *= p.extent();
        }

        if (node->dimensions > 0) {
            result += alloc_size;
        }
    }

    for (const auto &c : loop->children) {
        result += get_shared_mem_alloc_size(block, c.get());
    }

    return result;
}

bool State::exceeds_shared_memory_limit(const Anderson2021Params &params, const Target &target) const {
    if (!target.has_gpu_feature()) {
        return false;
    }

    static int64_t limit = get_shared_memory_limit(params);

    if (limit == 0) {
        return false;
    }

    for (const auto &c : root->children) {
        // If the working set is too large on the GPU, shared memory will be
        // exhausted, so reject any such schedules
        if (get_shared_mem_alloc_size(c.get(), c.get()) > limit) {
            return true;
        }
    }

    return false;
}

bool State::exceeds_local_memory_limit(const Anderson2021Params &params, const Target &target) const {
    if (!target.has_gpu_feature()) {
        return false;
    }

    for (const auto &c : root->children) {
        if (c->get_total_constant_local_mem_alloc_size() > get_stack_memory_limit(params)) {
            return true;
        }

        if (c->get_total_local_mem_alloc_size() > kLocalMemoryLimit) {
            return true;
        }
    }

    return false;
}

bool State::calculate_cost(const FunctionDAG &dag, const Anderson2021Params &params, const Target &target, CostModel *cost_model, Statistics &stats, bool verbose) {
    Timer timer;
    if (!root->has_valid_thread_extents()) {
        Filter(root.get()) << "Invalid thread extents\n";
        return false;
    }

    if (exceeds_shared_memory_limit(params, target)) {
        Filter(root.get()) << "Exceeds shared memory limit\n";
        return false;
    }

    if (exceeds_local_memory_limit(params, target)) {
        Filter(root.get()) << "Exceeds local memory limit\n";
        return false;
    }

    if (exceeds_serial_extents_limit(target)) {
        Filter(root.get()) << "Exceeds serial loop extent limit\n";
        return false;
    }

    stats.calculate_cost_time += timer.elapsed();

    StageMap<ScheduleFeatures> features;

    if (!compute_featurization(dag, params, target, &features, stats, verbose)) {
        Filter(root.get()) << "Contains a local allocation that likely cannot be promoted to registers\n";
        return false;
    }

    cost = 0;

    if (verbose) {
        for (auto it = features.begin(); it != features.end(); it++) {
            const auto &stage = *(it.key());
            const auto &feat = it.value();
            std::string name = stage.node->func.name();
            sanitize_names(name);
            aslog(1) << "Schedule features for " << name << "_s" << stage.index << "\n";
            feat.dump();
        }
    }

    internal_assert(cost_model);

    // Perform some addition pruning before burdening the cost model with silly states
    for (auto it = features.begin(); it != features.end(); it++) {
        if (!it.key()->node->is_wrapper) {  // It's OK to repeatedly stage data
            auto &feat = it.value();
            if (should_always_consider_inline(it.key()->node)) {
                continue;
            }

            if (feat.points_computed_total + feat.inlined_calls > 10 * feat.points_computed_minimum) {
                Filter(root.get()) << "Excess recompute for " << it.key()->node->func.name() << " stage " << it.key()->index << "\n"
                                   << "points_computed_total = " << feat.points_computed_total << "\n"
                                   << "inlined_calls = " << feat.inlined_calls << "\n"
                                   << "points_computed_total + inlined_calls = " << feat.points_computed_total + feat.inlined_calls << "\n"
                                   << "points_computed_minimum = " << feat.points_computed_minimum << "\n"
                                   << "8 * points_computed_minimum = " << 8 * feat.points_computed_minimum << "\n";
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

    cost_model->enqueue(dag, features, &cost, &cost_per_stage);

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
    s->always_consider_inline = always_consider_inline;
    return s;
}

void State::dump() const {
    aslog(1) << "State with cost " << cost << ":\n";
    root->dump();
    aslog(1) << schedule_source;
}

void State::print_compute_locations() const {
    StageMap<StageMap<bool>> descendants;
    root->get_stages_computed_in_each_compute_root_loop(descendants);

    aslog(1) << "BEGIN compute locations\n";
    for (const auto &d : descendants) {
        aslog(1) << d.first->sanitized_name << " -> ";

        for (const auto &descendant : d.second) {
            aslog(1) << descendant.first->sanitized_name << " ";
        }

        aslog(1) << "\n";
    }
    aslog(1) << "END compute locations\n";
}

void State::fuse_gpu_blocks(LoopNest::StageScheduleState *state, Stage &stage, const vector<VarOrRVar> &parallel_vars, const vector<int64_t> &parallel_extents, const vector<int> &constant_extents) const {
    if (parallel_vars.empty() || parallel_extents.empty()) {
        return;
    }

    constexpr int max_blocks[3] = {2147483647, 65535, 65535};
    int block_extents[3] = {1, 1, 1};

    std::vector<size_t> block_var_assignments[3];

    // When parallel_vars/parallel_extents/constant_extents were created in apply_schedule,
    // each entry was added in reverse order. Start from the end (the
    // innermost dimension) and assign each var to a gpu_block.
    int i = parallel_vars.size() - 1;
    for (size_t block_i = 0; block_i < 3; ++block_i) {
        for (; i >= 0 && parallel_extents[i] * block_extents[block_i] <= max_blocks[block_i]; --i) {
            if (parallel_extents[i] > 1 || !constant_extents[i]) {
                block_extents[block_i] *= parallel_extents[i];
                block_var_assignments[block_i].push_back(i);

                // Use a single block for the first 2 innermost dimensions. The
                // remaining dimensions should all be assigned to the same block and
                // fused
                if (block_i < 2) {
                    --i;
                    break;
                }
            }
        }
    }

    bool marked = false;
    for (auto &block_var_assignment : block_var_assignments) {
        for (size_t i = 1; i < block_var_assignment.size(); ++i) {
            auto inner_i = block_var_assignment[0];
            auto outer_i = block_var_assignment[i];
            state->schedule_source << "\n    .fuse(" << parallel_vars[inner_i].name()
                                   << ", " << parallel_vars[outer_i].name()
                                   << ", " << parallel_vars[inner_i].name() << ")";
            stage.fuse(parallel_vars[inner_i],
                       parallel_vars[outer_i],
                       parallel_vars[inner_i]);
        }

        if (!block_var_assignment.empty()) {
            auto inner_i = block_var_assignment[0];
            state->schedule_source << "\n    .gpu_blocks(" << parallel_vars[inner_i].name() << ")";
            stage.gpu_blocks(parallel_vars[inner_i]);
            state->parallel = true;
            marked = true;
        }
    }

    if (!marked) {
        bool all_one = true;
        for (auto extent : parallel_extents) {
            all_one = all_one && extent == 1;
        }

        // If all the parallel extents = 1, just mark the innermost parallel_var
        // as .gpu_block()
        if (all_one) {
            int i = parallel_vars.size() - 1;
            state->schedule_source << "\n    .gpu_blocks(" << parallel_vars[i].name() << ")";
            stage.gpu_blocks(parallel_vars[i]);
            state->parallel = true;
        }
    }
}

void State::mark_gpu_blocks(LoopNest::StageScheduleState *state, Stage &stage, const vector<VarOrRVar> &parallel_vars, const vector<int64_t> &parallel_extents) const {
    int max_blocks[3] = {2147483647, 65535, 65535};
    uint8_t n_loops_tagged_gpu_blocks = 0;

    for (const auto &v : parallel_vars) {
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

bool State::mark_gpu_threads(LoopNest::StageScheduleState *state, Stage &stage, std::unordered_set<std::string> &new_serial_vars, std::ostringstream &staged_funcs_schedule_source) const {
    uint8_t num_loops_tagged_gpu_thread = 0;
    int64_t total_threads = 1;
    int max_threads[3] = {1024, 1024, 64};

    bool first = true;

    for (const auto &v : state->vars) {
        if (!v.exists || !v.gpu_threads || v.extent == 1) {
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

        if (first) {
            first = false;

            Func func(state->node->func);

            for (const auto &to_be_staged : state->producers_to_be_staged) {
                const auto *producer_node = to_be_staged.first;

                for (const auto &cur_pair : to_be_staged.second) {
                    const LoopNest *loop_nest = cur_pair.first;
                    const std::vector<const FunctionDAG::Edge *> &edge_chain = cur_pair.second;

                    internal_assert(edge_chain.at(0)->consumer == loop_nest->stage);
                    internal_assert(edge_chain.back()->producer == producer_node);

                    if (edge_chain.size() > 1) {
                        std::string s = func.name();
                        for (size_t i = 0; i < edge_chain.size() - 1; ++i) {
                            s = edge_chain.at(i)->producer->func.name() + ".clone_in(" + s + ")";
                        }
                        aslog(1) << "Chain with length > 1: " << producer_node->func.name() << ".in(" << s << ")\n";
                        continue;
                    }

                    auto clone_in_chain = func;
                    auto clone_in_chain_source_str = func.name();

                    for (size_t i = 0; i < edge_chain.size() - 1; ++i) {
                        clone_in_chain = Func(edge_chain.at(i)->producer->func).clone_in(clone_in_chain);
                        clone_in_chain_source_str = edge_chain.at(i)->producer->func.name() + ".clone_in(" + clone_in_chain_source_str + ")";
                    }

                    Func producer(producer_node->func);
                    producer.in(clone_in_chain).store_in(MemoryType::Register).compute_at(func, v.var.var);
                    staged_funcs_schedule_source
                        << producer.name()
                        << ".in("
                        << clone_in_chain_source_str
                        << ").store_in(MemoryType::Register).compute_at("
                        << func.name()
                        << ", "
                        << v.var.var.name()
                        << ")";

                    const auto &bounds = loop_nest->get_bounds_along_edge_chain(producer_node, edge_chain);

                    int i = 0;
                    for (const auto &l : producer_node->stages[0].loop) {
                        Var unrolled_var(l.var);

                        int extent = bounds->region_required(i++).extent();
                        producer.in(clone_in_chain).bound_extent(unrolled_var, extent);
                        staged_funcs_schedule_source
                            << "\n    .bound_extent("
                            << unrolled_var.name()
                            << ", "
                            << extent
                            << ")";

                        producer.in(clone_in_chain).unroll(unrolled_var);
                        staged_funcs_schedule_source << "\n    .unroll(" << unrolled_var.name() << ")";
                    }
                    staged_funcs_schedule_source << ";\n";
                }
            }
        }
    }

    return num_loops_tagged_gpu_thread > 0;
}

bool State::can_fuse_gpu(const vector<int64_t> &parallel_extents) const {
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
void State::apply_schedule(const FunctionDAG &dag, const Anderson2021Params &params, const Target &target) {
    StageMap<std::unique_ptr<LoopNest::StageScheduleState>> state_map;
    std::vector<LoopNest::StageScheduleState *> ancestors;

    NodeMap<bool> all_inlined;
    root->collect_all_inlined(all_inlined);
    root->apply(LoopLevel::root(), state_map, params.parallelism, 0, nullptr, nullptr, target, ancestors, all_inlined);

    std::ostringstream src;
    std::unordered_set<std::string> new_serial_vars;

    src << "auto pipeline = get_pipeline();\n";

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
        vector<VarOrRVar> parallel_vars;
        vector<int64_t> parallel_extents;
        vector<int> constant_extents;
        bool any_parallel_vars = false, any_parallel_rvars = false;
        for (auto it = p.second->vars.rbegin(); it != p.second->vars.rend(); it++) {
            if (!it->exists) {
                continue;
            }
            if (!it->parallel) {
                break;
            }
            any_parallel_rvars |= it->var.is_rvar;
            any_parallel_vars |= !it->var.is_rvar;
            parallel_extents.push_back(it->extent);
            parallel_vars.push_back(it->var);
            constant_extents.push_back(it->constant_extent);
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
            fuse_gpu_blocks(p.second.get(), stage, parallel_vars, parallel_extents, constant_extents);
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
            for (const auto &v : storage_vars) {
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

    std::ostringstream staged_funcs_schedule_source;

    if (target.has_gpu_feature()) {
        std::set<const FunctionDAG::Node *> invalid;
        // Iterate from output backwards
        for (const auto &n : dag.nodes) {
            for (auto &p : state_map) {
                if (&n != p.second->node) {
                    continue;
                }

                if (p.first->node->is_input) {
                    continue;
                }

                Stage stage(p.first->stage);

                // If at least one loop has been marked gpu_thread, we need to
                // ensure that it is enclosed by a gpu_block loop. Check if this
                // loop nest or one of its ancestors has been marked gpu_block
                bool has_enclosing_parallel = p.second->parallel;

                if (!has_enclosing_parallel) {
                    for (auto *ancestor : p.second->ancestors) {
                        if (ancestor->parallel) {
                            has_enclosing_parallel = true;
                            break;
                        }
                    }
                }

                bool thread_loop_exists = mark_gpu_threads(p.second.get(), stage, new_serial_vars, staged_funcs_schedule_source);
                // The stage has no threads and no blocks. This is likely an update
                // stage where the reduction is a serial loop
                if (!thread_loop_exists && !has_enclosing_parallel) {
                    stage.gpu_single_thread();
                    p.second->schedule_source << "\n    .gpu_single_thread()";
                    continue;
                }

                if (!thread_loop_exists || has_enclosing_parallel) {
                    continue;
                }

                // There is no outer loop marked as gpu_block.
                // Split the outer loop to create a new outer var with
                // extent = 1 and mark it gpu_blocks()
                const auto &outer_var = p.second->ordered_vars.back();
                vector<VarOrRVar> vars;
                for (const auto &v : p.second->ordered_vars) {
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
                vars.emplace_back(new_outer);
                vars.emplace_back(Var::outermost());

                p.second->schedule_source << "\n    .reorder(";
                bool first = true;

                for (const auto &v : vars) {
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

    for (const auto &v : new_serial_vars) {
        src << "Var " << v << "(\"" << v << "\");\n";
    }

    for (auto &p : state_map) {
        if (p.first->node->is_input) {
            continue;
        }

        // Dump the schedule source string
        src << p.first->name
            << p.second->schedule_source.str()
            << ";\n";
    }

    src << staged_funcs_schedule_source.str();

    // Sanitize the names of things to make them legal source code.
    schedule_source = src.str();
    sanitize_names(schedule_source);
}

bool State::should_always_consider_inline(const FunctionDAG::Node *node) const {
    return always_consider_inline.contains(node) && always_consider_inline.get(node);
}

void State::add_to_always_consider_inline_options(const FunctionDAG::Node *node) {
    always_consider_inline.get_or_create(node) = true;
}

void State::update_always_consider_inline_options(const FunctionDAG::Node *node) {
    if (node->is_output) {
        return;
    }

    if (node->stages.size() > 1) {
        return;
    }

    if (is_func_trivial_to_inline(node->func)) {
        always_consider_inline.get_or_create(node) = true;
        return;
    }

    if (node->is_pointwise) {
        NodeMap<bool> currently_inlined;
        root->collect_all_inlined(currently_inlined);

        std::unordered_set<const FunctionDAG::Node *> non_inlined_consumers;
        std::unordered_set<const FunctionDAG::Node *> done;
        std::vector<const FunctionDAG::Node *> pending;
        pending.push_back(node);

        while (!pending.empty()) {
            const auto *cur_node = pending.back();
            pending.pop_back();

            if (done.count(cur_node)) {
                continue;
            }
            done.insert(cur_node);

            for (const auto *e : cur_node->outgoing_edges) {
                if (!currently_inlined.contains(e->consumer->node) || !currently_inlined.get(e->consumer->node)) {
                    non_inlined_consumers.insert(e->consumer->node);
                    continue;
                }

                pending.push_back(e->consumer->node);
            }
        }

        if (non_inlined_consumers.size() > 1) {
            return;
        }

        internal_assert(non_inlined_consumers.size() == 1);
        always_consider_inline.get_or_create(node) = true;
    }
}

}  // namespace Autoscheduler

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
