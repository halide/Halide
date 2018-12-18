/*
    Env vars used (directly or indirectly):

    TODO(someone): document all these

    HL_AUTO_SCHEDULE_TIME_LIMIT
    HL_BEAM_SIZE
    HL_CYOS
    HL_FEATURE_FILE -> output
    HL_MACHINE_PARAMS
    HL_PERMIT_FAILED_UNROLL
    HL_RANDOM_DROPOUT
    HL_SCHEDULE_FILE
    HL_SEED
    HL_USE_MANUAL_COST_MODEL
    HL_WEIGHTS_DIR

*/
#include <set>
#include <queue>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <iostream>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>

#include <Halide.h>
//#include "halide_benchmark.h"
//#include "ThroughputPredictorPipeline.h"
#include "Errors.h"
#include "featurization.h"
#include "function_dag.h"

namespace Halide {
namespace Internal {

namespace {

using std::string;
using std::vector;
using std::map;
using std::set;
using std::pair;


}
/*
template<>
RefCount &ref_count<BoundContents>(const BoundContents *t) {return t->ref_count;}

template<>
void destroy<BoundContents>(const BoundContents *t) {
    // Release it back into the memory pool to be reused
    t->layout->release(t);
}*/

namespace {
  #if 0
vector<vector<int64_t>> generate_tilings(const vector<int64_t> &s, int d, int factor, bool allow_splits, int vector_dim, int vector_size) {
    vector<vector<int64_t>> result;
    if (d == -1) {
        result.push_back(vector<int64_t>());
    } else {
        vector<vector<int64_t>> v;
        v = generate_tilings(s, d - 1, factor, allow_splits, vector_dim, vector_size);
        // If we're already generated tons of tiling configs for the
        // inner loops, search the outer loops with coarser
        // granularity.
        while (v.size() > (size_t)factor * 100) {
            factor *= 2;
        }

        for (auto t : v) {
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
                if (s[d] != 1 && !is_full && is_one && (d != vector_dim)) {
                    t.back() = s[d];
                    result.push_back(t);
                }
            } else {
                int max_inner = 0;
                int first_inner = (d == vector_dim) ? vector_size : 1;
                for (int inner = first_inner; inner < s[d]; inner *= factor) {
                    int outer = (s[d] + inner - 1) / inner;
                    if (is_one && outer == 1) continue;
                    if (is_full && outer == s[d]) continue;
                    // Stop when we hit inner sizes that would do too much recompute
                    if (inner > first_inner && inner * outer * 7 > s[d] * 8) break;
                    max_inner = inner;
                    t.back() = outer;
                    result.push_back(t);
                }
                for (int outer = 1; outer <= s[d]; outer *= factor) {
                    int inner = (s[d] + outer - 1) / outer;
                    if (is_one && outer == 1) continue;
                    if (is_full && outer == s[d]) continue;
                    // Stop when we get into the regime covered by the loop above.
                    if (inner < max_inner * 2) break;
                    // Or when the wasted compute gets too bad.
                    if (inner * outer * 7 > s[d] * 8) break;
                    t.back() = outer;
                    result.push_back(t);
                }

                // The sequence above (in terms of the inner loop) goes 1 2 4 8 16 ...
                // but 3 is an important inner tiling factor for matrix multiply ops.
                int inner3 = (d == vector_dim) ? 3*vector_size : 3;
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
#endif


// A specialized hash map used in this file. It can only grow, and it
// requires a perfect hash in the form of "id" and "max_id" fields on
// each key. If the keys don't all have a consistent max_id, or if you
// call make_large with the wrong max_id, you get UB. If you think
// that might be happening, uncomment the assertions below for some
// extra checking.
template<typename K, typename T, int max_small_size = 4>
class PerfectHashMap {
    using storage_type = std::vector<std::pair<const K *, T>>;

    storage_type storage;

    int occupied = 0;

    // Equivalent to storage[i], but broken out into a separate method
    // to allow for bounds checks when debugging this.
    pair<const K *, T> &storage_bucket(int i) {
        /*
        internal_assert(i >= 0 && i < (int)storage.size())
            << "Out of bounds access: " << i << " " << storage.size() << "\n";
        */
        return storage[i];
    }

    const pair<const K *, T> &storage_bucket(int i) const {
        /*
        internal_assert(i >= 0 && i < (int)storage.size())
            << "Out of bounds access: " << i << " " << storage.size() << "\n";
        */
        return storage[i];
    }

    enum {
        Empty = 0, // No storage allocated
        Small = 1, // Storage is just an array of key/value pairs
        Large = 2  // Storage is an array with empty slots, indexed by the 'id' field of each key
    } state = Empty;

    void upgrade_from_empty_to_small() {
        storage.resize(max_small_size);
        state = Small;
    }

    void upgrade_from_empty_to_large(int n) {
        storage.resize(n);
        state = Large;
    }

    void upgrade_from_small_to_large(int n) {
        internal_assert(occupied <= max_small_size) << occupied << " " << max_small_size << "\n";
        storage_type tmp(n);
        state = Large;
        tmp.swap(storage);
        int o = occupied;
        for (int i = 0; i < o; i++) {
            emplace_large(tmp[i].first, std::move(tmp[i].second));
        }
    }

    // Methods when the map is in the empty state
    T &emplace_empty(const K *n, T &&t) {
        upgrade_from_empty_to_small();
        storage_bucket(0).first = n;
        storage_bucket(0).second = std::move(t);
        occupied = 1;
        return storage_bucket(0).second;
    }

    const T &get_empty(const K *n) const {
        internal_error << "Calling get on an empty PerfectHashMap";
        return unreachable_value();
    }

    T &get_empty(const K *n) {
        internal_error << "Calling get on an empty PerfectHashMap";
        return unreachable_value();
    }

    T &get_or_create_empty(const K *n) {
        occupied = 1;
        return emplace_empty(n, T());
    }

    bool contains_empty(const K *n) const {
        return false;
    }

    // Methods when the map is in the small state
    int find_index_small(const K *n) const {
        int i;
        for (i = 0; i < (int)occupied; i++) {
            if (storage_bucket(i).first == n) return i;
        }
        return i;
    }

    T &emplace_small(const K *n, T &&t) {
        int idx = find_index_small(n);
        if (idx >= max_small_size) {
            upgrade_from_small_to_large((int)(n->max_id));
            return emplace_large(n, std::move(t));
        }
        auto &p = storage_bucket(idx);
        if (p.first == nullptr) {
            occupied++;
            p.first = n;
        }
        p.second = std::move(t);
        return p.second;
    }

    const T &get_small(const K *n) const {
        int idx = find_index_small(n);
        return storage_bucket(idx).second;
    }

    T &get_small(const K *n) {
        int idx = find_index_small(n);
        return storage_bucket(idx).second;
    }

    T &get_or_create_small(const K *n) {
        int idx = find_index_small(n);
        if (idx >= max_small_size) {
            upgrade_from_small_to_large((int)(n->max_id));
            return get_or_create_large(n);
        }
        auto &p = storage_bucket(idx);
        if (p.first == nullptr) {
            occupied++;
            p.first = n;
        }
        return p.second;
    }

    bool contains_small(const K *n) const {
        int idx = find_index_small(n);
        return (idx < max_small_size) && (storage_bucket(idx).first == n);
    }

    // Methods when the map is in the large state
    T &emplace_large(const K *n, T &&t) {
        auto &p = storage_bucket(n->id);
        if (!p.first) occupied++;
        p.first = n;
        p.second = std::move(t);
        return p.second;
    }

    const T &get_large(const K *n) const {
        return storage_bucket(n->id).second;
    }

    T &get_large(const K *n) {
        return storage_bucket(n->id).second;
    }

    T &get_or_create_large(const K *n) {
        auto &p = storage_bucket(n->id);
        if (p.first == nullptr) {
            occupied++;
            p.first = n;
        }
        return storage_bucket(n->id).second;
    }

    bool contains_large(const K *n) const {
        return storage_bucket(n->id).first != nullptr;
    }

    void check_key(const K *n) const {
        /*
        internal_assert(n->id >= 0 && n->id < n->max_id)
            << "Invalid hash key: " << n->id << " " << n->max_id << "\n";
        internal_assert(state != Large || (int)storage.size() == n->max_id)
            << "Inconsistent key count: " << n->max_id << " vs " << storage.size() << "\n";
        */
    }

    // Helpers used to pacify compilers
    T &unreachable_value() {
        return storage_bucket(0).second;
    }

    const T &unreachable_value() const {
        return storage_bucket(0).second;
    }

public:

    // Jump straight to the large state
    void make_large(int n) {
        if (state == Empty) upgrade_from_empty_to_large(n);
        else if (state == Small) upgrade_from_small_to_large(n);
    }

    T &emplace(const K *n, T &&t) {
        check_key(n);
        switch(state) {
        case Empty: return emplace_empty(n, std::move(t));
        case Small: return emplace_small(n, std::move(t));
        case Large: return emplace_large(n, std::move(t));
        }
        return unreachable_value();
    }

    T &insert(const K *n, const T &t) {
        check_key(n);
        T tmp(t);
        switch(state) {
        case Empty: return emplace_empty(n, std::move(tmp));
        case Small: return emplace_small(n, std::move(tmp));
        case Large: return emplace_large(n, std::move(tmp));
        }
        return unreachable_value();
    }

    const T &get(const K *n) const {
        check_key(n);
        switch(state) {
        case Empty: return get_empty(n);
        case Small: return get_small(n);
        case Large: return get_large(n);
        }
        return unreachable_value();
    }

    T &get(const K *n) {
        check_key(n);
        switch(state) {
        case Empty: return get_empty(n);
        case Small: return get_small(n);
        case Large: return get_large(n);
        }
        return unreachable_value();
    }

    T &get_or_create(const K *n) {
        check_key(n);
        switch(state) {
        case Empty: return get_or_create_empty(n);
        case Small: return get_or_create_small(n);
        case Large: return get_or_create_large(n);
        }
        return unreachable_value();
    }

    bool contains(const K *n) const {
        check_key(n);
        switch(state) {
        case Empty: return contains_empty(n);
        case Small: return contains_small(n);
        case Large: return contains_large(n);
        }
        return false; // Unreachable
    }

    size_t size() const {
        return occupied;
    }

    struct iterator {
        pair<const K *, T> *iter, *end;

        void operator++(int) {
            do {
                iter++;
            } while (iter != end && iter->first == nullptr);
        }

        const K *key() const {
            return iter->first;
        }

        T &value() const {
            return iter->second;
        }

        bool operator!=(const iterator &other) const {
            return iter != other.iter;
        }

        bool operator==(const iterator &other) const {
            return iter == other.iter;
        }
    };

    struct const_iterator {
        const pair<const K *, T> *iter, *end;

        void operator++(int) {
            do {
                iter++;
            } while (iter != end && iter->first == nullptr);
        }

        const K *key() const {
            return iter->first;
        }

        const T &value() const {
            return iter->second;
        }

        bool operator!=(const const_iterator &other) const {
            return iter != other.iter;
        }

        bool operator==(const const_iterator &other) const {
            return iter == other.iter;
        }
    };

    iterator begin() {
        if (state == Empty) return end();
        iterator it;
        it.iter = storage.data();
        it.end = it.iter + storage.size();
        if (it.key() == nullptr) it++;
        internal_assert(it.iter == it.end || it.key());
        return it;
    }

    iterator end() {
        iterator it;
        it.iter = it.end = storage.data() + storage.size();
        return it;
    }

    const_iterator begin() const {
        if (storage.empty()) return end();
        const_iterator it;
        it.iter = storage.data();
        it.end = it.iter + storage.size();
        if (it.key() == nullptr) it++;
        internal_assert(it.iter == it.end || it.key());
        return it;
    }

    const_iterator end() const {
        const_iterator it;
        it.iter = it.end = storage.data() + storage.size();
        return it;
    }
};


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

    // The extents of the loops
    vector<int64_t> size;

    // The nodes inside the loop body
    vector<IntrusivePtr<const LoopNest>> children;

    // Funcs inlined into this inner loop, and the number of times they are called. Only valid if children is empty.
    NodeMap<int64_t> inlined;

    // Funcs realized inside this inner loop
    set<const FunctionDAG::Node *> store_at;

    // The total bounds required of the given Func for one
    // representative iteration of this loop. Computed lazily and
    // cached. entries are immutable so that bounds are shared across
    // different instances.
    mutable NodeMap<IntrusivePtr<const BoundContents>> bounds;

    const FunctionDAG::Node *node = nullptr;
    const FunctionDAG::Node::Stage *stage = nullptr;
    int stage_idx = 0;

    // Is this the innermost loop of this func?
    bool innermost = false;

    // Are we permitted to tile this loop?
    bool tileable = false;

    // What dimension is this Func vectorized over, in terms of the args of the Func?
    int vector_dim = -1;

    // Which loop is vectorized. -1 means none of them.
    int vectorized_loop_index = -1;

    void copy_from(const LoopNest &n) {
        size = n.size;
        children = n.children;
        inlined = n.inlined;
        store_at = n.store_at;
        bounds = n.bounds;
        node = n.node;
        stage = n.stage;
        stage_idx = n.stage_idx;
        innermost = n.innermost;
        tileable = n.tileable;
    };

    static void hash_combine(uint64_t &h, uint64_t next) {
        // From boost
        h ^= (next + 0x9e3779b9 + (h<<6) + (h>>2));
    }

    // Hash the loop structure and sizes up to a fixed depth
    void structural_hash(uint64_t &h, int depth, int parallelism) const {
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
            // What are their loop sizes?
            for (const auto &c : children) {
                for (int64_t s : c->size) {
                    if (depth == 1) {
                        // Just take the most significant bit: is it more
                        // or less than the parallelism factor.
                        s = s >= parallelism ? 1 : 0;
                    }
                    hash_combine(h, s);
                }
            }
        }

        if (innermost) {
            // Which dimension are we vectorized over?
            hash_combine(h, vectorized_loop_index);
        }

        if (depth > 1) {
            // Descend into children
            for (const auto &c : children) {
                c->structural_hash(h, depth - 2, parallelism);
            }
        }
    }

    size_t funcs_realized_or_inlined() const {
        size_t count = inlined.size() + store_at.size();
        for (auto c : children) {
            count += c->funcs_realized_or_inlined();
        }
        return count;
    }

    struct Sites {
        const LoopNest *compute = nullptr, *store = nullptr, *produce = nullptr, *innermost = nullptr;
    };

    void get_sites(NodeMap<Sites> &sites,
                   const LoopNest *parent = nullptr) const {
        for (auto c : children) {
            c->get_sites(sites, this);
        }
        if (parent && node != parent->node) {
            auto &s = sites.get_or_create(node);
            s.compute = parent;
            s.produce = this;
        }
        for (auto f : store_at) {
            sites.get_or_create(f).store = this;
        }
        if (innermost) {
            sites.get_or_create(node).innermost = this;
        }
    }

    void compute_features(const MachineParams &params,
                          const NodeMap<Sites> &sites,
                          int64_t instances,
                          int64_t parallelism,
                          const LoopNest *parent,
                          const LoopNest &root,
                          int64_t *working_set,
                          StageMap<ScheduleFeatures> *features) const {

        int64_t working_set_here = 0;

        int64_t loop_instances = 1, parallel_loop_instances = 1;
        size_t idx = 0;
        bool in_impure = false;
        for (auto i : size) {
            loop_instances *= i;
            if (stage->loop[idx++].pure && !in_impure) {
                parallel_loop_instances *= i;
            } else {
                in_impure = true;
            }
        }
        int64_t subinstances = instances * loop_instances;

        for (const auto *node : store_at) {
            // Figure out the features at the store_at level
            const auto &bounds = get_bounds(node);

            for (size_t s = 0; s < node->stages.size(); s++) {
                // TODO: Lift invariants from this loop. Most of it's the same for every stage.
                ScheduleFeatures &feat = features->get_or_create(&(node->stages[s]));

                feat.num_realizations = subinstances;

                feat.points_computed_per_realization = 1;
                for (int i = 0; i < (int)node->stages[s].loop.size(); i++) {
                    const auto &p = bounds->loops(s, i);
                    feat.points_computed_per_realization *= (p.second - p.first + 1);
                }
                feat.points_computed_total = feat.points_computed_per_realization * feat.num_realizations;

                feat.bytes_at_realization = node->bytes_per_point;
                for (int i = 0; i < node->func.dimensions(); i++) {
                    const auto &p = bounds->region_computed(i);
                    feat.bytes_at_realization *= (p.second - p.first) + 1;
                }
                int64_t innermost_storage_extent = 1;
                int v = sites.get(node).produce->vector_dim;
                if (v >= 0) {
                    innermost_storage_extent = (bounds->region_computed(v).second -
                                                bounds->region_computed(v).first + 1);
                }
                feat.innermost_bytes_at_realization = node->bytes_per_point * innermost_storage_extent;

                int64_t bytes_read_per_point = 0;
                for (const auto *e : node->incoming_edges) {
                    bytes_read_per_point += e->calls * e->producer->bytes_per_point;
                }
                feat.non_unique_bytes_read_per_realization = bytes_read_per_point * feat.points_computed_per_realization;
            }
        }

        if (is_root()) {
            for (auto c : children) {
                c->compute_features(params, sites, subinstances, parallelism, this, root, &working_set_here, features);
            }

            // Figure out the root-level features for every Func
            for (auto it = features->begin(); it != features->end(); it++) {
                const auto *stage = it.key();
                const auto *node = stage->node;
                auto &feat = it.value();
                const auto &root_bounds = root.get_bounds(node);

                feat.bytes_at_root = node->bytes_per_point;
                for (int i = 0; i < node->func.dimensions(); i++) {
                    const auto &p = root_bounds->region_computed(i);
                    feat.bytes_at_root *= (p.second - p.first) + 1;
                }

                // What innermost storage extent means for inlined
                // Funcs is unclear, because we haven't selected which
                // storage dimension is innermost.
                auto *p = sites.get(node).produce;
                if (p) {
                    int64_t innermost_storage_extent = 1;
                    int v = p->vector_dim;
                    if (v >= 0) {
                        innermost_storage_extent = root_bounds->region_computed(v).second - root_bounds->region_computed(v).first + 1;
                    }
                    feat.innermost_bytes_at_root = node->bytes_per_point * innermost_storage_extent;
                } else {
                    feat.innermost_bytes_at_root = 0;
                }

                feat.points_computed_minimum = 1;
                int s = stage - &node->stages[0];
                for (int i = 0; i < (int)stage->loop.size(); i++) {
                    const auto &p = root_bounds->loops(s, i);
                    feat.points_computed_minimum *= (p.second - p.first + 1);
                }

                if (node->stages.size() == 1 && !node->is_output) {
                    int64_t points_computed_minimum_if_inlined = 0;
                    for (auto *e : node->outgoing_edges) {
                        points_computed_minimum_if_inlined += features->get(&(e->consumer->stages[e->consumer_stage])).points_computed_minimum * e->calls;
                    }
                    feat.points_computed_minimum = std::min(feat.points_computed_minimum, points_computed_minimum_if_inlined);
                }
            }

            return;
        }

        int64_t parallel_tasks = parent->is_root() ? parallel_loop_instances : 1;
        int64_t subparallelism = parallel_tasks * parallelism;

        // Figure out the features at the compute_at level
        ScheduleFeatures &feat = features->get_or_create(stage);

        if (innermost) {
            // Figure out the features at the innermost loop cluster level
            feat.innermost_loop_extent = size.empty() ? 1 : size[0];

            if (vectorized_loop_index >= 0) {
                feat.innermost_pure_loop_extent = size[vectorized_loop_index];
            } else {
                feat.innermost_pure_loop_extent = 1;
            }
        }

        const bool at_production = parent->node != node;
        const bool at_pure_production = at_production && stage_idx == 0;

        if (at_production) {
            feat.num_productions = instances;
            feat.inner_parallelism = parallel_tasks;
            feat.outer_parallelism = parallelism;
            feat.vector_size = stage->vector_size;
            feat.native_vector_size = stage->vector_size;

            const auto &bounds = parent->get_bounds(node);

            feat.bytes_at_production = node->bytes_per_point;
            for (int i = 0; i < node->func.dimensions(); i++) {
                const auto &p = bounds->region_computed(i);
                feat.bytes_at_production *= (p.second - p.first) + 1;
            }
            int64_t innermost_storage_extent = 1;
            if (vector_dim >= 0) {
                innermost_storage_extent = bounds->region_computed(vector_dim).second - bounds->region_computed(vector_dim).first + 1;
            }
            feat.innermost_bytes_at_production = node->bytes_per_point * innermost_storage_extent;
        }

        for (auto c : children) {
            c->compute_features(params, sites, subinstances, subparallelism, this, root, &working_set_here, features);
        }

        if (at_production) {
            for (const auto *node : store_at) {
                working_set_here += features->get(&(node->stages[0])).bytes_at_production;
            }
            feat.working_set = working_set_here;
            feat.rounded_innermost_pure_loop_extent = ((feat.innermost_pure_loop_extent + feat.vector_size - 1) / feat.vector_size) * feat.vector_size;
        }

        *working_set += working_set_here;

        int64_t bytes_loaded = 0, lines_loaded = 0, allocation_bytes_loaded = 0;
        if (innermost || at_production) {
            // Pick the site at which we will compute the footprint relationship
            const auto *consumer_store_site = innermost ? parent : sites.get(node).store;
            int consumer_instances = innermost ? instances : feat.num_realizations;

            vector<const FunctionDAG::Node *> pending;
            pending.push_back(node);
            while (!pending.empty()) {
                const auto &next = pending.back()->incoming_edges;
                pending.pop_back();
                for (const auto *e : next) {
                    if (!sites.contains(e->producer)) {
                        // Producer was inlined, recursively examine its inputs
                        pending.push_back(e->producer);
                        continue;
                    }

                    const auto &site = sites.get(e->producer);
                    const auto *producer_compute_site = site.compute;
                    const auto *producer_store_site = site.store;
                    const auto &bounds = consumer_store_site->get_bounds(e->producer);
                    const auto &producer_compute_bounds = producer_compute_site->get_bounds(e->producer);
                    const auto &producer_store_bounds = producer_store_site->get_bounds(e->producer);
                    int64_t footprint = e->producer->bytes_per_point;
                    int64_t compute_footprint = footprint;
                    int64_t store_footprint = footprint;
                    int64_t line_footprint = 1;
                    int64_t compute_line_footprint = 1;
                    int64_t store_line_footprint = 1;
                    bool discontinuous = false;

                    for (int i = 0; i < e->producer->func.dimensions(); i++) {
                        auto p = bounds->region_required(i);
                        auto compute_p = producer_compute_bounds->region_computed(i);
                        auto store_p = producer_store_bounds->region_required(i);
                        int64_t extent = p.second - p.first + 1;
                        int64_t compute_extent = compute_p.second - compute_p.first + 1;
                        int64_t store_extent = store_p.second - store_p.first + 1;
                        internal_assert(!mul_would_overflow(64, footprint, extent))
                            << footprint << " " << extent << "\n";
                        footprint *= extent;
                        internal_assert(!mul_would_overflow(64, compute_footprint, compute_extent))
                            << compute_footprint << " " << compute_extent << "\n";
                        compute_footprint *= compute_extent;
                        internal_assert(!mul_would_overflow(64, store_footprint, store_extent))
                            << store_footprint << " " << store_extent << "\n";
                        store_footprint *= store_extent;
                        if (discontinuous) {
                            line_footprint *= extent;
                            compute_line_footprint *= compute_extent;
                            store_line_footprint *= store_extent;
                        }
                        // Allocated extent might be smaller than extent if
                        // the producer was fused into this consumer. If
                        // it's larger, we may be reading from something
                        // that was computed at a much coarser
                        // granularity.
                        // discontinuous |= (store_extent > extent);
                        discontinuous = true;
                    }


                    int64_t store_instances_per_consumption = 1;
                    const auto &producer_feat = features->get_or_create(&(e->producer->stages[0]));

                    if (producer_feat.num_realizations) {
                        // The producer's realization is nested inside this Func's realization
                        const int64_t producer_store_instances = producer_feat.num_realizations;
                        if (producer_store_instances > consumer_instances) {
                            store_instances_per_consumption = producer_store_instances / consumer_instances;
                        }
                    }

                    allocation_bytes_loaded += compute_footprint;

                    if (store_instances_per_consumption > 1) {
                        // The producer is nested inside the consumer
                        bytes_loaded += store_footprint * store_instances_per_consumption;
                        // Due to folding, the actual buffer size is smaller than the bounds at the store level
                        lines_loaded += store_line_footprint * store_instances_per_consumption;
                    } else {
                        // The consumer is consuming some portion of a larger producer computed earlier
                        bytes_loaded += footprint;
                        lines_loaded += line_footprint;
                    }
                }
            }
        }

        // TODO: consider input images in these bytes-read metrics.
        if (innermost) {
            feat.bytes_read_per_tile = bytes_loaded;
        }

        if (at_production) {
            // Properties of the realization, but the values are
            // computable at the production site because that's where
            // the consumers are.
            internal_assert(bytes_loaded >= 0) << "Negative bytes loaded: " << bytes_loaded << "\n";
            feat.unique_bytes_read_per_realization = bytes_loaded;
            feat.allocation_bytes_read_per_realization = allocation_bytes_loaded;
            feat.unique_lines_read_per_realization = lines_loaded;

            if (!at_pure_production) {
                // Also pessimistically assume this update definition relies on the entirety of the produced region so far.
                // TODO: This overbills scatters, or writes to a restriction region.
                internal_assert(bytes_loaded >= 0) << "Negative bytes at production: " << feat.bytes_at_production << "\n";
                feat.unique_bytes_read_per_realization += feat.bytes_at_production;
                feat.unique_lines_read_per_realization++; // It's accessed contiguously (TODO: This is fishy. Should probably be lines_at_production)
                feat.allocation_bytes_read_per_realization += feat.bytes_at_production;
            }
        }

        if (at_pure_production) {
            feat.points_computed_per_production = feat.points_computed_total / instances;
        }

        // Track features for inlined Funcs
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            const auto *f = it.key();
            internal_assert(f);
            auto &inlined_feat = features->get_or_create(&(f->stages[0]));
            inlined_feat.inlined_calls += it.value() * subinstances;
            inlined_feat.native_vector_size = (int64_t)(stage->vector_size);
            if (inlined_feat.vector_size > 0) {
                inlined_feat.vector_size = std::min(inlined_feat.vector_size, (int64_t)stage->vector_size);
            } else {
                inlined_feat.vector_size = feat.vector_size;
            }
            if (inlined_feat.innermost_pure_loop_extent > 0) {
                inlined_feat.innermost_pure_loop_extent = std::min(inlined_feat.innermost_pure_loop_extent,
                                                                   feat.innermost_pure_loop_extent);
            } else {
                inlined_feat.innermost_pure_loop_extent = feat.innermost_pure_loop_extent;
            }
            inlined_feat.rounded_innermost_pure_loop_extent =
                ((inlined_feat.innermost_pure_loop_extent + inlined_feat.vector_size - 1) /
                 inlined_feat.vector_size) * inlined_feat.vector_size;
            inlined_feat.inner_parallelism = 1;
            inlined_feat.outer_parallelism = parallelism;
        }
    }

    bool is_root() const {
        return node == nullptr;
    }

    const IntrusivePtr<const BoundContents> &set_bounds(const FunctionDAG::Node *f, BoundContents *b) const {
        return bounds.emplace(f, b);
    }

    const IntrusivePtr<const BoundContents> &get_bounds(const FunctionDAG::Node *f) const {
        // debug(0) << "get_bounds of " << f.name() << " in loop over " << (is_root() ? "root" : func.name()) << '\n';
        if (bounds.contains(f)) {
            const IntrusivePtr<const BoundContents> &b = bounds.get(f);
            // debug(0) << "Getting bounds of " << f->func.name() << " at site:\n";
            // dump("  ");
            b->validate();
            return b;
        }
        auto bound = f->make_bound();
        // Compute the region required
        if (f->is_output && is_root()) {
            internal_assert(f->outgoing_edges.empty()) << "Outputs that access other outputs not yet supported\n";
            // It's an output.
            // Use the bounds estimate
            for (int i = 0; i < f->func.dimensions(); i++) {
                bound->region_required(i) = f->estimated_region_required[i];
            }
        } else {
            internal_assert(!f->outgoing_edges.empty())
                << "No consumers of " << f->func.name()
                << " at loop over " << (is_root() ? "root" : node->func.name()) << '\n';
            std::pair<int64_t, int64_t> init {INT64_MAX, INT64_MIN};
            for (int i = 0; i < f->func.dimensions(); i++) {
                bound->region_required(i) = init;
            }
            for (const auto *e : f->outgoing_edges) {
                // Ignore consumers outside of this loop nest
                if (!computes(e->consumer)) {
                    continue;
                }
                const auto &c_bounds = get_bounds(e->consumer);
                // debug(0) << "Expanding footprint along edge " << e->producer->func.name() << " -> " << e->consumer->func.name() << "\n";
                const auto *consumer_loop = &(c_bounds->loops(e->consumer_stage, 0)); // For the concrete sizes of the loop
                e->expand_footprint(consumer_loop, &(bound->region_required(0)));
            }
        }

        f->required_to_computed(&(bound->region_required(0)), &(bound->region_computed(0)));

        for (int i = 0; i < (int)f->stages.size(); i++) {
            f->loop_nest_for_region(i, &(bound->region_computed(0)), &(bound->loops(i, 0)));
        }

        const IntrusivePtr<const BoundContents> &b = set_bounds(f, bound);
        b->validate();
        return b;
    }

    void dump(string prefix) const {
        if (!is_root()) {
            debug(0) << prefix << node->func.name();
            prefix += " ";
        }
        for (auto s : size) {
            debug(0) << " " << s;
        }
        if (tileable) {
            debug(0) << " t";
        }
        if (innermost) {
            debug(0) << " *\n";
        } else {
            debug(0) << '\n';
        }
        for (auto p : store_at) {
            debug(0) << prefix << "realize: " << p->func.name() << '\n';
        }
        for (size_t i = children.size(); i > 0; i--) {
            children[i-1]->dump(prefix);
        }
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            debug(0) << prefix << "inlined: " << it.key()->func.name() << " " << it.value() << '\n';
        }
        /*
          for (auto p : bounds) {
          debug(0) << prefix << "bounds: " << p.first.name();
          for (auto d : p.second.region) {
          debug(0) << " [" << d.first << ", " << d.second << "]";
          }
          debug(0) << '\n';
          }
        */
    }

    bool calls(const FunctionDAG::Node *f) const {
        for (const auto &c : children) {
            if (c->calls(f)) return true;
        }
        for (const auto *e : f->outgoing_edges) {
            if (e->consumer == node && e->consumer_stage == stage_idx) {
                return true;
            }
            if (inlined.contains(e->consumer)) {
                return true;
            }
        }
        return false;
    }

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

    bool accesses_input_buffer() const {
        for (const auto &c : children) {
            if (c->accesses_input_buffer()) return true;
        }
        if (is_root()) return false;

        auto check = [&](const FunctionDAG::Node *n) {
            for (const auto &s : n->stages) {
                for (int t = 0; t < (int)PipelineFeatures::ScalarType::NumScalarTypes; t++) {
                    if (s.features.op_histogram[(int)PipelineFeatures::OpType::ImageCall][t] > 0) return true;
                }
            }
            return false;
        };

        if (check(node)) return true;
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            if (check(it.key())) return true;
        }
        return false;
    }

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

#if 0
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
                if (inlined.contains(e->consumer)) {
                    calls += inlined.get(e->consumer) * e->calls;
                }
                if (e->consumer == node) {
                    calls += e->calls;
                }
            }
            if (calls) {
                inlined.insert(f, calls);
            }
        }
    }

    void compute_here(const FunctionDAG::Node *f, bool tileable, int vector_dim) {
        const auto &bounds = get_bounds(f);
        for (int s = (int)f->stages.size() - 1; s >= 0; s--) {
            std::unique_ptr<LoopNest> node{new LoopNest};
            node->node = f;
            node->stage_idx = s;
            node->stage = &f->stages[s];
            node->innermost = true;
            // TODO: rvars are not tileable
            node->tileable = tileable;
            // Set up a bound for the inside of the
            // loop. computed/required is still the full region, but
            // the loop nest will be a single representative point.
            auto single_point = bounds->make_copy();
            size_t loop_dim = f->stages[s].loop.size();
            node->size.resize(loop_dim);
            for (size_t i = 0; i < loop_dim; i++) {
                const auto &l = bounds->loops(s, i);
                // Initialize the loop nest
                node->size[i] = l.second - l.first + 1;
                // Pick a representative loop iteration for the inner
                // loop. With the way tiling is done below, it needs
                // to be the first loop iteration.
                single_point->loops(s, i) = {l.first, l.first};

                if (f->stages[s].loop[i].var == f->func.args()[vector_dim]) {
                    node->vectorized_loop_index = (int)i;
                }
            }
            // Leave region required blank inside the computation of a Func
            node->set_bounds(f, std::move(single_point));
            node->vector_dim = vector_dim;
            children.emplace_back(node.release());
        }
    }
    #endif

#if 0
    struct StageScheduleState {
        double num_cores = 0; // How much parallelism do we need to exploit with this Func?
        int vector_dim = -1; // Which storage dimension is vectorized? We need to reorder it innermost.
        struct FuncVar {
            VarOrRVar orig;
            VarOrRVar var;
            string accessor;
            int64_t extent = 0;
            bool outermost = false, parallel = false, exists = false, pure = false;
            FuncVar() : orig(Var()), var(Var()) {}
        };
        vector<FuncVar> vars; // In order from innermost to outermost. Each group of d is one tiling.
        std::ostringstream schedule_source;
    };
#endif
#if 0
    void apply(LoopLevel here,
               map<const FunctionDAG::Node::Stage *, StageScheduleState> &state_map,
               double num_cores,
               int depth,
               const LoopNest *parent,
               const LoopNest *compute_site) const {
        if (is_root()) {
            for (auto &c : children) {
                Func(c->node->func).compute_root();
                c->apply(LoopLevel::root(), state_map, num_cores, 1, this, c.get());
                if (c->stage_idx == 0) {
                    auto &state = state_map[c->stage];
                    state.schedule_source << "\n    .compute_root()";
                    // TODO: Omitting logic for printing store_root() assumes everything store_root is also compute root
                }
            }
        } else {
            if (parent && parent->node != node) {
                compute_site = this;
            }

            auto it = state_map.find(stage);
            const auto &symbolic_loop = stage->loop;
            const auto &parent_bounds = parent->get_bounds(node);
            if (it == state_map.end()) {
                StageScheduleState state;
                state.num_cores = num_cores;
                state.vector_dim = vector_dim;
                for (size_t i = 0; i < symbolic_loop.size(); i++) {
                    StageScheduleState::FuncVar fv;
                    const auto &l = symbolic_loop[i];
                    fv.var = VarOrRVar(l.var, !l.pure);
                    fv.orig = fv.var;
                    fv.accessor = l.accessor;
                    const auto &p = parent_bounds->loops(stage_idx, i);
                    fv.extent = p.second - p.first + 1;
                    fv.outermost = true;
                    fv.parallel = parent->is_root() && l.pure;
                    fv.exists = true;
                    fv.pure = l.pure;
                    state.vars.push_back(fv);
                }
                state_map.emplace(stage, std::move(state));
            }
            auto &state = state_map[stage];

            // The getter for grabbing Func handles is reverse topological order
            Stage s = Func(node->func);
            if (stage_idx > 0) {
                s = Func(node->func).update(stage_idx - 1);
            }

            if (stage_idx == 0 && parent->node != node) {
                // Pick a memory type
                double bytes = node->bytes_per_point;
                for (int i = 0; i < node->func.dimensions(); i++) {
                    const auto &p = parent_bounds->region_computed(i);
                    bytes *= p.second - p.first + 1;
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
            } else if (stage_idx == 0) {
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
                    // Find the innermost var, and the innermost pure var
                    StageScheduleState::FuncVar *innermost_var = nullptr, *var_to_vectorize = nullptr;
                    internal_assert(state.vars.size() >= symbolic_loop.size());
                    int64_t product_of_pure_loops = 1;
                    for (size_t i = 0; i < symbolic_loop.size(); i++) {
                        if (!state.vars[i].exists) continue;
                        if (innermost_var == nullptr) {
                            innermost_var = &state.vars[i];
                        }
                        if (state.vars[i].pure) {
                            product_of_pure_loops *= state.vars[i].extent;
                        }
                    }
                    if (vectorized_loop_index >= 0) {
                        var_to_vectorize = &state.vars[vectorized_loop_index];
                    }
                    internal_assert(innermost_var);
                    here = LoopLevel(node->func, innermost_var->var);

                    // TODO: Do an aligned unroll of anything with mods/divs on the coordinates.

                    int vector_size = stage->vector_size;
                    bool vectorized = false;
                    if (var_to_vectorize && vector_size > 1) {
                        int split_factor = 1;
                        if (var_to_vectorize->extent >= vector_size) {
                            split_factor = vector_size;
                        } else if (var_to_vectorize->extent >= 16) {
                            split_factor = 16;
                        } else if (var_to_vectorize->extent >= 8) {
                            split_factor = 8;
                        } else if (var_to_vectorize->extent >= 4) {
                            split_factor = 4;
                        }
                        if (split_factor > 1) {
                            VarOrRVar vec(Var(var_to_vectorize->var.name() + "_vec"));
                            auto tail_strategy = pure_var_tail_strategy;
                            if (stage_idx != 0 && !var_to_vectorize->outermost) {
                                // Ugh, we'll be using vector predication
                                tail_strategy = TailStrategy::GuardWithIf;
                            }
                            s.split(var_to_vectorize->var, var_to_vectorize->var, vec, split_factor, tail_strategy)
                                .vectorize(vec);
                            state.schedule_source
                                << "\n    .split("
                                << var_to_vectorize->var.name() << ", "
                                << var_to_vectorize->var.name() << ", "
                                << vec.name() << ", "
                                << split_factor << ", "
                                << "TailStrategy::" << tail_strategy << ").vectorize("
                                << vec.name() << ")";
                            StageScheduleState::FuncVar v = *var_to_vectorize;
                            v.extent = split_factor;
                            v.var = vec;
                            v.accessor.clear();
                            v.parallel = false;
                            v.outermost = false;
                            var_to_vectorize->extent += split_factor - 1;
                            var_to_vectorize->extent /= split_factor;
                            state.vars.insert(state.vars.begin(), v);
                            product_of_pure_loops /= split_factor;
                            vectorized = true;
                        }
                    }

                    // Temporary hack until we can actually model
                    // which loops are constant size. The other part
                    // of this hack is that we changed the unrolling
                    // pass to not complain if things are not
                    // constant.
                    bool all_pure_loops_constant_size = true;

                    if (product_of_pure_loops <= 16 && all_pure_loops_constant_size) {
                        // There's a hope we can fit anything compute-at this level into registers if we fully unroll
                        // TODO: 16 should be the number of vector registers in the architecture

                        // Start at 1 to skip the vectorized var
                        size_t start = vectorized ? 1 : 0;
                        size_t end = symbolic_loop.size() + start;
                        std::stable_sort(state.vars.begin() + start, state.vars.begin() + end,
                                         [](const StageScheduleState::FuncVar &a, const StageScheduleState::FuncVar &b) {
                                             return a.pure && !b.pure;
                                         });

                        for (size_t i = start; i < end; i++) {
                            if (state.vars[i].pure && state.vars[i].exists) {
                                s.unroll(state.vars[i].var);
                                state.schedule_source << "\n    .unroll(" << state.vars[i].var.name() << ")";
                            }
                        }
                    }

                    // TODO: unroll anything small with an explicit bound

                } else {
                    // Do the implied splits
                    vector<StageScheduleState::FuncVar> new_inner;
                    for (size_t i = 0; i < symbolic_loop.size(); i++) {
                        StageScheduleState::FuncVar v;
                        StageScheduleState::FuncVar &parent = state.vars[i];
                        int64_t factor = (parent.extent + size[i] - 1) / size[i];
                        if (!parent.exists || parent.extent == 1 || factor == 1) {
                            v.exists = false;
                            v.extent = 1;
                        } else if (size[i] == 1) {
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
                            if (parent.var.is_rvar || (stage_idx != 0 && !parent.outermost)) {
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
                            parent.extent = size[i];
                            v.var = inner;
                            v.accessor.clear();
                            v.extent = factor;
                            v.parallel = false;
                            v.outermost = false;
                        }
                        new_inner.push_back(v);
                    }
                    bool found = false;
                    for (const auto &v : state.vars) {
                        if (!v.exists) continue;
                        here = LoopLevel(node->func, v.var);
                        found = true;
                        break;
                    }
                    internal_assert(found) << "Could not find appropriate compute_at location for children of " << node->func.name() << "\n";
                    state.vars.insert(state.vars.begin(), new_inner.begin(), new_inner.end());
                }
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
                if (c->node != node && c->stage_idx == 0) {
                    auto &state = state_map[c->stage];
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
                    auto &state = state_map[&(f->stages[0])];
                    state.schedule_source << "\n    .store" << loop_level;
                }
            }
        }
    }
#endif
};

}


template<>
RefCount &ref_count<LoopNest>(const LoopNest *t) {return t->ref_count;}

template<>
void destroy<LoopNest>(const LoopNest *t) {delete t;}


}

void compute_pipeline_featurization(const Pipeline &pipeline, const Target& tgt, const MachineParams &params, std::unordered_map<Stage, PipelineFeatures, StageHasher> *features) {
std::vector<Internal::Function> outputs;
for (Func f : pipeline.outputs()) {
    outputs.push_back(f.function());
}
Internal::FunctionDAG dag(outputs, params, tgt);

// Annotate the DAG with pipeline features
dag.featurize();

// Extract the pipeline features
for (const Internal::FunctionDAG::Node &node : dag.nodes) {
  for (const Internal::FunctionDAG::Node::Stage& stage : node.stages) {
    features->emplace(stage.stage, stage.features);
  }
}

/* Schedule features
Internal::IntrusivePtr<const Internal::LoopNest> root;
//root = ???;

Internal::StageMap<ScheduleFeatures> schedule_features;
Internal::NodeMap<Internal::LoopNest::Sites> sites;
sites.make_large(dag.nodes.size());
schedule_features.make_large(dag.nodes[0].stages[0].max_id);
internal_assert(root.defined());
root->get_sites(sites);

root->compute_features(params, sites, 1, 1, nullptr, *root, nullptr, &schedule_features);
*/
}


}
