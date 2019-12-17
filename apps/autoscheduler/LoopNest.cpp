#include "LoopNest.h"

using std::set;
using std::vector;

namespace Halide {
namespace Internal {
namespace Autoscheduler {

// How small should an innermost loop cluster be before you just
// entirely unroll the thing. Sized for an architecture with 16 vector
// registers.
const int kUnrollLimit = 12;
const int kUnrollLimitGPU = 16;

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

int64_t get_shared_memory_limit() {
    // HL_SHARED_MEMORY_LIMIT is in KB
    std::string limit = get_env_variable("HL_SHARED_MEMORY_LIMIT");
    return atoi(limit.c_str()) * 1024; // Convert to bytes
}

int64_t get_active_block_hardware_limit() {
    std::string limit = get_env_variable("HL_ACTIVE_BLOCK_LIMIT");
    if (limit.empty()) {
        return 32;
    }
    return atoi(limit.c_str());
}

int64_t get_active_warp_hardware_limit() {
    std::string limit = get_env_variable("HL_ACTIVE_WARP_LIMIT");
    if (limit.empty()) {
        return 64;
    }
    return atoi(limit.c_str());
}

int get_unroll_limit(const Target& target) {
    if (target.has_gpu_feature()) {
        return kUnrollLimitGPU;
    }

    return kUnrollLimit;
}

bool in_range_zero_one(double x) {
    return x > 0 && x <= 1;
}

bool are_valid_thread_extents(const vector<int64_t>& counts) {
    int num_thread_loops = 0;
    int num_threads = 1;

    for (auto c : counts) {
        if (c == 1) {
            continue;
        }

        if (num_thread_loops >= 3 || num_threads * c > MAX_THREADS_PER_BLOCK) {
            return false;
        }

        num_threads *= c;
        ++num_thread_loops;
    }

    return true;
}




// given a newly inserted node f into this LoopNest, get union of thread counts in each dimension
// across all siblings of f.
vector<int64_t> LoopNest::get_union_thread_counts(const FunctionDAG::Node *f) const {
    vector<int64_t> max_size{1,1,1};
    // find the loop nests we just created and get max gpu_thread extents of other children
    for (auto &c : children) {
        if (c->node != f) {
            if (c->gpu_label == thread) {
                vector<int64_t> lowered_size;
                lowered_dims(c->size, c->vectorized_loop_index, lowered_size);
                for (int dim = 0; dim < (int)(lowered_size.size()); dim++) {
                    if ( dim >= (int)(max_size.size()) ) {
                        max_size.push_back(lowered_size[dim]);
                    } else {
                        max_size[dim] = std::max(max_size[dim], lowered_size[dim]);
                    }
                }
            } else if (c->children.size() > 0) { // descend into children for thread blocks in serial loops
                vector<int64_t> child_max_sizes = c->get_union_thread_counts(f);
                for (int dim = 0; dim < (int)(child_max_sizes.size()); dim++) {
                    if (dim >= (int)(max_size.size())) {
                        max_size.push_back(child_max_sizes[dim]);
                    } else {
                        max_size[dim] = std::max(max_size[dim], child_max_sizes[dim]);
                    }
                }
            } // otherwise this a serial loop with no threaded descendants
        }
    }
    return max_size;
}

/** moves vectorized dimension first and also removes dimensions with size 1
    to reflect actual thread dimensions when loop nests are lowered **/
void lowered_dims(const vector<int64_t> &size, int vector_loop_i, vector<int64_t> &lowered_size) {
    if (vector_loop_i >= 0 && size[vector_loop_i] > 1) {
        lowered_size.push_back(size[vector_loop_i]);
    }
    for (int dim = 0; dim < (int)(size.size()); dim++) {
        if (dim != vector_loop_i && size[dim] > 1) {
            lowered_size.push_back(size[dim]);
        }
    }
}

// creates tilings for gpu threads loops.
// Innermost thread loop is always the vectorized dim and its extent is a multiple of 32.
// Other loop extents are sized to be powers of 2 such that total extent is < 1024
// called either when we are creating parallel -> (blocks, threads) loop when computing at root
// OR when we are creating none -> (threads, SIMD) loop when computing at a serial loop
// serial_inner = True when we're generating (thread, serial) tilings, False when generating (block,thread) tilings
// max_s hold max gpu_thread counts of all siblings in each dimension. Used to make sure union of
// thread counts is under 1024 threshold.
vector<vector<int64_t>> generate_gpu_tilings(const vector<vector<int64_t>> &stage_sizes,
        const vector<vector<int>> &pure_dims,
        const vector<int64_t> &max_s,
        int d, const vector<int> &vectorized_indices, bool serial_inner) {
    vector<vector<int64_t>> result;
    if (d == -1) {
        result.push_back(vector<int64_t>());
    } else {
        // set max thread count 64 for now in all dims
        int64_t max_threads_extent = 64, total_threads_limit = 1024; // less than 1024 to limit states
        int factor = 2, warp_width = 32, max_serial_ext = 8;

        vector<vector<int64_t>> v;
        v = generate_gpu_tilings(stage_sizes, pure_dims, max_s, d - 1, vectorized_indices, serial_inner);

        for (auto t : v) {
            enum validity{ serial_count_err, thread_count_err, valid_tiling };

            // helper function detects whether tiling is legal: cannot exceed max thread count,
            // have more than three dimensions with ext > 1, or result in large serial loops
            std::function<validity()> is_valid_tiling = [&]() {
                if (d == ((int)(stage_sizes[0].size()) - 1)) {
                    vector<int64_t> lowered_size, thread_t;
                    if (false) {
                        for (int dd = 0; dd < (int)(stage_sizes[0].size()); dd++) {
                            int64_t other_ext = (stage_sizes[0][dd] + t[dd] - 1) / t[dd];
                            thread_t.push_back(other_ext);
                        }
                    } else {
                        thread_t = t;
                    }
                    lowered_dims(thread_t, vectorized_indices[0], lowered_size);
                    // see how tiling will be applied to other stages of this func and update max_s accordingly
                    vector<int64_t> new_max_s = max_s;
                    for (size_t stage = 0; stage < pure_dims.size(); stage++) {
                        vector<int64_t> stage_thread_t, stage_lowered_size;
                        for (size_t i = 0; i < pure_dims[stage].size(); i++) {
                            if (pure_dims[stage][i] >= 0) {
                                stage_thread_t.push_back(thread_t[pure_dims[stage][i]]);
                            } else { // impure dims have extent 1
                                stage_thread_t.push_back(1);
                            }
                        }
                        lowered_dims(stage_thread_t, vectorized_indices[stage], stage_lowered_size);
                        // adjust max_size to account for other stages thread counts when we apply this tiling
                        for (size_t dim = 0; dim < stage_lowered_size.size(); dim++) {
                            if ( dim >= max_s.size() ) {
                                new_max_s.push_back(stage_lowered_size[dim]);
                            } else {
                                new_max_s[dim] = std::max(max_s[dim], stage_lowered_size[dim]);
                            }
                        }
                    }
                    int64_t union_threads;
                    int64_t total_threads_used = 1, not_ext1 = 0;
                    int max_dim = std::max((int)(new_max_s.size()), (int)(lowered_size.size()));
                    for (int dim = 0; dim < max_dim; dim++) {
                        if (dim >= (int)(new_max_s.size())) {
                            union_threads = lowered_size[dim];
                        } else if (dim >= (int)(lowered_size.size())) {
                            union_threads = new_max_s[dim];
                        } else {
                            union_threads = std::max(lowered_size[dim], new_max_s[dim]);
                        }
                        not_ext1 = not_ext1 + ( (union_threads > 1) ? 1 : 0 );
                        total_threads_used *= union_threads;
                    }
                    if (total_threads_used > total_threads_limit || not_ext1 > 3) {
                        return thread_count_err;
                    }
                    if (serial_inner) {
                        for (int dd = 0; dd < (int)(stage_sizes[0].size()); dd++) {
                            int64_t other_ext = (stage_sizes[0][dd] + t[dd] - 1) / t[dd];
                            if (other_ext > max_serial_ext) {
                                return serial_count_err;
                            }
                        }
                    }
                }
                return valid_tiling;
            };

            t.push_back(0);

            // if the vector dimension has extent < warp_width we use 1 warp for it
            int64_t min_threads = ( (d == vectorized_indices[0]) ? std::min(warp_width, (int)stage_sizes[0][d]) : 1 );
            for (int64_t threads_ext = min_threads; threads_ext <= stage_sizes[0][d]; threads_ext *= factor) {
                // reject if inner exceeds hardware thread limit
                if (threads_ext > max_threads_extent) {
                    break;
                }
                if (false) {
                    int64_t other_ext = (stage_sizes[0][d] + threads_ext - 1) / threads_ext;
                    t.back() = other_ext;
                } else {
                    t.back() = threads_ext;
                }
                validity valid_result = is_valid_tiling();
                if (valid_result == serial_count_err) {
                    continue;
                } else if (valid_result == thread_count_err) {
                    break;
                } else {
                   result.push_back(t);
                }
            }

            // The sequence above (in terms of the inner loop) goes
            // (32 64 128 256 512 ... ) x (1 2 4 8 16 ... )
            // but 16 may be an important threads tiling factor
            int64_t threads16 = 16;
            int64_t other16 = (stage_sizes[0][d] + threads16 - 1) / threads16;
            if ((d == vectorized_indices[0]) && threads16 < stage_sizes[0][d] && other16 > 1) {
                if (false)
                    t.back() = other16;
                else
                    t.back() = threads16;
                validity valid_result = is_valid_tiling();
                if (valid_result == valid_tiling ) {
                   result.push_back(t);
                }
            }
        }
    }
    return result;
}

// used for creating default serial loop tiling options inside gpu threads loop
vector<vector<int64_t>> generate_serial_tilings(const vector<int64_t> &s, int d,
                                                int vectorized_index,
                                                const vector<int> &vec_dim_serial_sizes) {
    vector<vector<int64_t>> result;
    if (d == -1) {
        result.push_back(vector<int64_t>());
    } else {
        vector<vector<int64_t>> v;
        v = generate_serial_tilings(s, d - 1, vectorized_index, vec_dim_serial_sizes);
        for (auto t : v) {
            t.push_back(0);
            // include odd serial sizes that encourage multiples of 16 as thread tile size
            if (vec_dim_serial_sizes.size() > 0 && d == vectorized_index) {
                for (int inner : vec_dim_serial_sizes) {
                    int outer = (s[d] + inner - 1) / inner;
                    t.back() = outer;
                    result.push_back(t);
                }
            }
            // always consider the even tile sizes: 1, 2, 4, 8
            for (int inner = 1; inner <= 8; inner *= 2) {
                if (inner > s[d]) {
                    break;
                }
                int outer = (s[d] + inner - 1) / inner;
                t.back() = outer;
                result.push_back(t);
            }
        }
    }
    return result;
}


// Given a multi-dimensional box of dimensionality d, generate a list
// of candidate tile sizes for it, logarithmically spacing the sizes
// using the given factor. If 'allow_splits' is false, every dimension
// must either be one, or the full extent of the box. This function is
// used to generate candidate tilings when tiling for
// producer-consumer fusion, or tiling for parallelism.
// inner_sizes is optional vector of fixed sizes to choose from for inner loop.
// used for GPU schedules when we split a 'none' loop into a parallel loop and a serial loop
vector<vector<int64_t>> generate_tilings(const vector<int64_t> &s, int d, int factor,
                                         bool allow_splits, const Target& target,
                                         const vector<int> &inner_sizes) {
    vector<vector<int64_t>> result;
    if (d == -1) {
        result.push_back(vector<int64_t>());
    } else {
        vector<vector<int64_t>> v;
        v = generate_tilings(s, d - 1, factor, allow_splits, target);
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
                if (!inner_sizes.empty()) { // using fixed set of inner loop extents
                    for (int inner : inner_sizes) {
                        int outer = (s[d] + inner - 1) / inner;
                        if (is_one && outer == 1) continue;
                        if (is_full && outer == s[d]) continue;
                        t.back() = outer;
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
    }
    return result;
}

// given a newly inserted node f into this LoopNest, gets the size of
// all of f's stages and their pure_dim indices
void LoopNest::get_stage_sizes(const FunctionDAG::Node *f,
    vector<vector<int64_t>> &stage_sizes,
    vector<vector<int>> &pure_dims,
    vector<int> &vectorized_indices) {
    stage_sizes.resize(f->stages.size());
    pure_dims.resize(f->stages.size());
    vectorized_indices.resize(f->stages.size());
    for (auto &c : children) {
        if (c->node == f && f->dimensions > 0) {
            vectorized_indices[c->stage->index] = c->vectorized_loop_index;
            stage_sizes[c->stage->index] = c->size;
            for (size_t i = 0; i < c->stage->loop.size(); i++) {
                pure_dims[c->stage->index].push_back(c->stage->loop[i].pure_dim);
            }
        }
    }
}

// given the loop nest of a stage to parallelize at root, figure out if using odd tile sizes
// for the vectorized dimension will allow the resulting thread tiles to be multiples of 32
// if so, we will include these in the serial loop sizes
void LoopNest::generate_vec_dim_serial_tilings(vector<int> &serial_sizes) const {
    // generate suggested tilings for vectorized dimension
    int warp_width = 32;
    if (size[vectorized_loop_index] % warp_width == 0) {
        int remaining_ext = size[vectorized_loop_index] / warp_width;
        for (int s = 3; s < 8; s += 2) {
            if (remaining_ext % s == 0) {
                serial_sizes.push_back(s);
            }
        }
    }
}

// get the loop nests of a newly inserted node, f, that is marked GPU threads. Tiles
// the newly inserted loop nests of f into a threads loop outside a serial loop.
// V is the vectorized dimension of f. Adds loopnests created from each tiling option in result.
bool LoopNest::add_gpu_thread_tilings(const FunctionDAG::Node *f,
                            const MachineParams &params,
                            const Target &target,
                            int v,
                            vector<IntrusivePtr<const LoopNest>> &result,
                            vector<int64_t> max_size) {
    vector<vector<int64_t>> stage_sizes;
    vector<vector<int>> pure_dims;
    vector<int> vectorized_indices;
    this->get_stage_sizes(f, stage_sizes, pure_dims, vectorized_indices);
    internal_assert(stage_sizes.size() != 0);
    //internal_assert(pure_size);
    auto tilings = generate_gpu_tilings(stage_sizes, pure_dims, max_size, (int)(stage_sizes[0].size() - 1), vectorized_indices, true);
    bool made_child = false;
    for (const auto &t : tilings) {
        LoopNest *new_parent = new LoopNest;
        new_parent->copy_from(*(this));
        for (auto &c : new_parent->children) {
            if (c->node == f) {
                c = c->parallelize_in_tiles(params, t, new_parent, target, false, true);
            }
        }
        result.emplace_back(new_parent);
        made_child = true;
    }
    if (!made_child) { // if we can't tile into gpu threads the inserted node, make it serial
        for (auto &c : children) {
            if (c->node == f)
                c->gpu_label = serial;
        }
    }
    return made_child;
}

void LoopNest::copy_from(const LoopNest &n) {
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
    gpu_label = n.gpu_label;
};

// Hash the loop structure and sizes up to a fixed depth. This is
// used as the hash function for the coarse-to-fine beam search in
// the paper.
void LoopNest::structural_hash(uint64_t &h, int depth) const {
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

// Compute all the sites of interest for each pipeline stage
void LoopNest::get_sites(StageMap<Sites> &sites,
               const LoopNest *task,
               const LoopNest *parent) const {
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

bool LoopNest::exceeds_serial_extents_limit(bool in_threads_loop) const {
    if (gpu_label == serial && in_threads_loop) {
        int64_t serial_loop_extents = 1;
        for (const auto s : size) {
            serial_loop_extents *= s;
        }

        return serial_loop_extents > 16;
    }

    for (const auto& c : children) {
        if (c->exceeds_serial_extents_limit(in_threads_loop || c->gpu_label == thread)) {
            return true;
        }
    }

    return false;
}

bool LoopNest::node_has_dynamic_region_computed(const FunctionDAG::Node* f) const {
    for (int i = 0; i < f->dimensions; i++) {
        const auto& region = get_bounds(f)->region_computed(i);

        if (!region.constant_extent()) {
            return true;
        }
    }

    return false;
}

bool LoopNest::has_dynamic_allocation_inside_thread(bool in_thread_loop) const {
    in_thread_loop = in_thread_loop || (gpu_label == thread);

    if (in_thread_loop) {
        for (const auto& f : store_at) {
            if (node_has_dynamic_region_computed(f)) {
                return true;
            }
        }
    }

    for (const auto& child : children) {
        if (child->has_dynamic_allocation_inside_thread(in_thread_loop)) {
            return true;
        }
    }

    return false;
}

const LoopNest* LoopNest::find_pure_stage_loop_nest(const FunctionDAG::Node* node) const {
    const LoopNest* pure;
    for (const auto &c : children) {
        if (node == c->node) {
            if (c->stage->index == 0) {
                return c.get();
            }
        } else {
            pure = c->find_pure_stage_loop_nest(node);
            if (pure) {
                return pure;
            }
        }
    }

    return nullptr;
}

int LoopNest::get_pure_stage_vectorized_loop_index(const FunctionDAG::Node* node) const {
    const auto* pure = find_pure_stage_loop_nest(node);
    internal_assert(pure) << "No pure stage found for " << node->func.name() << "\n";
    return pure->vectorized_loop_index;
}

int LoopNest::get_vectorized_loop_index_from_pure_stage(const LoopNest& root) const {
    int v = vectorized_loop_index;
    if (v < 0) {
        v = root.get_pure_stage_vectorized_loop_index(node);
    }

    // For update stages, it's possible that the pure stage's vectorized
    // loop index is larger than the dimensions of the update stage e.g.
    // the pure stage's vectorized loop index is 3, but the update stage
    // has 3 or fewer dimensions. In this case, the vectorized loop
    // index should just be its innermost dimension i.e. 0
    if ((size_t)v >= stage->loop.size()) {
        v = 0;
    }

    return v;
}

// Get the stride over "node's" storage for a unit increment in the vectorized loop's
// index
double LoopNest::storage_stride(const LoadJacobian& jac, int innermost_storage_dim, const FunctionDAG::Node* storage_node, const Bound& store_bounds, const LoopNest& root) const {
    internal_assert(innermost_storage_dim >= 0);

    // The node's storage dimensions (from innermost outward)
    std::vector<int64_t> storage_dims;
    storage_dims.push_back(innermost_storage_dim);
    for (int i = 0; i < storage_node->dimensions; i++) {
        if (i == storage_dims[0]) {
            continue;
        }

        storage_dims.push_back(i);
    }

    std::vector<int64_t> storage_strides;
    int64_t storage_stride = 1;
    for (std::size_t i = 0; i < storage_dims.size(); i++) {
        storage_strides.push_back(storage_stride);
        storage_stride *= store_bounds->region_required(storage_dims[i]).extent();
    }

    int v = get_vectorized_loop_index_from_pure_stage(root);

    double stride = 0;
    for (std::size_t i = 0; i < storage_dims.size(); i++) {
        auto jac_stride = jac(i, v);

        float s = (float)jac_stride.numerator / (float)jac_stride.denominator;
        stride += s * storage_strides[i];
    }

    return stride;
}

bool LoopNest::all_strides_exist(const LoadJacobian& jac, const FunctionDAG::Node* storage_node, const LoopNest& root) const {
    int v = get_vectorized_loop_index_from_pure_stage(root);

    for (int i = 0; i < storage_node->dimensions; i++) {
        auto stride = jac(i, v);

        if (!stride.exists) {
            return false;
        }
    }
    return true;
}

std::pair<int, double> LoopNest::num_shared_mem_accesses(const FunctionDAG::Node* node, const ThreadInfo& thread_info, double serial_loop_extents, double stride) const {
    // No bank conflicts when stride is 0
    if (stride == 0) {
        // No caching for shared mem so each warp needs to load the value
        int num_accesses = thread_info.num_active_warps_per_block * serial_loop_extents;
        return {num_accesses, stride};
    }

    int num_bank_accesses[32] = {0};
    int largest_index[32] = {-1};

    stride = std::abs(stride);

    double bytes = node->bytes_per_point;

    // Each bank is 4 bytes so adjust the stride based
    // on width of data being loaded
    double bank_stride = (bytes / 4);
    int num_banks_per_access = std::max(1.0, bank_stride);
    stride *= bank_stride;

    int total_accesses = 0;

    thread_info.for_each_thread_id([&](int thread_id, bool is_active, bool is_last_thread) {
        if (is_active) {
            // Compute counts of which banks are accessed
            // Multiple accesses to the same bank with different
            // indices will be serialized
            for (int j = 0; j < num_banks_per_access; j++) {
                int index = (int)(thread_id * stride) + j;
                int bank = index % 32;
                if (largest_index[bank] != index) {
                    num_bank_accesses[bank]++;
                }
                largest_index[bank] = index;
            }
        }

        if ((thread_id + 1) % 32 == 0 || is_last_thread) {
            int max_accesses_this_warp = 0;
            for (int j = 0; j < 32; ++j) {
                max_accesses_this_warp = std::max(max_accesses_this_warp, num_bank_accesses[j]);
                num_bank_accesses[j] = 0;
                largest_index[j] = -1;
            }
            total_accesses += max_accesses_this_warp;
        }
    });

    int num_accesses = total_accesses * serial_loop_extents;
    return {num_accesses, stride};
}

int LoopNest::num_banks_per_access(const FunctionDAG::Node* node) const {
    double bytes = node->bytes_per_point;
    return std::max(1.0, bytes / 4);
}

int LoopNest::compute_min_accesses(const FunctionDAG::Node* node, const ThreadInfo& thread_info, double stride, double serial_loop_extents) const {
    // In the best case, each warp requires only a single access per serial
    // loop iteration
    double min_accesses = serial_loop_extents * thread_info.num_active_warps_per_block;

    if (stride == 0) {
        return min_accesses;
    }

    // If the stride is non-zero, then nodes that access multiple banks per
    // thread (i.e. if the node requires more than 4 bytes per point) will
    // require more accesses per warp
    return min_accesses * num_banks_per_access(node);
}

std::pair<double, double> LoopNest::compute_shared_mem_stores(const LoadJacobian& jac, int consumer_innermost_dim, const FunctionDAG::Node* node, const Bound& consumer_store_bounds, const ThreadInfo& thread_info, double serial_loop_extents, const LoopNest& root) const {
    if (all_strides_exist(jac, node, root)) {
        double stride = storage_stride(jac, consumer_innermost_dim, node, consumer_store_bounds, root);
        auto num_accesses = num_shared_mem_accesses(node, thread_info, serial_loop_extents, stride).first;
        double min_accesses = stride == 0 ? num_accesses : num_shared_mem_accesses(node, thread_info, serial_loop_extents, 1).first;
        return {num_accesses, min_accesses / num_accesses};
    }

    // Assume worst case serialized loads if the stride
    // is unknown
    double num_accesses = thread_info.num_threads * serial_loop_extents;
    auto min_accesses = num_shared_mem_accesses(node, thread_info, serial_loop_extents, 1).first;
    return {num_accesses, min_accesses / num_accesses};
}

std::pair<double, double> LoopNest::compute_shared_mem_load_features(const LoadJacobian& jac, int producer_innermost_dim, const FunctionDAG::Node* node, const Bound& producer_store_bounds, bool producer_has_been_scheduled, const ThreadInfo& thread_info, const LoopNest& root, double serial_loop_extents) const {
    // Assume worst case serialized loads if the stride
    // is unknown
    if (!all_strides_exist(jac, node, root)) {
        // Assume worst case serialized loads if the stride
        // is unknown
        double num_accesses = thread_info.num_threads * serial_loop_extents;
        auto min_accesses = num_shared_mem_accesses(node, thread_info, serial_loop_extents, 1).first;

        return {num_accesses, min_accesses};
    }

    if (producer_has_been_scheduled) {
        double stride = storage_stride(jac, producer_innermost_dim, node, producer_store_bounds, root);
        auto num_accesses = num_shared_mem_accesses(node, thread_info, serial_loop_extents, stride).first;

        double min_accesses = stride == 0 ? num_accesses : num_shared_mem_accesses(node, thread_info, serial_loop_extents, 1).first;
        return {num_accesses, min_accesses};
    }

    // Assume best case if producer has not been scheduled: try all the
    // possible innermost dimensions and take the best
    int num_accesses = thread_info.num_threads * serial_loop_extents;
    double min_stride = 32.0;
    for (int i = 0; i < node->dimensions; i++) {
        double stride = storage_stride(jac, i, node, producer_store_bounds, root);
        auto result = num_shared_mem_accesses(node, thread_info, serial_loop_extents, stride);
        if (result.first < num_accesses) {
            num_accesses = result.first;
            min_stride = result.second;
        }
    }

    double min_accesses = min_stride == 0 ? num_accesses : num_shared_mem_accesses(node, thread_info, serial_loop_extents, 1).first;
    return {num_accesses, min_accesses};
}

void LoopNest::compute_gpu_store_features(const LoadJacobian& jac, int consumer_innermost_dim, const FunctionDAG::Node* node, const Bound& consumer_store_bounds, const ThreadInfo& thread_info, double serial_loop_extents, const Sites& consumer_site, ScheduleFeatures& feat, const LoopNest& root) const {
    if (consumer_site.store->gpu_label == block) {
        auto shared_mem_features = compute_shared_mem_stores(
            jac,
            consumer_innermost_dim,
            node,
            consumer_store_bounds,
            thread_info,
            serial_loop_extents,
            root
        );

        feat.num_shared_mem_stores_per_block = shared_mem_features.first;
        feat.shared_mem_store_efficiency = shared_mem_features.second;

        internal_assert(in_range_zero_one(feat.shared_mem_store_efficiency)) << "Invalid shared mem store efficiency: " << feat.shared_mem_store_efficiency;
    } else if (consumer_site.store->is_root()) {
        auto global_mem_info = compute_global_mem_store_features(
            jac,
            consumer_innermost_dim,
            node,
            consumer_store_bounds,
            thread_info,
            serial_loop_extents,
            root
        );

        feat.num_global_mem_stores_per_block = global_mem_info.required_accesses();
        feat.global_mem_store_efficiency = global_mem_info.access_efficiency();
        feat.global_mem_store_coalesce_efficiency = global_mem_info.coalesce_efficiency();

        internal_assert(in_range_zero_one(feat.global_mem_store_efficiency)) << "Invalid global mem store efficiency: " << feat.global_mem_store_efficiency;
        internal_assert(in_range_zero_one(feat.global_mem_store_coalesce_efficiency)) << "Invalid global mem store coalesce efficiency: " << feat.global_mem_store_coalesce_efficiency;
    }
}

int LoopNest::word_stride(const FunctionDAG::Node* node) const {
    double bytes = node->bytes_per_point;
    return std::max(1.0, bytes / 4);
}

int LoopNest::num_words_per_access(const FunctionDAG::Node* node) const {
    double bytes = node->bytes_per_point;
    return std::max(1.0, bytes / 4);
}

double LoopNest::min_global_mem_accesses(const FunctionDAG::Node* node, const ThreadInfo& thread_info, double serial_loop_extents, double stride) const {
    if (stride == 0) {
        // Only need a single access (optimistically assume that it remains
        // cached across warps)
        return 1;
    }

    double bytes = node->bytes_per_point;

    // Each word is 4 bytes so adjust the stride based
    // on width of data being accessed
    double word_stride = (bytes / 4);
    int words_per_access = std::max(1.0, word_stride);
    stride *= words_per_access;

    int num_accesses = 0;
    int last_segment_accessed = -1;

    thread_info.for_each_active_thread_id([&](int thread_id, bool is_last_thread) {
        // Compute counts of which segments are accessed
        for (int j = 0; j < words_per_access; j++) {
            int64_t index = (int64_t)(thread_id * stride) + j;
            int segment = index / 8;
            if (segment != last_segment_accessed) {
                last_segment_accessed = segment;
                num_accesses++;
            }
        }
    });

    return serial_loop_extents * num_accesses;
}

void LoopNest::compute_num_global_mem_accesses_per_block(const LoadJacobian& jac, const FunctionDAG::Node* node, const Bound& store_bounds, const ThreadInfo& thread_info, int innermost_dim, double serial_loop_extents, GlobalMemInfo& global_mem_info, const LoopNest& root) const {
    double stride = storage_stride(jac, innermost_dim, node, store_bounds, root);

    if (stride == 0) {
        // Only need a single access (optimistically assume that it remains
        // cached across warps)
        global_mem_info.add_access_info(1, 1, stride);
        return;
    }

    double bytes = node->bytes_per_point;

    // Each word is 4 bytes so adjust the stride based
    // on width of data being accessed
    double word_stride = (bytes / 4);
    int words_per_access = std::max(1.0, word_stride);
    stride = std::abs(stride);
    stride *= words_per_access;

    // If the stride is larger than 8 words (32 bytes), it is guaranteed to
    // traverse at least one segment each iteration. Any stride larger than
    // 2 segments will just traverse empty segments so we reduce it here to
    // avoid potential overflow below
    if (stride > 8.0) {
        stride = 8.0 + std::fmod(stride, 8.0);
    }

    double min_stride = words_per_access;

    // If the stride is less than min_stride, the minimum number of accesses
    // should be equal to the actual number of accesses
    bool min_equals_actual = stride < min_stride;

    double strides[2] = {stride, min_stride};
    int num_accesses[2] = {0, 0};
    int last_segment_accessed[2] = {-1, -1};

    thread_info.for_each_active_thread_id([&](int thread_id, bool is_last_thread) {
        for (int s = 0; s < 2; ++s) {
            if (s == 1 && min_equals_actual) {
                break;
            }

            // Compute counts of which segments are accessed
            for (int j = 0; j < words_per_access; j++) {
                int64_t index = (int64_t)(thread_id * strides[s]) + j;
                int segment = index / 8;
                if (segment != last_segment_accessed[s]) {
                    last_segment_accessed[s] = segment;
                    num_accesses[s]++;
                }
            }
        }
    });

    if (min_equals_actual) {
        num_accesses[1] = num_accesses[0];
    }

    global_mem_info.add_access_info(serial_loop_extents * num_accesses[0], serial_loop_extents * num_accesses[1], stride);
}

GlobalMemInfo LoopNest::compute_global_mem_store_features(const LoadJacobian& jac, int consumer_innermost_dim, const FunctionDAG::Node* node, const Bound& consumer_store_bounds, const ThreadInfo& thread_info, double serial_loop_extents, const LoopNest& root) const {
    GlobalMemInfo global_mem_info;

    if (!all_strides_exist(jac, node, root)) {
        double stride = 32.0;

        // Assume worst case serialized loads if the stride
        // is unknown
        auto required_accesses = serial_loop_extents * thread_info.num_threads;

        auto min_accesses = min_global_mem_accesses(node, thread_info, serial_loop_extents, stride);

        global_mem_info.add_access_info(required_accesses, min_accesses, stride);
        return global_mem_info;
    }

    compute_num_global_mem_accesses_per_block(jac, node, consumer_store_bounds, thread_info, consumer_innermost_dim, serial_loop_extents, global_mem_info, root);
    return global_mem_info;
}

void LoopNest::compute_global_mem_load_features(const LoadJacobian& jac, int producer_innermost_dim, const FunctionDAG::Node* node, const Bound& producer_store_bounds, bool producer_has_been_scheduled, const ThreadInfo& thread_info, GlobalMemInfo& global_mem_info, double serial_loop_extents_and_load_count, const LoopNest& root) const {
    // Assume worst case serialized loads if the stride
    // is unknown
    if (!all_strides_exist(jac, node, root)) {
        double stride = 32.0;

        auto required_accesses = serial_loop_extents_and_load_count * thread_info.num_threads;

        auto min_accesses = min_global_mem_accesses(node, thread_info, serial_loop_extents_and_load_count, stride);

        global_mem_info.add_access_info(required_accesses, min_accesses, stride);
        return;
    }

    if (producer_has_been_scheduled) {
        compute_num_global_mem_accesses_per_block(jac, node, producer_store_bounds, thread_info, producer_innermost_dim, serial_loop_extents_and_load_count, global_mem_info, root);

        return;
    }

    // Assume best case if producer has not been scheduled: try all the
    // possible innermost dimensions and take the best
    int min_required_accesses = serial_loop_extents_and_load_count * thread_info.num_threads;
    int min_accesses = min_required_accesses;
    double stride = 32.0;
    global_mem_info.add_access_info(min_required_accesses, min_accesses, stride);

    for (int i = 0; i < node->dimensions; i++) {
        GlobalMemInfo info;
        compute_num_global_mem_accesses_per_block(jac, node, producer_store_bounds, thread_info, i, serial_loop_extents_and_load_count, info, root);
        if (info.required_accesses() < min_required_accesses) {
            global_mem_info = info;
        }
    }
}

// Assumes block, serial, thread or block, thread nesting
const LoopNest* LoopNest::get_enclosing_block(const LoopNest *parent, const LoopNest *grandparent) const {
    internal_assert(gpu_label == thread);

    if (parent->gpu_label == block && grandparent->is_root()) {
        return parent;
    }

    if (parent->gpu_label == serial && grandparent->gpu_label == block) {
        return grandparent;
    }

    internal_error << "Invalid nesting: " << parent->gpu_label << ", " << grandparent->gpu_label << "\n";
    return nullptr;
}

std::pair<int64_t, int64_t> LoopNest::get_block_and_serial_extents(const LoopNest* block) const {
    int max_blocks[3] = {2147483647, 65535, 65535};

    std::vector<int64_t> lowered_size;
    lowered_dims(block->size, block->vectorized_loop_index, lowered_size);

    int64_t block_extents = 1;

    int i = 0;
    for (int N = std::min(3, (int)lowered_size.size()); i < N; ++i) {
        if (lowered_size[i] > max_blocks[i]) {
            break;
        }

        block_extents *= lowered_size[i];
    }

    int64_t serial_extents = 1;
    for (; i < (int)lowered_size.size(); ++i) {
        serial_extents *= lowered_size[i];
    }

    return {block_extents, serial_extents};
}

bool LoopNest::all_paths_to_leaves_have_thread_loop() const {
    if (gpu_label == thread) {
        return true;
    }

    if (children.size() == 0) {
        return false;
    }

    for (const auto &c : children) {
        if (!c->all_paths_to_leaves_have_thread_loop()) {
            return false;
        }
    }

    return true;
}

bool LoopNest::has_thread_loop_descendant() const {
    if (gpu_label == thread) {
        return true;
    }

    for (const auto &c : children) {
        if (c->has_thread_loop_descendant()) {
            return true;
        }
    }

    return false;
}

void LoopNest::compute_warp_features(ScheduleFeatures& features, const GPULoopInfo& gpu_loop_info) const {
    const ThreadInfo* thread_info = gpu_loop_info.thread_info;
    features.warp_lane_utilization = thread_info->warp_lane_utilization();
    features.warp_lane_utilization_at_block = thread_info->total_warp_lane_utilization_at_block();
    features.warp_lane_utilization_at_block_x = thread_info->warp_lane_utilization_at_block_x();
    features.warp_lane_utilization_at_block_y = thread_info->warp_lane_utilization_at_block_y();
    features.warp_lane_utilization_at_block_z = thread_info->warp_lane_utilization_at_block_z();
    features.num_warps_per_block = thread_info->num_warps_per_block;
    features.num_blocks = gpu_loop_info.num_blocks;
    features.block_occupancy = thread_info->block_occupancy();

    internal_assert(in_range_zero_one(features.block_occupancy)) << "Invalid block occupancy: " << features.block_occupancy;
    internal_assert(in_range_zero_one(features.warp_lane_utilization)) << "Invalid warp utilization: " << features.warp_lane_utilization;
    internal_assert(in_range_zero_one(features.warp_lane_utilization_at_block)) << "Invalid warp utilization at block: " << features.warp_lane_utilization_at_block;
    internal_assert(in_range_zero_one(features.warp_lane_utilization_at_block_x)) << "Invalid warp utilization at block x: " << features.warp_lane_utilization_at_block_x;
    internal_assert(in_range_zero_one(features.warp_lane_utilization_at_block_y)) << "Invalid warp utilization at block y: " << features.warp_lane_utilization_at_block_y;
    internal_assert(in_range_zero_one(features.warp_lane_utilization_at_block_z)) << "Invalid warp utilization at block z: " << features.warp_lane_utilization_at_block_z;
}

// Assume that when a block is active, all its warps are active
void LoopNest::compute_warp_and_block_occupancy(ScheduleFeatures &feat, const GPULoopInfo& gpu_loop_info) const {
    // Only compute these features for stage's that actually have a block
    // loop
    if (node != gpu_loop_info.current_block_loop->node) {
        return;
    }

    auto active_block_hardware_limit = get_active_block_hardware_limit();
    auto active_warp_hardware_limit = get_active_warp_hardware_limit();

    int64_t num_warps_per_block = gpu_loop_info.thread_info->num_warps_per_block;

    auto num_blocks = gpu_loop_info.num_blocks;

    auto max_theoretical_active_blocks = std::min(active_block_hardware_limit, num_blocks);
    auto max_active_warps = std::min(active_warp_hardware_limit, max_theoretical_active_blocks * num_warps_per_block);

    auto max_active_blocks = max_active_warps / num_warps_per_block;

    feat.max_warp_occupancy = (double)max_active_warps / (double)active_warp_hardware_limit;
    feat.max_block_occupancy = (double)max_active_blocks / (double)active_block_hardware_limit;
}

void LoopNest::compute_shared_mem_occupancy(const Target& target, int64_t working_set_here, ScheduleFeatures &feat) const {
    if (!is_gpu_block(target)) {
        return;
    }

    auto shared_mem_limit = get_shared_memory_limit();
    auto active_block_hardware_limit = get_active_block_hardware_limit();

    feat.shared_mem_occupancy = (double)working_set_here / (double)shared_mem_limit;

    if (working_set_here > 0) {
        auto shared_mem_max_active_blocks = std::min(active_block_hardware_limit, shared_mem_limit / working_set_here);
        feat.shared_mem_block_limit_factor = (double)shared_mem_max_active_blocks / (double)active_block_hardware_limit;

        internal_assert(feat.shared_mem_block_limit_factor <= 1) << "Invalid shared mem block limit factor: " << feat.shared_mem_block_limit_factor;
    }
}

std::pair<const LoopNest*, const LoopNest*> LoopNest::find_innermost_and_parent() const {
    internal_assert(!innermost);

    const LoopNest* parent = this;
    const LoopNest* child = nullptr;

    while (true) {
        for (const auto& c : parent->children) {
            if (c->node != node) {
                continue;
            }

            child = c.get();
        }

        internal_assert(child);

        if (child->innermost) {
            break;
        }

        parent = child;
    }

    return {child, parent};
}

int64_t LoopNest::compute_licm_amortization(const LoopNest* innermost, const LoopNest* parent, const ScheduleFeatures& feat, const LoadJacobian& jac, int producer_dims) const {
    // Is this load loop-invariant over an
    // unrolled block? If so, we amortize the
    // number of loads to account for LICM.
    int64_t amortization = 1;
    if (feat.unrolled_loop_extent <= 1) {
        return amortization;
    }

    for (size_t idx = 0; idx < innermost->stage->loop.size(); idx++) {
        if (!innermost->stage->loop[idx].rvar) {
            bool loop_invariant = true;
            for (int i = 0; i < producer_dims; i++) {
                if (!(jac(i, idx) == 0)) {
                    loop_invariant = false;
                    break;
                }
            }
            if (loop_invariant) {
                amortization *= parent->size[idx];
            }
        }
    }

    // TODO: LICM still acts for the innermost loop of non-unrolled things

    return amortization;
}

// Do a recursive walk over the loop nest computing features to feed the cost model.
void LoopNest::compute_features(const FunctionDAG &dag,
                      const MachineParams &params,
                      const Target& target,
                      const StageMap<Sites> &sites,
                      int64_t instances,
                      int64_t parallelism,
                      const LoopNest *parent,
                      const LoopNest *grandparent,
                      const LoopNest &root,
                      int64_t *working_set,
                      StageMap<ScheduleFeatures> *features,
                      GPULoopInfo gpu_loop_info) const {

    gpu_loop_info.update(target, this);
    std::unique_ptr<ThreadInfo> thread_info;

    if (is_gpu_thread(target)) {
        thread_info = gpu_loop_info.create_thread_info();
    }

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
            c->compute_features(dag, params, target, sites, subinstances, parallelism, this, parent, root, &working_set_here, features, gpu_loop_info);
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
        c->compute_features(dag, params, target, sites, subinstances, subparallelism, this, parent, root, &working_set_here, features, gpu_loop_info);
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

    if (is_gpu_thread(target)) {
        feat.working_set_at_thread = working_set_here;
    }

    if (at_task) {
        set_working_set_at_task_feature(working_set_here, features);
    }

    if (at_production) {
        feat.working_set = working_set_here;
    }

    if (innermost) {
        bool parent_unrolled =
            (feat.innermost_pure_loop_extent <= get_unroll_limit(target) &&
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
    GlobalMemInfo global_mem_loads;
    double num_shared_mem_loads_per_block = 0;
    double min_num_shared_mem_loads_per_block = 0;
    int64_t total_serial_loop_extents = 1;

    if (innermost || at_production) { // These are the sites at which we compute load footprints
        // Pick the site at which we will compute the footprint relationship
        const auto &consumer_site = sites.get(stage);

        // The store_at location of the consumer
        const auto *consumer_store_site = innermost ? parent : consumer_site.store;

        std::vector<int64_t> inner_serial_loop_extents;

        if (innermost && !stage->store_jacobian->empty()) {
            const auto& bounds = consumer_site.store->get_bounds(stage->node);
            inner_serial_loop_extents = gpu_loop_info.get_inner_serial_loop_extents(this);
            auto store_jac = *stage->store_jacobian * inner_serial_loop_extents;

            compute_gpu_store_features(
                store_jac,
                vector_dim,
                stage->node,
                bounds,
                *gpu_loop_info.thread_info,
                gpu_loop_info.total_serial_extents(),
                consumer_site,
                feat,
                root
            );

            feat.num_shared_mem_stores = gpu_loop_info.num_blocks * feat.num_shared_mem_stores_per_block;
        }

        // The parallel loop of the consumer
        const auto *consumer_task_site = consumer_site.task;

        int64_t consumer_instances = innermost ? instances : feat.num_realizations;
        internal_assert(consumer_instances != 0);

        vector<const FunctionDAG::Node::Stage *> pending;
        pending.emplace_back(stage);
        vector<pair<LoadJacobian, FunctionDAG::Node *>> jacobians;
        vector<pair<LoadJacobian, FunctionDAG::Node *>> thread_jacobians;
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

                            // Thread loops may not be innermost so in the
                            // Jacobians we need to account for the stride
                            // of the inner loops
                            thread_jacobians.emplace_back(j * inner_serial_loop_extents, e->producer);
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

                        // Consumer was inlined. Concat the jacobians to look through it.
                        decltype(jacobians) new_thread_jacobians;
                        for (auto &j1 : thread_jacobians) {
                            if (e->consumer->node == j1.second) {
                                for (auto &j2 : e->load_jacobians) {
                                    LoadJacobian j = j2 * j1.first;
                                    new_thread_jacobians.emplace_back(j, e->producer);
                                }
                            } else {
                                new_thread_jacobians.emplace_back(std::move(j1));
                            }
                        }
                        thread_jacobians.swap(new_thread_jacobians);
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

                if (innermost) {
                    int producer_innermost_dim =
                        (e->producer->is_input ? 0 : // Assume default storage layout for inputs
                         !producer_has_been_scheduled ? -1 :
                         site.produce->vector_dim);

                    // Shared or global memory?
                    bool is_shared_mem = producer_store_site->gpu_label == block;
                    bool is_global_mem = producer_store_site->is_root();

                    // Grab the jacobians that describe the memory dependence
                    for (const auto &jac : thread_jacobians) {
                        if (jac.second != e->producer) continue;
                        double n = jac.first.count();

                        int64_t amortization = compute_licm_amortization(this, parent, feat, jac.first, e->producer->dimensions);
                        n /= amortization;

                        if (is_shared_mem) {
                            auto shared_mem_features = compute_shared_mem_load_features(
                                jac.first,
                                producer_innermost_dim,
                                e->producer,
                                producer_store_bounds,
                                producer_has_been_scheduled,
                                *gpu_loop_info.thread_info,
                                root,
                                total_serial_loop_extents
                            );
                            num_shared_mem_loads_per_block += n * shared_mem_features.first;
                            min_num_shared_mem_loads_per_block += n * shared_mem_features.second;
                        } else if (is_global_mem) {
                            compute_global_mem_load_features(
                                jac.first,
                                producer_innermost_dim,
                                e->producer,
                                producer_store_bounds,
                                producer_has_been_scheduled,
                                *gpu_loop_info.thread_info,
                                global_mem_loads,
                                n * total_serial_loop_extents,
                                root
                            );
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

        feat.num_shared_mem_loads_per_block = num_shared_mem_loads_per_block;
        feat.num_shared_mem_loads = gpu_loop_info.num_blocks * num_shared_mem_loads_per_block;
        if (min_num_shared_mem_loads_per_block > 0 && num_shared_mem_loads_per_block > 0) {
            feat.shared_mem_load_efficiency = min_num_shared_mem_loads_per_block / num_shared_mem_loads_per_block;
            internal_assert(in_range_zero_one(feat.shared_mem_load_efficiency)) << "Invalid shared mem load efficiency: " << feat.shared_mem_load_efficiency;
        }

        feat.num_global_mem_loads_per_block = global_mem_loads.required_accesses();
        feat.global_mem_load_efficiency = global_mem_loads.access_efficiency();
        feat.global_mem_load_coalesce_efficiency = global_mem_loads.coalesce_efficiency();

        internal_assert(in_range_zero_one(feat.global_mem_load_efficiency)) << "Invalid global mem load efficiency: " << feat.global_mem_load_efficiency;
        internal_assert(in_range_zero_one(feat.global_mem_load_coalesce_efficiency)) << "Invalid global mem load coalease efficiency: " << feat.global_mem_load_coalesce_efficiency;
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

    compute_shared_mem_occupancy(target, working_set_here, feat);

    if (innermost && !is_scalar()) {
        compute_warp_features(feat, gpu_loop_info);
        compute_warp_and_block_occupancy(feat, gpu_loop_info);
    }
}

// Get the region required of a Func at this site, from which we
// know what region would be computed if it were scheduled here,
// and what its loop nest would be.
const Bound &LoopNest::get_bounds(const FunctionDAG::Node *f) const {
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
void LoopNest::dump(string prefix, const LoopNest *parent) const {
    if (!is_root()) {
        // Non-root nodes always have parents.
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
    } else if (gpu_label == block) {
        aslog(0) << " gpu_block\n";
    } else if (gpu_label == serial) {
        aslog(0) << " gpu_serial\n";
    } else if (gpu_label == none) {
        aslog(0) << " gpu_none\n";
    } else if (gpu_label == simd) {
        aslog(0) << " gpu_simd\n";
    } else if (gpu_label == thread) {
        aslog(0) << " gpu_thread\n";
    } else if (gpu_label == parallelized) {
        aslog(0) << " gpu_parallelized\n";
    } else if (parallel) {
        aslog(0) << " p\n";
    } else {
        aslog(0) << '\n';
    }
    for (auto p : store_at) {
        aslog(0) << prefix << "realize: " << p->func.name() << " [";
        for (int i = 0; i < p->dimensions; i++) {
            if (i > 0) {
                aslog(0) << ", ";
            }
            const auto& region = get_bounds(p)->region_computed(i);
            aslog(0) << region.extent();
            if (region.constant_extent()) {
                aslog(0) << "c";
            }
        }
        aslog(0) << "] with " << p->stages.size() << " stages\n";
    }
    for (size_t i = children.size(); i > 0; i--) {
        children[i-1]->dump(prefix, this);
    }
    for (auto it = inlined.begin(); it != inlined.end(); it++) {
        aslog(0) << prefix << "inlined: " << it.key()->func.name() << " " << it.value() << '\n';
    }
}

// Does this loop nest access the given Func
bool LoopNest::calls(const FunctionDAG::Node *f) const {
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
int64_t LoopNest::max_inlined_calls() const {
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
bool LoopNest::accesses_input_buffer() const {
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
bool LoopNest::computes(const FunctionDAG::Node *f) const {
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
void LoopNest::inline_func(const FunctionDAG::Node *f) {
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
void LoopNest::compute_here(const FunctionDAG::Node *f,
                  bool tileable,
                  int v,
                  bool in_threads_loop,
                  const Target &target) {
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

        // always set gpu_label as thread if legal.
        // if !in_threads_loop we are computing either at root level or inside a serial loop
        // set gpu_label to none, then call parallelize_in_tiles to create a parallel, serial, SIMD loop
        // if compute_root set gpu_label to none, parallelize_in_tiles creates block and thread loops later
        // if computing at serial loop set gpu_label to thread.
        if (target.has_gpu_feature()) {
            if (is_root()) {
                node->gpu_label = none;
            } else if (!in_threads_loop) {
                node->gpu_label = thread;
            } else {
                node->gpu_label = serial;
            }
        }
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
            one_vector->gpu_label = simd;
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
IntrusivePtr<const LoopNest> LoopNest::parallelize_in_tiles(const MachineParams &params,
                                                  const vector<int64_t> &tiling,
                                                  const LoopNest *parent,
                                                  const Target& target,
                                                  bool inner_tiling,
                                                  bool adjust_tiling) const {

    // Split this loop and move factors to the inner loop
    LoopNest *inner = new LoopNest, *outer = new LoopNest;
    inner->node      = outer->node      = node;
    inner->stage     = outer->stage     = stage;
    inner->tileable  = outer->tileable  = tileable && may_subtile();
    inner->vector_dim = outer->vector_dim = vector_dim;
    inner->vectorized_loop_index = outer->vectorized_loop_index = vectorized_loop_index;

    if (target.has_gpu_feature()) {
        if (gpu_label == none) {
            inner->gpu_label = serial;
            outer->gpu_label = parallelized;
            outer->parallel = true;
        } else if (gpu_label == parallelized) {
            inner->gpu_label = thread; // compute root funcs always allowed to use GPU threads
            outer->gpu_label = block;
            outer->parallel = true;
        } else if (gpu_label == thread) {
            inner->gpu_label = serial;
            outer->gpu_label = thread;
            outer->parallel = false;
        } else {
            internal_error << "invalid gpu label " << gpu_label << " for parallelized loop\n";
        }
    }

    outer->size = size;
    outer->innermost = false;

    if (!target.has_gpu_feature())
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
        if (inner_tiling) {
            if (l >= 0) {
                internal_assert(l < (int)tiling.size()) << l << " " << tiling.size() << "\n";
                outer_extent = (outer->size[i] + tiling[l] - 1) / tiling[l];
                inner->size[i] = tiling[l];
            } else {
                // RVars are moved inwards
                outer_extent = 1;
                inner->size[i] = outer->size[i];
            }
            if (adjust_tiling) {
                inner->size[i] = (outer->size[i] + outer_extent - 1) / outer_extent;
            }
        } else {
            if (l >= 0) {
                internal_assert(l < (int)tiling.size()) << l << " " << tiling.size() << "\n";
                inner->size[i] = (outer->size[i] + tiling[l] - 1) / tiling[l];
                outer_extent = tiling[l];
            } else {
                outer_extent = 1;
                inner->size[i] = outer->size[i];
            }
            if (adjust_tiling) {
                outer_extent = (outer->size[i] + inner->size[i] - 1) / inner->size[i];
            }
        }
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

// All store ats further in than the block level must be fixed
// sized allocations. This method checks if f will require a dynamic
// allocation
bool LoopNest::requires_dynamic_allocation(const FunctionDAG::Node *f, const Target &target, bool in_threads_loop) const {
    if (!target.has_gpu_feature() || !in_threads_loop) {
        return false;
    }

    for (int i = 0; i < f->dimensions; i++) {
        if (!get_bounds(f)->region_computed(i).constant_extent()) {
            return true;
        }
    }

    return false;
}

// Return all possible ways to compute f in tiles somewhere within
// this loop nest.
// in_threads_loop tracks whether or not function is going to be placed inside a
// loop marked gpu_threads, in which case f's loops cannot be gpu_threads
vector<IntrusivePtr<const LoopNest>> LoopNest::compute_in_tiles(const FunctionDAG::Node *f,
                                                      const LoopNest *parent,
                                                      const MachineParams &params,
                                                      const Target &target,
                                                      int v,
                                                      bool in_realization,
                                                      bool in_threads_loop,
                                                      vector<int64_t> union_counts) const {
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

    // once we enter a gpu block loop compute union thread counts to pass down
    if (gpu_label == block) {
        union_counts = get_union_thread_counts(f);
    }

    bool can_allocate_here = !target.has_gpu_feature() || in_realization || !in_threads_loop || !node_has_dynamic_region_computed(f);

    // Place the computation directly inside this loop (provided it's not a SIMD loop)
    if (!innermost &&
        (!in_realization ||
         size.empty() ||
         vector_dim == -1 ||
         size[vector_dim] == 1) && can_allocate_here) {

        std::unique_ptr<LoopNest> r{new LoopNest};
        r->copy_from(*this);
        r->compute_here(f, true, v, in_threads_loop, target);
        if (!in_realization) {
            r->store_at.insert(f);
        } else {
            r->tileable = false;
        }

        // if GPU and creating a threads loop INSIDE a block loop, create child for each thread tiling
        if ( !is_root() && !in_threads_loop && target.has_gpu_feature() ) {
            bool made_child = r->add_gpu_thread_tilings(f, params, target, v, result, union_counts);
            if (!made_child) { // no good thread tilings, just keep r with the untiled loop inserted as serial
                result.emplace_back(r.release());
            }
        } else { // computing at root or inside a threads loop
            result.emplace_back(r.release());
        }
    }

    if (f->is_output) {
        // Outputs must be compute_root, so we're done.
        return result;
    }

    if (tileable) {
        // The root node is not tileable, so all tileable nodes have parents.
        internal_assert(parent != nullptr);

        // Generate a list of tile sizes to try
        auto tilings = generate_tilings(size, (int)(size.size() - 1), 2, !in_realization, target);

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
            std::unique_ptr<LoopNest> inner{new LoopNest};
            std::unique_ptr<LoopNest> outer{new LoopNest};
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

            bool may_slide = (!in_realization &&
                              f->stages.size() == 1 &&
                              !target.has_gpu_feature()); // disable sliding for GPU, often not useful
            if (may_slide) {
                // should NEVER get here for GPU schedules, no sliding on GPU
                // Store here, but compute further in. Currently
                // don't have to worry about the constraints this
                // places on parallelism, as we forced all the
                // parallelism to the outer loop.
                auto opts = inner->compute_in_tiles(f, outer.get(), params, target, v, true, in_threads_loop);
                for (IntrusivePtr<const LoopNest> &n : opts) {
                    LoopNest *store_at_outer_compute_further_in = new LoopNest;
                    store_at_outer_compute_further_in->copy_from(*outer);
                    store_at_outer_compute_further_in->children.pop_back();
                    store_at_outer_compute_further_in->children.emplace_back(std::move(n));
                    result.emplace_back(store_at_outer_compute_further_in);
                }
            }

            outer->tileable &= !in_realization;

            if (!target.has_gpu_feature()) {
                outer->children.emplace_back(inner.release());
                // Site the computation inside the outer loop
                outer->compute_here(f, true, v, in_threads_loop, target);
                result.emplace_back(outer.release());
            } else {
                // Rules for assigning gpu_labels when splitting a loop:
                // threaded loops can be split into: (threaded, serial) or (serial, threaded)
                // block loops can only be split into: blocks, serial
                // serial loops can only be split into: serial, serial
                switch (gpu_label) {
                    case thread: {
                        // create (threads, serial) option

                        internal_assert(in_threads_loop); // threads loop can't be inside threads loop
                        if (in_realization || !node_has_dynamic_region_computed(f)) {
                            outer->gpu_label = thread;
                            inner->gpu_label = serial;

                            outer->children.emplace_back(inner.release());
                            outer->compute_here(f, true, v, true, target);

                            result.emplace_back(outer.release());
                        }
                        break;
                    }

                    case block: {
                        internal_assert(!in_threads_loop);
                        outer->gpu_label = block;
                        inner->gpu_label = serial;

                        outer->children.emplace_back(inner.release());
                        outer->compute_here(f, true, v, false, target);

                        bool made_child = outer->add_gpu_thread_tilings(f, params, target, v, result, union_counts);

                        // no good thread tilings, just add the untiled thread loop
                        if (!made_child) {
                            result.emplace_back(outer.release());
                        }
                        break;
                    }

                    case serial: {
                        if (in_realization || !in_threads_loop || !node_has_dynamic_region_computed(f)) {
                            outer->gpu_label = serial;
                            inner->gpu_label = serial;

                            outer->children.emplace_back(inner.release());
                            outer->compute_here(f, true, v, in_threads_loop, target);

                            if (!in_threads_loop) {
                                bool made_child = outer->add_gpu_thread_tilings(f, params, target, v, result, union_counts);

                                // no good thread tilings, just add the untiled thread loop
                                if (!made_child) {
                                    result.emplace_back(outer.release());
                                }
                            } else { // inside a threads loop, can't generate thread loop tilings
                                result.emplace_back(outer.release());
                            }
                        }
                        break;
                    }
                    case simd: {
                        internal_error << "attempting to split a SIMD loop\n";
                        break;
                    }
                    case none: {
                        internal_error << "attempting to split a loop with none gpu_label " << is_root() << " num children " << (int)(children.size()) << "\n";
                        break;
                    }
                    case parallelized: {
                        internal_error << "attempting to split a loop with parallelized gpu_label\n";
                        break;
                    }
                }
            }
        }
    }

    if (child >= 0 && !called_by_multiple_children && !in_realization &&
        (may_subtile() || is_root())) {
        // Push the Func further inwards in the loop nest

        // See if it's appropriate to slide over this loop. Can't
        // slide at the root level if we intend to parallelize it.
        bool may_slide = (params.parallelism == 1) || !is_root();

        // Disable sliding for GPU schedules because it's usually not useful
        may_slide &= !target.has_gpu_feature();

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

            in_threads_loop |= (children[child]->gpu_label == thread);
            // we must pass down union thread count constraints computed at block level when computing further in
            auto opts = children[child]->compute_in_tiles(f, this, params, target, v, store_here, in_threads_loop, union_counts);
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

// Apply the schedule represented by this loop nest to a Halide pipeline.
void LoopNest::apply(LoopLevel here,
           StageMap<std::unique_ptr<StageScheduleState>> &state_map,
           double num_cores,
           int depth,
           const LoopNest *parent,
           const LoopNest *compute_site,
           const Target& target,
           std::vector<StageScheduleState*>& ancestors) const {
    if (is_root()) {
        for (auto &c : children) {
            Func(c->node->func).compute_root();
            c->apply(LoopLevel::root(), state_map, num_cores, 1, this, c.get(), target, ancestors);
            if (c->stage->index == 0) {
                auto &state = state_map.get(c->stage);
                state->schedule_source << "\n    .compute_root()";
                // TODO: Omitting logic for printing store_root() assumes everything store_root is also compute root
            }
        }
    } else {
        // Non-root nodes always have parents.
        internal_assert(parent != nullptr);

        if (parent->node != node) {
            compute_site = this;
        }

        const auto &symbolic_loop = stage->loop;
        const auto &parent_bounds = parent->get_bounds(node);
        if (!state_map.contains(stage)) {
            StageScheduleState *state = new StageScheduleState;
            state->node = node;
            state->num_cores = num_cores;
            state->vector_dim = vector_dim;
            state->vectorized_loop_index = vectorized_loop_index;
            state->ancestors = ancestors;
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
                fv.parallel = l.pure && target.has_gpu_feature() ? gpu_label == block : parallel;
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
                if (!target.has_gpu_feature()) {
                    Func(node->func).store_in(MemoryType::Stack);
                    state.schedule_source << "\n    .store_in(MemoryType::Stack)";
                }
            }
        }

        // Pick a tail strategy for any splits of pure vars. RVars always use guardwithif
        auto pure_var_tail_strategy = TailStrategy::Auto;
        const bool might_access_gpu_shared = true; // Conservatively always true for now
        if (!might_access_gpu_shared && !compute_site->accesses_input_buffer() && !node->is_output) {
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
                // In case the threads loop is innermost
                for (size_t i = 0; i < symbolic_loop.size(); i++) {
                    StageScheduleState::FuncVar &v = state.vars[i];
                    v.gpu_threads = gpu_label == thread && symbolic_loop[i].pure;
                }

                if (vectorized_loop_index >= 0) {
                    size_t i = 0;
                    while (!state.vars[i].innermost_pure_dim) i++;
                    auto &v = state.vars[i];
                    internal_assert(v.innermost_pure_dim && v.exists) << v.var.name() << "\n";
                    // Is the result of a split

                    // The vector size for gpu depends on the width of the
                    // stage's types and will often be 1, in which case we
                    // don't want to vectorize the loop
                    if (!target.has_gpu_feature() || stage->vector_size > 1) {
                        state.schedule_source
                            << "\n    .vectorize(" << v.var.name() << ")";
                        s.vectorize(v.var);
                        v.vectorized = true;
                        state.vectorized = true;
                        state.vectorized_var = v;
                    }
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

                    parent.gpu_threads = gpu_label == thread && symbolic_loop[i].pure;

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
                        v.gpu_threads = false;

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
                        v.gpu_threads = false;
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

                    if (product_of_pure_loops <= get_unroll_limit(target) && all_pure_loops_constant_size) {
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
            ancestors.push_back(state_map.get(stage).get());
            c->apply(here, state_map, num_cores, depth + 1, this, compute_site, target, ancestors);
            ancestors.pop_back();
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

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide
