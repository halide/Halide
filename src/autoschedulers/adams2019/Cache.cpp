#include "Cache.h"
#include "LoopNest.h"
#include "State.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

bool use_memoized_features() {
    return get_env_variable("HL_DISABLE_MEMOIZED_FEATURES") != "1";
}

bool is_memoize_blocks_enabled() {
    return get_env_variable("HL_DISABLE_MEMOIZED_BLOCKS") != "1";
}

bool Cache::add_memoized_blocks(const State *state,
                                std::function<void(IntrusivePtr<State> &&)> &accept_child,
                                const FunctionDAG::Node *node, int &num_children,
                                const FunctionDAG &dag,
                                const Adams2019Params &params,
                                CostModel *cost_model,
                                int64_t memory_limit) const {
    if (!options.cache_blocks || !memoized_compute_root_blocks.contains(node)) {
        // either memoization is turned off, or we haven't cached this node yet.
        return false;
    }

    // get correct vector dimension.
    int vector_dims = -1;
    for (const auto &child : state->root->children) {
        if (child->node == node && child->stage->index == 0) {
            vector_dims = child->vector_dim;
            break;
        }
    }

    const auto &vector_dim_map = memoized_compute_root_blocks.get(node);

    if (vector_dim_map.count(vector_dims) == 0) {
        // Never cached this vector dimension before.
        return false;
    }

    auto blocks = vector_dim_map.at(vector_dims);

    size_t num_stages = node->stages.size();

    for (size_t i = 0; i < blocks.size(); i += num_stages) {
        // Construct child from memoization.
        IntrusivePtr<State> child = state->make_child();
        LoopNest *new_root = new LoopNest;
        new_root->copy_from(*(state->root));
        child->root = new_root;
        child->num_decisions_made++;

        int block_index = 0;
        for (const auto &new_child : new_root->children) {
            if (new_child->node == node) {
                break;
            }
            block_index++;
        }

        // Copy all stages into new_root.
        for (size_t j = 0; j < num_stages; j++) {
            LoopNest *new_block = new LoopNest;
            new_block->copy_from_including_features(*blocks[i + j]);
            new_root->children[block_index++] = new_block;
        }

        if (child->calculate_cost(dag, params, cost_model, this->options, memory_limit)) {
            num_children++;
            accept_child(std::move(child));
            cache_hits++;
        }
    }

    // succesfully added cached items!
    return true;
}

void Cache::memoize_blocks(const FunctionDAG::Node *node, LoopNest *new_root) {
    if (!options.cache_blocks) {
        return;
    }

    int vector_dim = -1;
    bool loop_nest_found = false;

    for (auto &child : new_root->children) {
        if (child->node == node && child->stage->index == 0) {
            vector_dim = child->vector_dim;
            loop_nest_found = true;
            break;
        }
    }

    internal_assert(loop_nest_found) << "memoize_blocks did not find loop nest!\n";

    auto &blocks = memoized_compute_root_blocks.get_or_create(node)[vector_dim];

    for (auto &child : new_root->children) {
        if (child->node == node) {
            LoopNest *new_block = new LoopNest;
            // Need const reference for copy.
            const LoopNest *child_ptr = child.get();
            new_block->copy_from_including_features(*child_ptr);
            blocks.emplace_back(new_block);
            cache_misses++;
        }
    }
}

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide
