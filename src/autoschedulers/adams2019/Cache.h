#ifndef BLOCK_CACHE_H
#define BLOCK_CACHE_H

#include "ASLog.h"
#include "CostModel.h"
#include "Featurization.h"
#include "FunctionDAG.h"
#include "Halide.h"
#include "LoopNest.h"
#include "PerfectHashMap.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

/*
  The adams2019 autoscheduler has two caching implementations within its schedule search:

  1) Block (or tile) caching: handled by this file and Cache.cpp. If block caching is enabled
  the below data structure (Cache) is used to save the tilings that have been generated at prior
  passes of beam search. This allows for faster children generation when tiling is a scheduling
  option. As noted below, this cache is a mapping of the form: Node -> vector_dim -> vector<tiled LoopNest>.

  2) Featurization caching: handled within a LoopNest. The featurization of a LoopNest is used at
  multiple points in beam search (i.e. whenever the featurization of a child LoopNest is computed),
  so it is useful to not repeatedly calculate featurizations. As noted in LoopNest.h, this mapping
  is of the form: (structural hash of producers) -> (StageMap of schedule features). Note that not
  all features can be safely cached (i.e. inlined features), so some must be recomputed (see
  LoopNest::recompute_inlined_features).

  Important changes that caching impacts, outside of this file and Cache.cpp:

  - LoopNest::compute_features
    If cache_features is enabled (i.e. HL_DISABLE_MEMOIZED_FEATURES!=1) then this function caches
    the featurizations of its children, and if called again, reuses those cached featurizations.
    The features are saved in a LoopNest's member, std::map<> features_cache. Some features do not
    persist, and the FeaturesIntermediates struct (see Featurization.h) is used to cache useful
    values that aid in recomputing such features.

  - LoopNest::compute_working_set_from_features
    Used to re-compute the working_set from cached features.

  - LoopNest::recompute_inlined_features
    Recursively recomputes the features of all inlined Funcs based on the cached FeaturesIntermediates
    struct.

  - LoopNest::compute_hash_of_producers_stored_at_root
    Computes a structural hash for use in feature caching in a LoopNest.

  - LoopNest::collect_producers
    Collects all producers for a LoopNest for use in calculating the structural hash in
    LoopNest::compute_hash_of_producers_stored_at_root.

  - LoopNest::collect_stages
    Collects all stages referenced by a LoopNest for use in LoopNest::collect_producers.

  - State::compute_featurization
    Calculates and stores hash_of_producers_stored_at_root for each child if feature caching
    is enabled.

  - State::generate_children
    If block caching is enabled, and tilings for this States have been cached in the Cache object,
    then tilings are not generated again, and the cached tilings are used instead. See
    Cache::add_memoized_blocks below (and in Cache.cpp).
    Additionally, if a tiling has not been cached, and it is not pruned, then the tiling will be
    cached using Cache::memoize_blocks (see below and in Cache.cpp).
*/

struct State;

// true unless HL_DISABLE_MEMOIZED_FEATURES=1
bool use_memoized_features();

// true unless HL_DISABLE_MEMOIZED_BLOCKS=1
bool is_memoize_blocks_enabled();

/*
Object stores caching options for autoscheduling.
cache_blocks: decides if tilings are cached for decisions related to parallelizing the loops of a Func.
cache_features: decides if LoopNest::compute_features will cache / will use cached featurizations.
*/
struct CachingOptions {
    bool cache_blocks = false;
    bool cache_features = false;

    static CachingOptions MakeOptionsFromEnviron() {
        CachingOptions options;
        options.cache_blocks = is_memoize_blocks_enabled();
        options.cache_features = use_memoized_features();
        return options;
    }
};

// Node -> (vector_dim -> vector<tiled LoopNest>)
using BlockCache = NodeMap<std::map<int, std::vector<IntrusivePtr<const LoopNest>>>>;

// Cache for memoizing possible tilings.
// Tracks hit/miss statistics for both block caching
// and for feature caching (self-contained by LoopNests).
struct Cache {
    CachingOptions options;
    BlockCache memoized_compute_root_blocks;

    mutable size_t cache_hits = 0;
    mutable size_t cache_misses = 0;

    Cache() = delete;
    Cache(const CachingOptions &_options, size_t nodes_size)
        : options(_options) {
        if (options.cache_blocks) {
            memoized_compute_root_blocks.make_large(nodes_size);
        }
    }

    ~Cache() = default;

    // check if we generated tilings for the current func on a previous pass
    // if so, add them and return true.
    // otherwise, return false (also return false if memoization is turned off).
    bool add_memoized_blocks(const State *state,
                             std::function<void(IntrusivePtr<State> &&)> &accept_child,
                             const FunctionDAG::Node *node,
                             int &num_children,
                             const FunctionDAG &dag,
                             const MachineParams &params,
                             CostModel *cost_model,
                             int64_t memory_limit) const;

    // Generate tilings for a specific vector dimension and memoize them.
    void memoize_blocks(const FunctionDAG::Node *node, LoopNest *new_root);
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif  // BLOCK_CACHE_H
