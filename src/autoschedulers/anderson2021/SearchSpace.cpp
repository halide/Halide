#include "SearchSpace.h"

using std::vector;

namespace Halide {
namespace Internal {
namespace Autoscheduler {

SearchSpace::SearchSpace(const FunctionDAG &dag,
                         const Internal::Autoscheduler::Anderson2021Params &params,
                         const Target &target,
                         std::mt19937 &rng,
                         CostModel *cost_model,
                         Statistics &stats,
                         const LoopNestParser *partial_schedule)
    : dag{dag},
      params{params},
      target{target},
      search_space_options{params.search_space_options},
      rng{rng},
      cost_model{cost_model},
      stats{stats},
      partial_schedule{partial_schedule} {
    memoized_compute_root_blocks.make_large(dag.nodes.size());
}

void SearchSpace::memoize_blocks(const FunctionDAG::Node *node,
                                 LoopNest *new_root) {
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

    auto &blocks = memoized_compute_root_blocks.get_or_create(node)[vector_dim];

    for (auto &c : new_root->children) {
        if (c->node == node) {
            LoopNest *new_block = new LoopNest;
            new_block->copy_from_including_features(*c);
            blocks.emplace_back(new_block);
            ++stats.num_block_memoization_misses;
        }
    }
}

bool SearchSpace::add_states_from_memoized_blocks(const IntrusivePtr<State> &state,
                                                  std::function<void(IntrusivePtr<State> &&)> &accept_child,
                                                  const FunctionDAG::Node *node,
                                                  int &num_children) const {
    if (!memoized_compute_root_blocks.contains(node)) {
        return false;
    }

    int vector_dim = -1;
    for (const auto &c : state->root->children) {
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
        auto child = state->make_child();
        LoopNest *new_root = new LoopNest;
        new_root->copy_from(*state->root);
        child->root = new_root;
        child->num_decisions_made++;

        int block_index = 0;
        for (const auto &c : new_root->children) {
            if (c->node == node) {
                break;
            }
            ++block_index;
        }

        for (size_t j = 0; j < num_stages; ++j) {
            LoopNest *new_block = new LoopNest;
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

vector<SearchSpace::ParallelTileOption> SearchSpace::filter_parallel_tile_options(const IntrusivePtr<State> &state,
                                                                                  const FunctionDAG::Node *node,
                                                                                  vector<vector<int64_t>> &inner_tilings,
                                                                                  const vector<int64_t> &pure_size) const {
    vector<SearchSpace::ParallelTileOption> options;
    vector<SearchSpace::ParallelTileOption> insufficient_parallelism;
    for (auto &t : inner_tilings) {
        SearchSpace::ParallelTileOption o;
        o.inner_tiling = t;

        for (size_t j = 0; j < pure_size.size(); j++) {
            t[j] = (pure_size[j] + t[j] - 1) / t[j];
        }

        t.swap(o.outer_tiling);

        // Compute max idle cores across the other stages of the Func
        int64_t min_total = 0, max_total = 0;
        o.idle_core_wastage = 1;
        for (const auto &c : state->root->children) {
            if (c->node == node) {
                int64_t total = 1;
                int64_t max_available = 1;
                for (const auto &l : c->stage->loop) {
                    if (!l.rvar) {
                        total *= o.outer_tiling[l.pure_dim];
                        max_available *= c->size[l.pure_dim];
                    }
                }
                max_total = std::max(max_total, total);

                // If a stage does not have enough parallelism regardless of the
                // tiling (i.e. its size is < params.parallelism * 2 before
                // splitting), then the only tiling worth considering is the
                // one that retains the full extent in this dimension
                // (outer_tiling == size). In that case, skip over updating
                // min_total, otherwise it will be filtered out below
                if (max_available >= params.parallelism * 2 || total != max_available) {
                    if (min_total != 0) {
                        min_total = std::min(min_total, total);
                    } else {
                        min_total = total;
                    }
                    const double tasks_per_core = ((double)total) / params.parallelism;
                    o.idle_core_wastage = std::max(o.idle_core_wastage,
                                                   std::ceil(tasks_per_core) / tasks_per_core);
                }
            }
        }

        o.min_parallelism = min_total;
        o.max_parallelism = max_total;

        // Filter out the less useful options
        bool ok =
            (min_total >= params.parallelism * 2 &&
             (max_total <= params.parallelism * 16 || target.has_gpu_feature()));

        if (!ok) {
            insufficient_parallelism.emplace_back(std::move(o));
            continue;
        }

        options.emplace_back(std::move(o));
    }

    int64_t parallelism_limit = params.parallelism;
    while (options.empty()) {
        for (auto &o : insufficient_parallelism) {
            if (o.min_parallelism >= parallelism_limit) {
                options.emplace_back(std::move(o));
            }
        }

        parallelism_limit /= 2;
    }

    std::sort(options.begin(), options.end());

    return options;
}

vector<ThreadTileOption> SearchSpace::filter_thread_tile_options(vector<IntrusivePtr<const LoopNest>> &loop_nests) const {
    vector<ThreadTileOption> options;
    for (const auto &loop_nest : loop_nests) {
        if (!loop_nest->has_valid_thread_extents()) {
            Filter(loop_nest.get()) << "Invalid thread extents\n";
            continue;
        }

        ThreadTileOption o;
        o.loop_nest = loop_nest;
        o.max_idle_lane_wastage = loop_nest->max_idle_lane_wastage(target, GPULoopInfo(loop_nest.get()));
        options.emplace_back(std::move(o));
    }

    std::sort(options.begin(), options.end());

    return options;
}

void SearchSpace::process_pending_states(std::unordered_map<uint64_t, StateVector> &primary_options,
                                         std::unordered_map<uint64_t, StateVector> &secondary_options,
                                         int &num_children,
                                         std::function<void(IntrusivePtr<State> &&)> &accept_child,
                                         const FunctionDAG::Node *node) {
    for (auto &entry : primary_options) {
        size_t N = entry.second.size();
        if (N > 1 && !is_in_partial_schedule(node)) {
            N = std::log2(entry.second.size());
        }

        std::shuffle(entry.second.begin(), entry.second.end(), rng);

        size_t accepted = 0;
        for (size_t i = 0; i < entry.second.size() && accepted < N; ++i) {
            if (entry.second[i]->calculate_cost(dag, params, target, cost_model, stats)) {
                num_children++;
                accept_child(std::move(entry.second[i]));
                accepted++;
                stats.num_tilings_accepted++;
            }
        }
    }

    if (num_children > 0) {
        return;
    }

    for (auto &entry : secondary_options) {
        for (auto &state : entry.second) {
            if (state->calculate_cost(dag, params, target, cost_model, stats)) {
                num_children++;
                accept_child(std::move(state));
                stats.num_tilings_accepted++;
                break;
            }
        }
    }
}

void SearchSpace::generate_children(const IntrusivePtr<State> &state,
                                    std::function<void(IntrusivePtr<State> &&)> &accept_child,
                                    int pass_idx,
                                    bool is_pre_pass) {
    const IntrusivePtr<const LoopNest> root = state->root;

    internal_assert(root.defined() && root->is_root());

    if (state->num_decisions_made == 2 * (int)dag.nodes.size()) {
        return;
    }

    int next_node = state->num_decisions_made / 2;
    int phase = state->num_decisions_made % 2;

    if (!may_subtile(params)) {
        // When emulating the older search space, we do all
        // parallelizing last, so that it is independent of the
        // tiling decisions.
        next_node = state->num_decisions_made % dag.nodes.size();
        phase = state->num_decisions_made / dag.nodes.size();
    }

    // Enumerate all legal ways to schedule the next Func
    const FunctionDAG::Node *node = &dag.nodes[next_node];
    for (const auto *e : node->outgoing_edges) {
        internal_assert(root->computes(e->consumer->node))
            << "Partially scheduled code doesn't compute " << e->consumer->name
            << ", which is one of the consumers of " << node->func.name();
    }

    // ScopedTimer scoped_timer{"generate_children() for " + node->func.name()};
    bool must_inline = inlined_nodes.contains(node);
    bool must_compute_root = compute_root_nodes.contains(node);

    if (node->is_input || (phase == 1 && must_compute_root)) {
        // We don't need to schedule nodes that represent inputs,
        // and there are no other decisions to be made about them
        // at this time.
        // aslog(1) << "Skipping over scheduling input node: " << node->func.name() << "\n";
        auto child = state->make_child();
        child->num_decisions_made++;
        accept_child(std::move(child));
        return;
    }

    if (!node->outgoing_edges.empty() && !root->calls(node)) {
        aslog(1) << "In state:\n";
        state->dump();
        aslog(1) << node->func.name() << " is consumed by:\n";
        for (const auto *e : node->outgoing_edges) {
            aslog(1) << e->consumer->name << "\n";
            aslog(1) << "Which in turn consumes:\n";
            for (const auto *e2 : e->consumer->incoming_edges) {
                aslog(1) << "  " << e2->producer->func.name() << "\n";
            }
        }
        internal_error << "Pipeline so far doesn't use next Func: " << node->func.name() << '\n';
    }

    int num_children = 0;

    if (phase == 0) {
        // Injecting realizations
        {
            state->update_always_consider_inline_options(node);

            if (is_in_partial_schedule(node)) {
                state->add_to_always_consider_inline_options(node);
            }

            // 1) Inline it
            if (search_space_options.compute_inline() && node->stages.size() == 1 && !node->is_output && !must_compute_root) {
                LoopNest *new_root = new LoopNest;
                new_root->copy_from(*root);
                new_root->inline_func(node);
                if (add_child(state, new_root, accept_child)) {
                    num_children++;
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

            return;
        }

        if (must_compute_root) {
            LoopNest *new_root = new LoopNest;
            new_root->copy_from(*root);
            const auto &nodes = compute_root_nodes.get(node);
            for (const auto &n : nodes) {
                const auto *compute_root_loop = deep_copy_loop_nest(n, NoOpMutator{});
                new_root->children.emplace_back(compute_root_loop);
            }
            new_root->store_at.insert(node);

            add_child(state, new_root, accept_child);
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
                    if (!is_in_partial_schedule(node)) {
                        break;
                    }
                }
            }
        }
        // Outputs must be vectorized over their innermost
        // dimension, because we don't have control of the
        // storage. TODO: Check which dimension has a stride==1
        // constraint instead of assuming 0.
        if (vector_dims.empty()) {
            vector_dims.push_back(0);
        }

        // 2) Realize it somewhere
        std::unordered_map<uint64_t, StateVector> primary_options;
        std::unordered_map<uint64_t, StateVector> secondary_options;
        for (int vector_dim : vector_dims) {
            Timer timer;
            auto tile_options = root->compute_in_tiles(node,
                                                       nullptr,
                                                       params,
                                                       target,
                                                       search_space_options,
                                                       vector_dim,
                                                       false,
                                                       false,
                                                       is_pre_pass);
            stats.compute_in_tiles_time += timer.elapsed();

            timer.restart();
            auto options = filter_thread_tile_options(tile_options);
            stats.filter_thread_tiles_time += timer.elapsed();

            for (const auto &o : options) {
                if (!params.randomize_tilings && num_children >= 1 && o.max_idle_lane_wastage > 0.5) {
                    Filter(o.loop_nest.get()) << "Excess idle lane wastage\n"
                                              << "max_idle_lane_wastage = " << o.max_idle_lane_wastage << "\n";
                    break;
                }

                ++stats.num_tilings_generated;

                if (!params.randomize_tilings) {
                    if (add_child(state, o.loop_nest, accept_child)) {
                        num_children++;
                    }
                    continue;
                }

                auto child = state->make_child();
                child->root = o.loop_nest;
                child->num_decisions_made++;
                uint64_t h = child->structural_hash(pass_idx);

                if (o.max_idle_lane_wastage > 0.5) {
                    secondary_options[h].push_back(child);
                    continue;
                }

                primary_options[h].push_back(child);
            }
        }

        if (params.randomize_tilings) {
            process_pending_states(primary_options, secondary_options, num_children, accept_child, node);
        }
    } else {
        // We are parallelizing the loops of the func we just injected a realization for.

        bool should_parallelize = false;
        IntrusivePtr<const LoopNest> pure_stage;

        if (params.parallelism > 1) {
            for (const auto &c : root->children) {
                if (c->node == node && node->dimensions > 0) {
                    if (c->stage->index == 0) {
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
            auto child = state->make_child();
            child->num_decisions_made++;
            accept_child(std::move(child));
            return;
        }

        if (add_states_from_memoized_blocks(state, accept_child, node, num_children)) {
            return;
        }

        // When GPU scheduling we approach tiling in two steps.
        // step 1) convert (none, SIMD) loops to (parallel, serial, SIMD) loops with specialized serial sizes
        auto parallel_tilings = generate_compute_root_serial_tilings(pure_stage, node);

        internal_assert(!parallel_tilings.empty()) << " zero parallel tilings\n";

        std::unordered_map<uint64_t, std::vector<IntrusivePtr<State>>> primary_options;
        std::unordered_map<uint64_t, std::vector<IntrusivePtr<State>>> secondary_options;
        for (auto &parallel_t : parallel_tilings) {
            LoopNest parallel_root;
            parallel_root.copy_from(*root);

            // step 1) parallelize all loop nests for this node into (parallel, serial) with given serial tiles
            for (auto &c : parallel_root.children) {
                if (c->node == node) {
                    c = c->parallelize_in_tiles(parallel_t, &parallel_root, params, target, false, true);
                }
            }

            // step 2) split all parallel loops for this node into to (blocks, thread) loop
            vector<vector<int64_t>> stage_sizes;
            vector<vector<int>> pure_dims;
            vector<int> vectorized_indices;
            parallel_root.get_stage_sizes(node, stage_sizes, pure_dims, vectorized_indices);
            // at root level sibling thread counts are in separate blocks, extents are irrelevant
            vector<int64_t> max_size((int)(stage_sizes[0].size()), 1);

            auto block_tilings = generate_gpu_tilings(stage_sizes,
                                                      pure_dims,
                                                      max_size,
                                                      node->dimensions - 1,
                                                      vectorized_indices,
                                                      false,
                                                      true);

            // If no options, create a thread tiling as large as possible with block size (1,1,1).
            // This can happen if the loops are too small to generate desired gpu tiles.
            if (block_tilings.empty()) {
                LoopNest *new_root = new LoopNest;
                new_root->copy_from(parallel_root);
                for (auto &c : new_root->children) {
                    if (c->node == node) {
                        vector<int64_t> tiling((int)(c->size.size()), 1);
                        c = c->parallelize_in_tiles(tiling, new_root, params, target, false, true);
                    }
                }
                if (add_child(state, new_root, accept_child)) {
                    num_children++;
                    memoize_blocks(node, new_root);
                }
                internal_assert(false) << "block tilings empty";
                return;
            }

            Timer timer;
            auto options = filter_parallel_tile_options(state, node, block_tilings, stage_sizes[0]);
            stats.filter_parallel_tiles_time += timer.elapsed();

            double prev_idle_core_wastage = 0;
            for (const auto &o : options) {
                if (!params.randomize_tilings &&
                    num_children >= 1 &&
                    o.idle_core_wastage > 1.2 &&
                    o.idle_core_wastage != prev_idle_core_wastage) {
                    // We have considered several options, and the
                    // remaining ones leave lots of cores idle.
                    break;
                }
                prev_idle_core_wastage = o.idle_core_wastage;

                ++stats.num_tilings_generated;

                LoopNest *new_root = new LoopNest;
                new_root->copy_from(parallel_root);

                for (auto &c : new_root->children) {
                    if (c->node == node) {
                        c = c->parallelize_in_tiles(o.inner_tiling, new_root, params, target, true, false);
                    }
                }

                if (!params.randomize_tilings) {
                    if (add_child(state, new_root, accept_child)) {
                        num_children++;
                        memoize_blocks(node, new_root);
                    }
                    continue;
                }

                auto child = state->make_child();
                child->root = new_root;
                child->num_decisions_made++;
                uint64_t h = child->structural_hash(pass_idx);

                if (o.idle_core_wastage > 1.2) {
                    secondary_options[h].push_back(child);
                    continue;
                }

                primary_options[h].push_back(child);
            }
        }

        if (params.randomize_tilings) {
            process_pending_states(primary_options, secondary_options, num_children, accept_child, node);
        }
    }

    if (num_children == 0) {
        aslog(1) << "Warning: Found no legal way to schedule "
                 << node->func.name() << " in the following State:\n";
        state->dump();
        // All our children died. Maybe other states have had
        // children. Carry on.
    }
}

struct ClearInlinedMutator {
    void operator()(LoopNest *new_loop_nest) const {
        new_loop_nest->inlined = {};
    }
};

void SearchSpace::freeze_lowest_cost_stages(const IntrusivePtr<State> &best) {
    std::vector<std::pair<int, double>> node_ids_and_costs;
    NodeMap<double> node_costs;
    size_t num_nodes = 0;
    for (const auto &n : dag.nodes) {
        if (n.is_input) {
            continue;
        }

        int i = 0;
        for (const auto &s : n.stages) {
            if (!node_costs.contains(dag.stage_id_to_node_map.at(s.id))) {
                node_costs.get_or_create(dag.stage_id_to_node_map.at(s.id)) = 0;
            }

            node_costs.get(dag.stage_id_to_node_map.at(s.id)) += best->cost_per_stage[i++];
        }

        ++num_nodes;
    }

    for (auto it = node_costs.begin(); it != node_costs.end(); it++) {
        node_ids_and_costs.emplace_back(it.key()->id, it.value());
    }

    for (const auto &n : node_ids_and_costs) {
        internal_assert(n.first >= 0);
    }

    std::sort(node_ids_and_costs.begin(), node_ids_and_costs.end(),
              [](const std::pair<int, double> &a, const std::pair<int, double> &b) { return a.second < b.second; });

    size_t num_to_freeze = num_nodes - std::log2(num_nodes);
    NodeMap<bool> nodes_to_freeze;
    for (size_t i = 0; i < num_to_freeze; ++i) {
        auto id = node_ids_and_costs[i].first;
        std::cerr << "Freezing " << dag.nodes[id].func.name() << " with cost = " << node_ids_and_costs[i].second << "\n";
        nodes_to_freeze.insert(&dag.nodes[id], true);
    }

    best->root->collect_nodes_that_should_be_inlined(nodes_to_freeze, inlined_nodes);

    ClearInlinedMutator mutator{};

    for (const auto &c : best->root->children) {
        if (nodes_to_freeze.contains(c->node)) {
            auto *new_loop_nest = deep_copy_loop_nest(c, mutator);
            compute_root_nodes.get_or_create(c->node).emplace_back(new_loop_nest);
            std::cerr << "Freezing as compute_root: " << c->node->func.name() << "\n";
        }
    }
}

vector<vector<int64_t>> SearchSpace::generate_compute_root_serial_tilings(const IntrusivePtr<const LoopNest> &pure_stage,
                                                                          const FunctionDAG::Node *node) const {
    std::vector<int> vec_dim_serial_sizes;
    pure_stage->generate_vec_dim_serial_tilings(vec_dim_serial_sizes);

    return generate_serial_tilings(pure_stage->size,
                                   node->dimensions - 1,
                                   node->dimensions - 1,
                                   pure_stage->vectorized_loop_index,
                                   vec_dim_serial_sizes,
                                   false,
                                   true);
}

bool SearchSpace::add_child(const IntrusivePtr<State> &state,
                            const IntrusivePtr<const LoopNest> &new_root,
                            std::function<void(IntrusivePtr<State> &&)> &accept_child) const {
    auto child = state->make_child();
    child->root = new_root;
    child->num_decisions_made++;
    if (child->calculate_cost(dag, params, target, cost_model, stats)) {
        accept_child(std::move(child));
        return true;
    }
    return false;
}

bool SearchSpace::is_in_partial_schedule(const FunctionDAG::Node *node) const {
    return partial_schedule && partial_schedule->is_in_partial_schedule(node);
}

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide
