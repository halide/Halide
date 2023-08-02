#include "LoopNest.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>

using std::set;
using std::vector;

namespace Halide {
namespace Internal {
namespace Autoscheduler {

std::string stringify(GPU_parallelism label) {
    if (label == GPU_parallelism::Block) {
        return "block";
    }
    if (label == GPU_parallelism::Thread) {
        return "thread";
    }
    if (label == GPU_parallelism::Serial) {
        return "serial";
    }
    if (label == GPU_parallelism::Simd) {
        return "simd";
    }
    if (label == GPU_parallelism::Parallelized) {
        return "parallelized";
    }
    return "None";
}

// How small should an innermost loop cluster be before you just
// entirely unroll the thing
const int kUnrollLimitGPU = 16;

bool may_subtile(const Anderson2021Params &params) {
    return params.disable_subtiling == 0;
}

// Shared memory limit per block for the target GPU
int64_t get_shared_memory_limit(const Anderson2021Params &params) {
    return (int64_t)params.shared_memory_limit_kb * 1024;  // Convert to bytes
}

int64_t get_shared_memory_sm_limit(const Anderson2021Params &params) {
    return (int64_t)params.shared_memory_sm_limit_kb * 1024;  // Convert to bytes
}

// Maximum number of active blocks for the target GPU
int64_t get_active_block_hardware_limit(const Anderson2021Params &params) {
    return params.active_block_limit;
}

// Maximum number of active warps for the target GPU
int64_t get_active_warp_hardware_limit(const Anderson2021Params &params) {
    return params.active_warp_limit;
}

int get_unroll_limit(const Target &target) {
    return kUnrollLimitGPU;
}

bool in_range_zero_one(double x) {
    return x > 0 && x <= 1;
}

bool are_valid_thread_extents(const vector<int64_t> &counts) {
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

bool all(const vector<int> &v) {
    for (auto x : v) {
        if (!x) {
            return false;
        }
    }
    return true;
}

// given a newly inserted node f into this LoopNest, get union of thread counts in each dimension
// across all siblings of f.
vector<int64_t> LoopNest::get_union_thread_counts(const FunctionDAG::Node *f) const {
    vector<int64_t> max_size{1, 1, 1};
    // find the loop nests we just created and get max gpu_thread extents of other children
    for (const auto &c : children) {
        if (c->node != f) {
            if (c->gpu_label == GPU_parallelism::Thread) {
                vector<int64_t> lowered_size;
                lowered_dims(c->size, c->vectorized_loop_index, lowered_size);
                for (int dim = 0; dim < (int)(lowered_size.size()); dim++) {
                    if (dim >= (int)(max_size.size())) {
                        max_size.push_back(lowered_size[dim]);
                    } else {
                        max_size[dim] = std::max(max_size[dim], lowered_size[dim]);
                    }
                }
            } else if (!c->children.empty()) {  // descend into children for thread blocks in serial loops
                vector<int64_t> child_max_sizes = c->get_union_thread_counts(f);
                for (int dim = 0; dim < (int)(child_max_sizes.size()); dim++) {
                    if (dim >= (int)(max_size.size())) {
                        max_size.push_back(child_max_sizes[dim]);
                    } else {
                        max_size[dim] = std::max(max_size[dim], child_max_sizes[dim]);
                    }
                }
            }  // otherwise this a serial loop with no threaded descendants
        }
    }
    return max_size;
}

// given a newly inserted node f into this LoopNest, gets the size of
// all of f's stages and their pure_dim indices
void LoopNest::get_stage_sizes(const FunctionDAG::Node *f,
                               vector<vector<int64_t>> &stage_sizes,
                               vector<vector<int>> &pure_dims,
                               vector<int> &vectorized_indices) const {
    stage_sizes.resize(f->stages.size());
    pure_dims.resize(f->stages.size());
    vectorized_indices.resize(f->stages.size());
    for (const auto &c : children) {
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
                                      const Anderson2021Params &params,
                                      const Target &target,
                                      int v,
                                      vector<IntrusivePtr<const LoopNest>> &result,
                                      const vector<int64_t> &max_size) {
    vector<vector<int64_t>> stage_sizes;
    vector<vector<int>> pure_dims;
    vector<int> vectorized_indices;
    this->get_stage_sizes(f, stage_sizes, pure_dims, vectorized_indices);
    internal_assert(!stage_sizes.empty());
    auto tilings = generate_gpu_tilings(stage_sizes,
                                        pure_dims,
                                        max_size,
                                        (int)(stage_sizes[0].size() - 1),
                                        vectorized_indices,
                                        true,
                                        false);
    bool made_child = false;
    for (const auto &t : tilings) {
        LoopNest *new_parent = new LoopNest;
        new_parent->copy_from(*(this));
        for (auto &c : new_parent->children) {
            if (c->node == f) {
                c = c->parallelize_in_tiles(t, new_parent, params, target, false, false);
            }
        }
        result.emplace_back(new_parent);
        made_child = true;
    }
    if (!made_child) {  // if we can't tile into gpu threads the inserted node, make it serial
        for (auto &c : children) {
            if (c->node == f) {
                c->gpu_label = GPU_parallelism::Serial;
            }
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
    features.clear();
};

void LoopNest::copy_from_including_features(const LoopNest &n) {
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
    features = n.features;
    feature_intermediates = n.feature_intermediates;
};

// Hash the loop structure and sizes up to a fixed depth. This is
// used as the hash function for the coarse-to-fine beam search in
// the paper.
void LoopNest::structural_hash(uint64_t &h, int depth) const {
    if (depth < 0) {
        return;
    }

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

        hash_combine(h, vector_dim);
    }

    if (depth > 1) {
        // Descend into children
        for (const auto &c : children) {
            c->structural_hash(h, depth - 2);
        }
    }
}

GPUMemoryType LoopNest::get_gpu_memory_type(bool in_block, bool in_thread, bool is_inlined) const {
    if (is_inlined) {
        return GPUMemoryType::Inlined;
    }

    if (in_thread) {
        internal_assert(in_block);
        return GPUMemoryType::Local;
    }

    if (in_block) {
        return GPUMemoryType::Shared;
    }

    return GPUMemoryType::Global;
}

std::vector<int> LoopNest::unrolled_loops(const Target &target,
                                          const LoopNest *parent,
                                          const LoopNest *grandparent) const {
    internal_assert(innermost);
    const auto &grandparent_bounds = grandparent->get_bounds(node);
    std::vector<int> unrolled(parent->size.size(), 0);

    if (parent->node != node) {
        return unrolled;
    }

    int64_t total_extent = 1;
    for (size_t i = 0; i < parent->size.size(); i++) {
        if (!stage->loop[i].rvar) {
            const auto &l = grandparent_bounds->loops(parent->stage->index, i);
            unrolled[i] = l.constant_extent();
            total_extent *= l.extent();
        }
    }

    if (total_extent <= get_unroll_limit(target)) {
        return unrolled;
    }

    std::fill(unrolled.begin(), unrolled.end(), 0);
    return unrolled;
}

bool accessed_at_constant_indices(const std::vector<int> &unrolled, const FunctionDAG::Edge *e) {
    for (const auto &jac : e->load_jacobians) {
        for (size_t loop_index = 0; loop_index < unrolled.size(); ++loop_index) {
            for (int i = 0; i < e->producer->dimensions; ++i) {
                // There are two ways for an index to be constant:
                // 1. It's an actual constant i.e. the jac entry = 0
                // 2. It has a known stride and the loop accessing it is
                // unrolled
                if (!(jac(i, loop_index) == 0) && (!jac(i, loop_index).exists() || !unrolled[loop_index])) {
                    return false;
                }
            }
        }
    }

    return true;
}

void LoopNest::get_allocs_that_can_be_promoted_to_registers(const Target &target,
                                                            StageMap<Sites> &sites,
                                                            NodeMap<bool> &can_be_promoted_to_registers,
                                                            const LoopNest *grandparent,
                                                            const LoopNest *parent) const {
    for (const auto *alloc_node : store_at) {
        const auto &store_site = sites.get(&alloc_node->stages[0]);
        if (store_site.gpu_store_memory_type != GPUMemoryType::Local) {
            continue;
        }

        can_be_promoted_to_registers.get_or_create(alloc_node) = store_site.is_constant_allocation &&
                                                                 store_site.allocation_size <= get_register_mem_alloc_limit();
    }

    for (const auto &c : children) {
        c->get_allocs_that_can_be_promoted_to_registers(target, sites, can_be_promoted_to_registers, parent, this);
    }

    if (innermost) {
        auto unrolled = unrolled_loops(target, parent, grandparent);

        for (const auto *e : stage->incoming_edges) {
            if (sites.get(&e->producer->stages[0]).gpu_store_memory_type != GPUMemoryType::Local) {
                continue;
            }

            can_be_promoted_to_registers.get(e->producer) = can_be_promoted_to_registers.get(e->producer) &&
                                                            accessed_at_constant_indices(unrolled, e);
        }
    }
}

// Compute all the sites of interest for each pipeline stage
void LoopNest::get_sites(const Target &target,
                         StageMap<Sites> &sites,
                         StageMap<int64_t> &total_shared_mem_alloc_sizes,
                         const LoopNest *task,
                         const LoopNest *parent,
                         const LoopNest *current_thread_loop) const {
    if (is_gpu_thread(target)) {
        current_thread_loop = this;
    }

    if (!task && !is_root()) {
        task = this;
    }

    for (const auto &c : children) {
        c->get_sites(target, sites, total_shared_mem_alloc_sizes, task, this, current_thread_loop);
    }
    if (parent && node != parent->node) {
        auto &s = sites.get_or_create(stage);
        s.compute = parent;
        s.produce = this;
        s.task = task;
    }

    bool in_block = task != nullptr;
    bool in_thread = current_thread_loop != nullptr;

    for (const auto *f : store_at) {
        auto store_gpu_memory_type = get_gpu_memory_type(in_block, in_thread);

        for (const auto &s : f->stages) {
            sites.get_or_create(&s).store = this;
            sites.get_or_create(&s).gpu_store_memory_type = store_gpu_memory_type;
            auto alloc = sites.get_or_create(&s).store->compute_alloc_size_of_node_here(f);
            sites.get_or_create(&s).allocation_size = alloc.first;
            sites.get_or_create(&s).is_constant_allocation = alloc.second;

            const LoopNest *store_site = sites.get_or_create(&s).store;
            if (store_site->gpu_label == GPU_parallelism::Block && s.index == 0) {
                total_shared_mem_alloc_sizes.get_or_create(store_site->stage) += alloc.first;
            }
        }
    }
    for (auto it = inlined.begin(); it != inlined.end(); it++) {
        auto &s = sites.get_or_create(&(it.key()->stages[0]));
        s.inlined = true;
        // These values will be unreliable for inlined Funcs that are located
        // at multiple different locations
        s.compute = s.store = s.produce = s.innermost = this;

        // Accumulate all the innermost loop nests into which this func is
        // inlined
        s.inlined_innermosts.push_back(this);
        s.gpu_store_memory_type = GPUMemoryType::Inlined;
        s.task = task;
    }
    if (innermost) {
        sites.get_or_create(stage).innermost = this;
        sites.get_or_create(stage).thread = current_thread_loop;
    }
}

bool LoopNest::promote_allocs_to_registers(const Target &target, StageMap<Sites> &sites) const {
    NodeMap<bool> can_be_promoted_to_registers;
    get_allocs_that_can_be_promoted_to_registers(target, sites, can_be_promoted_to_registers, nullptr, nullptr);

    for (auto &node : can_be_promoted_to_registers) {
        if (!node.second) {
            return false;
        }

        for (const auto &stage : node.first->stages) {
            internal_assert(sites.get(&stage).gpu_store_memory_type == GPUMemoryType::Local);
            sites.get(&stage).gpu_store_memory_type = GPUMemoryType::Registers;
        }
    }

    return true;
}

bool LoopNest::exceeds_serial_extents_limit(const Target &target, const LoopNest *parent, bool in_threads_loop) const {
    bool parent_of_innermost = false;
    for (const auto &c : children) {
        if (c->node == node && c->innermost) {
            parent_of_innermost = true;
        }
    }

    if (gpu_label == GPU_parallelism::Serial && stage->index == 0) {
        int64_t serial_loop_extents = 1;
        for (const auto &i : stage->loop) {
            if (!i.pure) {
                continue;
            }

            serial_loop_extents *= size[i.pure_dim];
        }

        if (parent_of_innermost) {
            return serial_loop_extents > get_unroll_limit(target);
        }

        if (serial_loop_extents > 64) {
            return true;
        }
    }

    for (const auto &c : children) {
        if (c->exceeds_serial_extents_limit(target, this, in_threads_loop || c->gpu_label == GPU_parallelism::Thread)) {
            return true;
        }
    }

    return false;
}

bool LoopNest::node_has_dynamic_region_computed(const FunctionDAG::Node *f) const {
    for (int i = 0; i < f->dimensions; i++) {
        const auto &region = get_bounds(f)->region_computed(i);

        if (!region.constant_extent()) {
            return true;
        }
    }

    return false;
}

bool LoopNest::has_dynamic_allocation_inside_thread(bool in_thread_loop) const {
    in_thread_loop = in_thread_loop || (gpu_label == GPU_parallelism::Thread);

    if (in_thread_loop) {
        for (const auto &f : store_at) {
            if (node_has_dynamic_region_computed(f)) {
                return true;
            }
        }
    }

    for (const auto &child : children) {
        if (child->has_dynamic_allocation_inside_thread(in_thread_loop)) {
            return true;
        }
    }

    return false;
}

const LoopNest *LoopNest::find_pure_stage_loop_nest(const FunctionDAG::Node *node) const {
    const LoopNest *pure;
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

int LoopNest::get_pure_stage_vectorized_loop_index(const FunctionDAG::Node *node) const {
    const auto *pure = find_pure_stage_loop_nest(node);
    internal_assert(pure) << "No pure stage found for " << node->func.name() << "\n";
    return pure->vectorized_loop_index;
}

int LoopNest::get_vectorized_loop_index_from_pure_stage(const LoopNest &root) const {
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
double LoopNest::storage_stride(const LoadJacobian &jac,
                                int innermost_storage_dim,
                                const FunctionDAG::Node *storage_node,
                                const Bound &store_bounds,
                                const LoopNest &root) const {
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
    for (long storage_dim : storage_dims) {
        storage_strides.push_back(storage_stride);
        storage_stride *= store_bounds->region_required(storage_dim).extent();
    }

    int v = get_vectorized_loop_index_from_pure_stage(root);

    double stride = 0;
    for (std::size_t i = 0; i < storage_dims.size(); i++) {
        auto jac_stride = jac(storage_dims[i], v);

        float s = (float)jac_stride.numerator / (float)jac_stride.denominator;
        stride += s * storage_strides[i];
    }

    return std::abs(stride);
}

// Shared mem accesses with stride 1 will likely be vectorized
bool LoopNest::can_vectorize_access_for_innermost_dim(const LoadJacobian &jac,
                                                      const FunctionDAG::Node *accessed,
                                                      int innermost_dim,
                                                      int loop_index) const {
    for (int i = 0; i < accessed->dimensions; i++) {
        auto stride = jac(i, loop_index);
        if (i == innermost_dim) {
            if (!(stride == 1)) {
                return false;
            }
        } else if (!(stride == 0)) {
            return false;
        }
    }

    return true;
}

bool LoopNest::can_vectorize_store_access(const LoadJacobian &jac,
                                          const FunctionDAG::Node *accessed,
                                          bool accessed_has_been_scheduled,
                                          int innermost_dim,
                                          int loop_index,
                                          const GPUMemoryType &mem_type) const {
    if (loop_index < 0 || mem_type != GPUMemoryType::Shared) {
        return false;
    }

    internal_assert(innermost_dim >= 0);
    return can_vectorize_access_for_innermost_dim(jac, accessed, innermost_dim, loop_index);
}

int LoopNest::vectorized_load_access_size(const LoadJacobian &jac,
                                          const FunctionDAG::Node *accessed,
                                          bool accessed_has_been_scheduled,
                                          int innermost_dim,
                                          const GPUMemoryType &mem_type,
                                          bool verbose) const {
    int vector_size = 1;
    if (mem_type != GPUMemoryType::Shared) {
        return vector_size;
    }

    if (accessed_has_been_scheduled) {
        // Loads can potentially be vectorized in any loop dimension, not just
        // the vectorized_loop dimension. It's possible that some of the loop
        // dimensions will be removed by LICM but those indices won't conflict with
        // any potential vectorized indices because the Jacobian entry for them
        // must be 0 in all storage dimensions, whereas for vectorization it
        // must be 1 for the innermost_dim and 0 for all others
        for (size_t loop_index = 0; loop_index < size.size(); ++loop_index) {
            if (!can_vectorize_access_for_innermost_dim(jac, accessed, innermost_dim, loop_index)) {
                continue;
            }

            vector_size = std::max(vector_size, vectorized_access_size(loop_index, verbose));
        }

        if (verbose) {
            aslog(2) << "vector_size = " << vector_size << "\n";
        }

        return vector_size;
    }

    // If the producer has not been scheduled, try all of its dimensions as the
    // innermost storage dim to see if any can be vectorized
    for (int i = 0; i < accessed->dimensions; i++) {
        for (size_t loop_index = 0; loop_index < size.size(); ++loop_index) {
            if (!can_vectorize_access_for_innermost_dim(jac, accessed, i, loop_index)) {
                continue;
            }

            vector_size = std::max(vector_size, vectorized_access_size(loop_index, verbose));
        }
    }

    if (verbose) {
        aslog(2) << "vector_size = " << vector_size << "\n";
    }
    return vector_size;
}

int LoopNest::vectorized_access_size(size_t loop_index, bool verbose) const {
    int64_t extent = size[loop_index];
    constexpr int max_vector_size_in_bytes = 16;
    int64_t max_points_per_vector = std::min(4, max_vector_size_in_bytes / (int)node->bytes_per_point);

    if (verbose) {
        aslog(2) << "\nextent = " << extent;
        aslog(2) << "\nbytes_per_point = " << node->bytes_per_point;
        aslog(2) << "\nmax_points_per_vector = " << max_points_per_vector;
    }

    if (extent >= max_points_per_vector && extent % max_points_per_vector == 0) {
        return max_points_per_vector;
    }

    if (extent < max_points_per_vector && max_points_per_vector % extent == 0) {
        return extent;
    }

    return 1;
}

double LoopNest::compute_local_mem_stride(double stride, double bytes) const {
    // Each word is 4 bytes so adjust the stride based
    // on width of data being accessed
    double word_stride = (bytes / 4);
    int words_per_access = std::max(1.0, word_stride);
    stride *= words_per_access;

    stride = std::min(8.0, std::max(1.0, stride));

    return stride;
}

// Get the stride over "node's" storage and its element-wise stride for a unit
// increment in the given thread loops
Strides LoopNest::compute_strides(const LoadJacobian &jac,
                                  int innermost_storage_dim,
                                  const FunctionDAG::Node *storage_node,
                                  const Bound &store_bounds,
                                  const ThreadInfo *thread_info,
                                  bool verbose) const {
    internal_assert(innermost_storage_dim >= 0);

    if (verbose) {
        aslog(2) << "\nstrides: " << node->func.name() << " (stage = "
                 << stage->index << ") loading from "
                 << storage_node->func.name() << " ->\n";
        if (aslog::aslog_level() >= 2) {
            jac.dump("");
        }
    }

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
    if (verbose) {
        aslog(2) << "Storage stride: ";
    }
    for (long storage_dim : storage_dims) {
        storage_strides.push_back(storage_stride);
        if (verbose) {
            aslog(2) << storage_stride << " ";
        }
        storage_stride *= store_bounds->region_required(storage_dim).extent();
    }
    if (verbose) {
        aslog(2) << "\n";
    }

    Strides strides{storage_strides};
    for (const auto &thread_loop_var : thread_info->loop_vars) {
        int loop_index = stage->get_loop_index_from_var(thread_loop_var);
        bool loop_index_exists = loop_index >= 0;

        std::vector<double> index_strides;
        bool exists = true;
        for (std::size_t i = 0; i < storage_dims.size(); i++) {
            if (verbose) {
                aslog(2) << "loop_index for this stage = " << loop_index;
                aslog(2) << "; loop_var = " << thread_loop_var;
                aslog(2) << "; storage_dim = " << i;
            }

            if (loop_index_exists) {
                auto jac_stride = jac(storage_dims[i], loop_index);
                if (!jac_stride.exists()) {
                    if (verbose) {
                        aslog(2) << "; stride does not exist\n";
                        jac.dump("");
                    }
                    exists = false;
                    break;
                }

                float s = (float)jac_stride.numerator / (float)jac_stride.denominator;
                index_strides.push_back(s);
            } else {
                index_strides.push_back(0);
            }

            if (verbose) {
                aslog(2) << "; index_stride = " << index_strides.back() << "\n";
            }
        }

        if (exists) {
            strides.add_valid(index_strides);
            if (verbose) {
                aslog(2) << "adding valid stride\n";
            }
        } else {
            strides.add_invalid();
            if (verbose) {
                aslog(2) << "adding invalid stride\n";
            }
        }
    }

    if (verbose) {
        aslog(2) << "<- strides\n\n";
    }

    return strides;
}

bool LoopNest::all_strides_exist(const LoadJacobian &jac,
                                 const FunctionDAG::Node *storage_node,
                                 const LoopNest &root) const {
    int v = get_vectorized_loop_index_from_pure_stage(root);

    for (int i = 0; i < storage_node->dimensions; i++) {
        auto stride = jac(i, v);

        if (!stride.exists()) {
            return false;
        }
    }
    return true;
}

int LoopNest::get_actual_vector_dim(const Bound &store_bounds) const {
    if (store_bounds->region_computed(vector_dim).extent() > 1) {
        return vector_dim;
    }

    for (int i = 0; i < node->dimensions; ++i) {
        if (store_bounds->region_computed(i).extent() > 1) {
            return i;
        }
    }

    return vector_dim;
}

void LoopNest::compute_gpu_store_features(const LoadJacobian &jac,
                                          int consumer_innermost_dim,
                                          const FunctionDAG::Node *node,
                                          const Bound &consumer_store_bounds,
                                          const GPULoopInfo &gpu_loop_info,
                                          const std::vector<int64_t> &inner_serial_loop_extents,
                                          const Sites &consumer_site,
                                          ScheduleFeatures &feat,
                                          const LoopNest *parent,
                                          const LoopNest &root,
                                          GlobalMemInfo &global_mem_loads,
                                          SharedMemInfo &shared_mem_loads,
                                          LocalMemInfo &local_mem_loads,
                                          bool verbose) const {
    if (consumer_site.is_stored_in_registers()) {
        return;
    }

    internal_assert(gpu_loop_info.get_thread_info() != nullptr);
    const ThreadInfo *thread_info = gpu_loop_info.get_thread_info();
    bool is_shared_mem = consumer_site.gpu_store_memory_type == GPUMemoryType::Shared;

    size_t actual_vector_dim = get_actual_vector_dim(consumer_store_bounds);

    // If any of the store dimensions are constant over all the loop dimensions,
    // then the value to be stored will likely be held in a register and stored
    // once instead of on every iteration
    double total_serial_loop_extents = gpu_loop_info.total_serial_extents();
    int vector_size = 1;
    for (size_t loop_index = 0; loop_index < stage->loop.size(); ++loop_index) {
        bool constant = true;
        for (int i = 0; i < node->dimensions; ++i) {
            if (!(jac(i, loop_index) == 0)) {
                constant = false;
                break;
            }
        }

        if (constant) {
            total_serial_loop_extents /= parent->size[loop_index];
        } else if (can_vectorize_store_access(jac, node, true, actual_vector_dim, loop_index, consumer_site.gpu_store_memory_type)) {
            vector_size = std::max(vector_size, parent->vectorized_access_size(loop_index));
        }
    }
    total_serial_loop_extents /= vector_size;

    if (verbose) {
        std::string type = stage->index == 0 ? "store" : "load_and_store";
        std::string consumer_name = node->func.name();
        sanitize_names(consumer_name);
        std::string mem_type = "global";
        if (consumer_site.gpu_store_memory_type == GPUMemoryType::Shared) {
            mem_type = "shared";
        } else if (consumer_site.gpu_store_memory_type == GPUMemoryType::Local) {
            mem_type = "local";
        }
        aslog(2) << "BEGIN MEM ACCESS " << mem_type << "_mem_" << type;
        aslog(2) << ". consumer: " << consumer_name << "_s" << stage->index << "; producer: " << consumer_name << "\n";
        aslog(2) << "total_serial_loop_extents = " << total_serial_loop_extents << "\n";
    }

    if (is_shared_mem) {
        if (verbose) {
            aslog(2) << "vector_size = " << vector_size << "\n";
        }
        auto store_jac = jac * inner_serial_loop_extents;
        auto shared_mem_info = compute_mem_store_info<SharedMem>(
            store_jac,
            consumer_innermost_dim,
            node,
            consumer_store_bounds,
            thread_info,
            total_serial_loop_extents,
            verbose);

        feat.num_shared_mem_stores_per_block = shared_mem_info.num_transactions();
        if (stage->index > 0) {
            shared_mem_loads.add(shared_mem_info);
        }
        feat.shared_mem_store_efficiency = shared_mem_info.efficiency();

        internal_assert(in_range_zero_one(feat.shared_mem_store_efficiency))
            << "Invalid shared mem store efficiency: " << feat.shared_mem_store_efficiency
            << " for " << node->func.name();

    } else if (consumer_site.gpu_store_memory_type == GPUMemoryType::Global) {
        if (verbose) {
            aslog(2) << "vector_size = " << vector_size << "\n";
        }
        auto store_jac = jac * inner_serial_loop_extents;
        auto global_mem_info = compute_mem_store_info<GlobalMem>(
            store_jac,
            consumer_innermost_dim,
            node,
            consumer_store_bounds,
            thread_info,
            total_serial_loop_extents,
            verbose);

        feat.num_global_mem_stores_per_block = global_mem_info.num_transactions();
        if (stage->index > 0) {
            global_mem_loads.add(global_mem_info);
        }
        feat.global_mem_store_efficiency = global_mem_info.efficiency();

        internal_assert(in_range_zero_one(feat.global_mem_store_efficiency))
            << "Invalid global mem store efficiency: " << feat.global_mem_store_efficiency
            << " for " << node->func.name();

    } else if (consumer_site.gpu_store_memory_type == GPUMemoryType::Local) {
        auto local_mem_info = compute_mem_store_info<LocalMem>(
            jac,
            consumer_innermost_dim,
            node,
            consumer_store_bounds,
            thread_info,
            total_serial_loop_extents,
            verbose);
        // feat.num_local_mem_stores_per_block = local_mem_info.num_transactions();
        if (stage->index > 0) {
            local_mem_loads.add(local_mem_info);
        }
        // feat.local_mem_store_efficiency = local_mem_info.efficiency();

        // internal_assert(in_range_zero_one(feat.local_mem_store_efficiency))
        //     << "Invalid local mem store coalesce efficiency: " << feat.local_mem_store_efficiency
        //     << " for " << node->func.name();
    }

    if (verbose) {
        aslog(2) << "num_blocks = " << gpu_loop_info.num_blocks << "\n";
        std::string type = stage->index == 0 ? "store" : "load_and_store";
        std::string consumer_name = node->func.name();
        sanitize_names(consumer_name);
        std::string mem_type = "global";
        if (consumer_site.gpu_store_memory_type == GPUMemoryType::Shared) {
            mem_type = "shared";
        } else if (consumer_site.gpu_store_memory_type == GPUMemoryType::Local) {
            mem_type = "local";
        }
        aslog(2) << "END MEM ACCESS "
                 << mem_type << "_mem_" << type
                 << ". consumer: " << consumer_name
                 << "_s" << stage->index
                 << "; producer: " << consumer_name;
        if (!jac.all_coeffs_exist()) {
            aslog(2) << " (not all coeffs exist)";
        }
        aslog(2) << "\n\n";
    }
}

template<typename T>
void LoopNest::compute_num_mem_accesses_per_block(const LoadJacobian &jac,
                                                  const FunctionDAG::Node *node,
                                                  const Bound &store_bounds,
                                                  const ThreadInfo *thread_info,
                                                  int innermost_dim,
                                                  double num_requests_per_warp,
                                                  MemInfoType<T> &mem_info,
                                                  bool verbose) const {
    int bytes_per_access = node->bytes_per_point;

    // If the consumer is a scalar and is compute_root, then it will not be
    // surrounded by a gpu_threads loop, in which case thread_info will be null.
    // In this case, there is no need to compute the below thread/warp-related
    // details because only a single point is being computed
    if (!thread_info && is_scalar()) {
        mem_info.add_access_info(num_requests_per_warp, 1, bytes_per_access);
        return;
    }

    internal_assert(thread_info != nullptr);

    Strides strides = compute_strides(jac, innermost_dim, node, store_bounds, thread_info, verbose);

    size_t dimensions = thread_info->loop_indices.size();
    strides.dump(verbose);

    {
        int num_requests = thread_info->num_regular_active_warps_per_block * num_requests_per_warp;
        Accumulator<T> accumulator(bytes_per_access, dimensions, strides, verbose);
        thread_info->for_each_thread_id_in_first_warp(accumulator);

        accumulator.add_access_info(num_requests, mem_info, false);

        if (verbose) {
            aslog(2) << "num_requests_per_warp = " << num_requests_per_warp << "\n";
            aslog(2) << "num_regular_warps = " << thread_info->num_regular_active_warps_per_block << "\n";
        }
    }

    if (!thread_info->has_tail_warp) {
        return;
    }

    if (verbose) {
        aslog(2) << "\nBEGIN tail warp\n";
        aslog(2) << "# threads in tail warp: " << thread_info->num_threads_in_final_warp << "\n";
    }

    Accumulator<T> accumulator(bytes_per_access, dimensions, strides, verbose);
    thread_info->for_each_thread_id_in_tail_warp(accumulator);

    accumulator.add_access_info(num_requests_per_warp, mem_info, true);

    if (verbose) {
        aslog(2) << "END tail warp\n\n";
    }
}

template void LoopNest::compute_num_mem_accesses_per_block<GlobalMem>(const LoadJacobian &jac,
                                                                      const FunctionDAG::Node *node,
                                                                      const Bound &store_bounds,
                                                                      const ThreadInfo *thread_info,
                                                                      int innermost_dim,
                                                                      double num_requests_per_warp,
                                                                      MemInfoType<GlobalMem> &mem_info,
                                                                      bool verbose) const;

template void LoopNest::compute_num_mem_accesses_per_block<SharedMem>(const LoadJacobian &jac,
                                                                      const FunctionDAG::Node *node,
                                                                      const Bound &store_bounds,
                                                                      const ThreadInfo *thread_info,
                                                                      int innermost_dim,
                                                                      double num_requests_per_warp,
                                                                      MemInfoType<SharedMem> &mem_info,
                                                                      bool verbose) const;

template<>
void LoopNest::compute_num_mem_accesses_per_block<LocalMem>(const LoadJacobian &jac,
                                                            const FunctionDAG::Node *node,
                                                            const Bound &store_bounds,
                                                            const ThreadInfo *thread_info,
                                                            int innermost_dim,
                                                            double num_requests_per_warp,
                                                            MemInfoType<LocalMem> &mem_info,
                                                            bool verbose) const {
    int bytes_per_access = node->bytes_per_point;

    // If the consumer is a scalar and is compute_root, then it will not be
    // surrounded by a gpu_threads loop, in which case thread_info will be null.
    // In this case, there is no need to compute the below thread/warp-related
    // details because only a single point is being computed
    if (!thread_info && is_scalar()) {
        mem_info.add_access_info(num_requests_per_warp, 1, bytes_per_access);
        return;
    }

    {
        int num_requests = thread_info->num_regular_active_warps_per_block * num_requests_per_warp;
        LocalAccessAccumulator accumulator(bytes_per_access, verbose);
        thread_info->for_each_thread_id_in_first_warp(accumulator);

        accumulator.add_access_info(num_requests, mem_info, false);

        if (verbose) {
            aslog(2) << "num_requests_per_warp = " << num_requests_per_warp << "\n";
            aslog(2) << "num_regular_warps = " << thread_info->num_regular_active_warps_per_block << "\n";
        }
    }

    if (!thread_info->has_tail_warp) {
        return;
    }

    if (verbose) {
        aslog(2) << "\nBEGIN tail warp\n";
        aslog(2) << "# threads in tail warp: " << thread_info->num_threads_in_final_warp << "\n";
    }

    LocalAccessAccumulator accumulator(bytes_per_access, verbose);
    thread_info->for_each_thread_id_in_tail_warp(accumulator);

    accumulator.add_access_info(num_requests_per_warp, mem_info, true);

    if (verbose) {
        aslog(2) << "END tail warp\n\n";
    }
}

std::pair<double, double>
LoopNest::compute_local_mem_store_features(const LoadJacobian &jac,
                                           int consumer_innermost_dim,
                                           const FunctionDAG::Node *node,
                                           const Bound &consumer_store_bounds,
                                           const LoopNest &root,
                                           double serial_loop_extents) const {
    // Assume worst case serialized loads if the stride is unknown
    if (!all_strides_exist(jac, node, root)) {
        double stride = compute_local_mem_stride(32.0, node->bytes_per_point);
        double accesses = jac.count() * std::ceil((stride * serial_loop_extents) / 8.0);
        return {accesses, 1.0 / stride};
    }

    double stride = storage_stride(jac, consumer_innermost_dim, node, consumer_store_bounds, root);
    stride = compute_local_mem_stride(stride, node->bytes_per_point);
    double accesses = jac.count() * std::ceil((stride * serial_loop_extents) / 8.0);
    return {accesses, 1.0 / stride};
}

template<typename T>
MemInfoType<T> LoopNest::compute_mem_store_info(const LoadJacobian &jac,
                                                int consumer_innermost_dim,
                                                const FunctionDAG::Node *node,
                                                const Bound &consumer_store_bounds,
                                                const ThreadInfo *thread_info,
                                                double serial_loop_extents,
                                                bool verbose) const {
    MemInfoType<T> mem_info;

    compute_num_mem_accesses_per_block<T>(jac,
                                          node,
                                          consumer_store_bounds,
                                          thread_info,
                                          consumer_innermost_dim,
                                          serial_loop_extents,
                                          mem_info, verbose);
    return mem_info;
}

template MemInfoType<GlobalMem> LoopNest::compute_mem_store_info<GlobalMem>(const LoadJacobian &jac,
                                                                            int consumer_innermost_dim,
                                                                            const FunctionDAG::Node *node,
                                                                            const Bound &consumer_store_bounds,
                                                                            const ThreadInfo *thread_info,
                                                                            double serial_loop_extents,
                                                                            bool verbose) const;

template MemInfoType<SharedMem> LoopNest::compute_mem_store_info<SharedMem>(const LoadJacobian &jac,
                                                                            int consumer_innermost_dim,
                                                                            const FunctionDAG::Node *node,
                                                                            const Bound &consumer_store_bounds,
                                                                            const ThreadInfo *thread_info,
                                                                            double serial_loop_extents,
                                                                            bool verbose) const;

template<typename T>
void LoopNest::compute_mem_load_features(const LoadJacobian &jac,
                                         int producer_innermost_dim,
                                         const FunctionDAG::Node *node,
                                         const Bound &producer_store_bounds,
                                         bool producer_has_been_scheduled,
                                         const ThreadInfo *thread_info,
                                         MemInfoType<T> &mem_info,
                                         double points_accessed_per_thread,
                                         bool verbose) const {
    if (producer_has_been_scheduled) {
        compute_num_mem_accesses_per_block<T>(jac,
                                              node,
                                              producer_store_bounds,
                                              thread_info,
                                              producer_innermost_dim,
                                              points_accessed_per_thread,
                                              mem_info,
                                              verbose);

        return;
    }

    // Assume best case if producer has not been scheduled: try all the
    // possible innermost dimensions and take the best
    int min_required_accesses = 0;
    MemInfoType<T> min_info;

    for (int i = 0; i < node->dimensions; i++) {
        MemInfoType<T> info;
        compute_num_mem_accesses_per_block<T>(jac,
                                              node,
                                              producer_store_bounds,
                                              thread_info,
                                              i,
                                              points_accessed_per_thread,
                                              info,
                                              verbose);
        if (i == 0 || info.num_transactions() < min_required_accesses) {
            min_info = info;
            min_required_accesses = info.num_transactions();
        }
    }

    mem_info.add(min_info);
}

template void LoopNest::compute_mem_load_features<GlobalMem>(const LoadJacobian &jac,
                                                             int producer_innermost_dim,
                                                             const FunctionDAG::Node *node,
                                                             const Bound &producer_store_bounds,
                                                             bool producer_has_been_scheduled,
                                                             const ThreadInfo *thread_info,
                                                             MemInfoType<GlobalMem> &mem_info,
                                                             double points_accessed_per_thread,
                                                             bool verbose) const;

template void LoopNest::compute_mem_load_features<SharedMem>(const LoadJacobian &jac,
                                                             int producer_innermost_dim,
                                                             const FunctionDAG::Node *node,
                                                             const Bound &producer_store_bounds,
                                                             bool producer_has_been_scheduled,
                                                             const ThreadInfo *thread_info,
                                                             MemInfoType<SharedMem> &mem_info,
                                                             double points_accessed_per_thread,
                                                             bool verbose) const;

template<>
void LoopNest::compute_mem_load_features<LocalMem>(const LoadJacobian &jac,
                                                   int producer_innermost_dim,
                                                   const FunctionDAG::Node *node,
                                                   const Bound &producer_store_bounds,
                                                   bool producer_has_been_scheduled,
                                                   const ThreadInfo *thread_info,
                                                   MemInfoType<LocalMem> &mem_info,
                                                   double points_accessed_per_thread,
                                                   bool verbose) const {
    compute_num_mem_accesses_per_block<LocalMem>(jac,
                                                 node,
                                                 producer_store_bounds,
                                                 thread_info,
                                                 producer_innermost_dim,
                                                 points_accessed_per_thread,
                                                 mem_info,
                                                 verbose);
}

// Assumes block, serial, thread or block, thread nesting
const LoopNest *LoopNest::get_enclosing_block(const LoopNest *parent, const LoopNest *grandparent) const {
    internal_assert(gpu_label == GPU_parallelism::Thread);

    if (parent->gpu_label == GPU_parallelism::Block && grandparent->is_root()) {
        return parent;
    }

    if (parent->gpu_label == GPU_parallelism::Serial && grandparent->gpu_label == GPU_parallelism::Block) {
        return grandparent;
    }

    internal_error << "Invalid nesting: " << stringify(parent->gpu_label) << ", " << stringify(grandparent->gpu_label)
                   << "\n";
    return nullptr;
}

std::pair<int64_t, int64_t> LoopNest::get_block_and_serial_extents(const LoopNest *block) const {
    constexpr int max_blocks[3] = {2147483647, 65535, 65535};
    int block_extents[3] = {1, 1, 1};

    std::vector<int64_t> lowered_size;
    lowered_dims(block->size, block->vectorized_loop_index, lowered_size);

    int64_t total_block_extents = 1;

    size_t i = 0;
    size_t block_i = 0;
    for (size_t N = lowered_size.size(); i < N && block_i < 3; ++i) {
        if (lowered_size[i] * block_extents[block_i] > max_blocks[block_i]) {
            ++block_i;
            continue;
        }

        block_extents[block_i] *= lowered_size[i];
        total_block_extents *= lowered_size[i];
    }

    int64_t serial_extents = 1;
    for (; i < lowered_size.size(); ++i) {
        serial_extents *= lowered_size[i];
    }

    internal_assert(serial_extents == 1);
    return {total_block_extents, serial_extents};
}

bool LoopNest::all_paths_to_leaves_have_thread_loop() const {
    if (gpu_label == GPU_parallelism::Thread) {
        return true;
    }

    if (children.empty()) {
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
    if (gpu_label == GPU_parallelism::Thread) {
        return true;
    }

    for (const auto &c : children) {
        if (c->has_thread_loop_descendant()) {
            return true;
        }
    }

    return false;
}

void LoopNest::compute_warp_features(ScheduleFeatures &features, const GPULoopInfo &gpu_loop_info) const {
    const ThreadInfo *thread_info = gpu_loop_info.get_thread_info();
    features.warp_lane_utilization = thread_info->warp_lane_utilization();
    features.num_active_warps_per_block = thread_info->num_active_warps_per_block;
    features.idle_lane_wastage = thread_info->idle_lane_wastage();
    features.num_warps_per_block = thread_info->num_warps_per_block;
    features.num_blocks = gpu_loop_info.num_blocks;
    features.block_occupancy = thread_info->block_occupancy();
    features.num_threads_per_block = thread_info->num_threads;

    internal_assert(in_range_zero_one(features.block_occupancy))
        << "Invalid block occupancy: " << features.block_occupancy;
    internal_assert(in_range_zero_one(features.warp_lane_utilization))
        << "Invalid warp utilization: " << features.warp_lane_utilization;
}

// Assume that when a block is active, all its warps are active
void LoopNest::compute_warp_and_block_occupancy(const Anderson2021Params &params,
                                                ScheduleFeatures &feat,
                                                const GPULoopInfo &gpu_loop_info) const {
    // Only compute these features for stage's that actually have a block
    // loop
    if (node != gpu_loop_info.current_block_loop->node) {
        return;
    }

    auto active_block_hardware_limit = get_active_block_hardware_limit(params);
    auto active_warp_hardware_limit = get_active_warp_hardware_limit(params);

    const ThreadInfo *thread_info = gpu_loop_info.get_thread_info();
    internal_assert(thread_info != nullptr);
    int64_t num_warps_per_block = thread_info->num_warps_per_block;

    int64_t num_blocks = std::ceil(gpu_loop_info.num_blocks / (double)params.parallelism);

    auto max_theoretical_active_blocks = std::min(active_block_hardware_limit, num_blocks);
    auto max_active_warps = std::min(active_warp_hardware_limit, max_theoretical_active_blocks * num_warps_per_block);

    auto max_active_blocks = max_active_warps / num_warps_per_block;

    feat.max_warp_occupancy = (double)max_active_warps / (double)active_warp_hardware_limit;
    feat.max_block_occupancy = (double)max_active_blocks / (double)active_block_hardware_limit;
}

void LoopNest::compute_shared_mem_occupancy(const Anderson2021Params &params,
                                            const Target &target,
                                            int64_t total_shared_mem_alloc_size,
                                            ScheduleFeatures &feat) const {
    if (!is_gpu_block(target)) {
        return;
    }

    auto shared_mem_limit = get_shared_memory_limit(params);
    auto shared_mem_sm_limit = get_shared_memory_sm_limit(params);
    auto active_block_hardware_limit = get_active_block_hardware_limit(params);

    feat.shared_mem_occupancy = (double)total_shared_mem_alloc_size / (double)shared_mem_limit;
    internal_assert(feat.shared_mem_occupancy <= 1) << "Invalid shared mem occupancy: " << feat.shared_mem_occupancy;

    if (total_shared_mem_alloc_size > 0) {
        auto shared_mem_max_active_blocks = std::min(active_block_hardware_limit,
                                                     shared_mem_sm_limit / total_shared_mem_alloc_size);
        feat.shared_mem_block_limit_factor = (double)shared_mem_max_active_blocks / (double)active_block_hardware_limit;

        internal_assert(feat.shared_mem_block_limit_factor <= 1)
            << "Invalid shared mem block limit factor: " << feat.shared_mem_block_limit_factor;
    }
}

std::pair<const LoopNest *, const LoopNest *> LoopNest::find_innermost_and_parent() const {
    internal_assert(!innermost);

    const LoopNest *parent = this;
    const LoopNest *child = nullptr;

    while (true) {
        for (const auto &c : parent->children) {
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

int64_t LoopNest::points_accessed_per_thread(
    const Anderson2021Params &params,
    const Target &target,
    const GPULoopInfo &gpu_loop_info,
    const std::vector<const FunctionDAG::Edge *> &edge_chain,
    const LoadJacobian &jac,
    const LoopNest *parent,
    const LoopNest *grandparent,
    int64_t n,
    const ScheduleFeatures &feat,
    const LoadJacobian &serial_jac,
    bool producer_has_been_scheduled,
    int producer_innermost_dim,
    const GPUMemoryType &mem_type,
    bool verbose) const {

    std::unique_ptr<LoopNest> innermost_parent_clone = std::make_unique<LoopNest>();
    innermost_parent_clone->copy_from(*parent);
    int64_t unrolled_loop_extent = feat.unrolled_loop_extent;
    vector<int64_t> tiling(node->dimensions, 1);
    vector<int> rvars_to_move_inward(parent->size.size(), 0);

    // There are 3 cases to consider when computing the number of unique points
    // accessed:
    // 1. If LICM can be applied, then accessed points can be reused across
    // the loop's iterations so its extents are not counted
    // 2. If LICM cannot be applied to a loop but it is unrolled, then accessed
    // points can potentially be reused across the unrolled block and the number
    // of unique points accessed is equal to the region_required
    // 3. If LICM cannot be applied to a loop and it is not unrolled, then
    // points accessed cannot be reused across iterations and the number of
    // unique points accessed in 2. is multiplied by the loop's extents

    int64_t product_of_non_licm_non_unrolled_extents = 1;
    int64_t product_of_non_licm_extents = 1;
    int num_pure_loops = 0;
    const FunctionDAG::Node *producer = edge_chain.back()->producer;
    for (size_t idx = 0; idx < parent->size.size(); idx++) {
        bool can_apply_licm = true;
        for (int i = 0; i < producer->dimensions; i++) {
            if (!(jac(i, idx) == 0)) {
                can_apply_licm = false;
                break;
            }
        }

        bool pure = stage->loop[idx].pure;
        bool pure_and_unrolled = pure && unrolled_loop_extent > 1;

        if (pure) {
            ++num_pure_loops;
        }

        if (!can_apply_licm) {
            product_of_non_licm_extents *= parent->size[idx];
            if (pure_and_unrolled) {
                // Case 2
                if (stage->loop[idx].pure_dim >= 0) {
                    tiling[stage->loop[idx].pure_dim] = parent->size[idx];
                } else {
                    rvars_to_move_inward[idx] = 1;
                }
                if (verbose) {
                    aslog(2) << "loop idx = " << idx << ": non_licm_unrolled = " << parent->size[idx] << "\n";
                }
            } else {
                // Case 3
                product_of_non_licm_non_unrolled_extents *= parent->size[idx];
                if (verbose) {
                    aslog(2) << "loop idx = " << idx << ": non_licm_non_unrolled = " << parent->size[idx] << "\n";
                }
            }
        } else if (verbose) {
            // Case 1
            aslog(2) << "loop idx = " << idx << ": apply licm = " << parent->size[idx] << "\n";
        }
    }

    IntrusivePtr<const LoopNest> innermost_parent = innermost_parent_clone->parallelize_in_tiles(
        tiling,
        grandparent,
        params,
        target,
        true,
        false,
        false,
        rvars_to_move_inward);

    const auto &bounds = innermost_parent->get_bounds_along_edge_chain(producer, edge_chain);
    int64_t num_points = 1;
    for (int i = 0; i < producer->dimensions; i++) {
        num_points *= bounds->region_required(i).extent();

        // If the min is >= 100000, there's a good chance that the bounds are
        // uninitialized, indicating a bug
        internal_assert(std::abs(bounds->region_required(i).min()) < 100000)
            << "region_required min = " << std::abs(bounds->region_required(i).min())
            << "; region_required max = " << std::abs(bounds->region_required(i).max());
        if (verbose) {
            aslog(2) << "region_required(" << i << ") = " << bounds->region_required(i).extent() << "; ";
        }
    }

    // There are 2 ways to calculate the number of points accessed:
    // 1. The region_required of the producer in the non-LICM unrolled loops * the loop extents of the non-LICM loops
    // that cannot be unrolled
    int64_t points_accessed_by_region_required = num_points * product_of_non_licm_non_unrolled_extents;

    // 2. The number of points computed according to 'n' (the number of
    // entries in the LoadJacobian i.e. the number of loads, ignoring any reuse
    // of points) * the loops extents of all the non-LICM loops. This value is
    // an upper bound
    int64_t points_accessed_by_loop_extents = n * product_of_non_licm_extents;

    // In some cases, the region_required is larger than the actual number of
    // points that need to be loaded e.g. if f(x) = g(x) + g(x + 100), the
    // region_required of g will be the range [x, x + 100] but really only 2
    // points need to be loaded. In cases like this, option 1. will
    // over-estimate and we instead use the upper bound from option 2.
    int64_t points_accessed = points_accessed_by_region_required;
    if (points_accessed_by_loop_extents <= points_accessed_by_region_required) {
        points_accessed = points_accessed_by_loop_extents;

        if (mem_type == GPUMemoryType::Shared) {
            int vector_size = parent->vectorized_load_access_size(serial_jac,
                                                                  producer,
                                                                  producer_has_been_scheduled,
                                                                  producer_innermost_dim,
                                                                  mem_type,
                                                                  verbose);

            if (verbose) {
                aslog(2) << "\n";
                aslog(2) << "vector_size = " << vector_size << "\n";
            }

            if (points_accessed % vector_size == 0) {
                points_accessed /= vector_size;
                if (verbose) {
                    aslog(2) << "vectorization applied\n";
                }
            }
        }
    }

    points_accessed *= gpu_loop_info.total_outer_serial_extents;

    int64_t total_inner_serial_extents_outside_realization =
        gpu_loop_info.get_total_inner_serial_extents_outside_realization(this);

    // If you have a realization inside a serial loop e.g.
    // f 80 gpu_block
    //  f 32 gpu_thread
    //   f 8 gpu_serial
    //    realize: g
    //    g 1 gpu_serial
    //     g 1 gpu_simd
    //    f 1 gpu_simd
    // LICM won't be able to hoist g's loads/stores above its realization level
    // so 'f 8' will contribute a factor of 8 to the total
    points_accessed *= total_inner_serial_extents_outside_realization;

    if (verbose) {
        aslog(2) << "\n";
        aslog(2) << "region_required = " << num_points << "\n";
        aslog(2) << "total_inner_serial_extents = " << gpu_loop_info.total_inner_serial_extents << "\n";
        aslog(2) << "total_outer_serial_extents = " << gpu_loop_info.total_outer_serial_extents << "\n";
        aslog(2) << "total_inner_serial_extents_outside_realization = " << total_inner_serial_extents_outside_realization << "\n";
        aslog(2) << "product_of_non_licm_non_unrolled_extents = " << product_of_non_licm_non_unrolled_extents << "\n";
        aslog(2) << "n = " << n << "\n";
        aslog(2) << "points_accessed_by_region_required = " << points_accessed_by_region_required << "\n";
        aslog(2) << "points_accessed_by_loop_extents = " << points_accessed_by_loop_extents << "\n";
        aslog(2) << "final points_accessed_per_thread = " << points_accessed << "\n";
    }

    return points_accessed;
}

int64_t LoopNest::compute_licm_amortization(const LoopNest *innermost,
                                            const LoopNest *parent,
                                            const ScheduleFeatures &feat,
                                            const LoadJacobian &jac,
                                            int producer_dims) const {
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

void LoopNest::memoize_points_computed_minimum(StageMap<ScheduleFeatures> &memoized_features,
                                               const StageMap<ScheduleFeatures> *features) const {
    for (auto it = inlined.begin(); it != inlined.end(); it++) {
        const auto *f = it.key();
        const auto &inlined_feat = features->get(&(f->stages[0]));
        memoized_features.get(&(f->stages[0])).points_computed_minimum = inlined_feat.points_computed_minimum;
    }

    memoized_features.get(stage).points_computed_minimum = features->get(stage).points_computed_minimum;

    for (const auto &c : children) {
        c->memoize_points_computed_minimum(memoized_features, features);
    }
}

vector<pair<int, int>> LoopNest::collect_producers(const StageMap<Sites> &sites) const {
    set<const FunctionDAG::Node::Stage *> stages;
    collect_stages(stages);

    vector<const FunctionDAG::Edge *> pending;

    for (const auto *stage : stages) {
        for (const auto *e : stage->incoming_edges) {
            pending.push_back(e);
        }
    }

    set<const FunctionDAG::Node *> done;
    vector<pair<int, int>> producers;

    // Collect all producers of the funcs within this LoopNest
    while (!pending.empty()) {
        const auto *e = pending.back();
        pending.pop_back();
        if (done.count(e->producer)) {
            continue;
        }
        done.insert(e->producer);
        const auto &site = sites.get(&(e->producer->stages[0]));
        if (site.store->is_root()) {
            int vector_dim = (e->producer->is_input ? 0 : (site.produce != nullptr ? site.produce->vector_dim : -1));
            producers.emplace_back(e->producer->id, vector_dim);
        } else if (site.produce != nullptr) {
            // Computation must be nested inside this task or inlined into it.
            for (const auto &s : e->producer->stages) {
                for (const auto *e2 : s.incoming_edges) {
                    pending.push_back(e2);
                }
            }
        }
    }

    return producers;
}

uint64_t LoopNest::compute_hash_of_producers_stored_at_root(const StageMap<Sites> &sites) const {
    vector<pair<int, int>> producers = collect_producers(sites);

    // Sort them according to node id
    std::sort(producers.begin(), producers.end(),
              [](const pair<int, int> &a, const pair<int, int> &b) { return a.first < b.first; });

    uint64_t store_root_hash = 0;
    for (const auto &p : producers) {
        hash_combine(store_root_hash, p.first);
        hash_combine(store_root_hash, p.second);
    }

    return store_root_hash;
}

void LoopNest::collect_stages(std::set<const FunctionDAG::Node::Stage *> &stages) const {
    stages.insert(stage);

    for (const auto &c : children) {
        c->collect_stages(stages);
    }
}

void LoopNest::memoize_features(StageMap<ScheduleFeatures> &memoized_features,
                                const StageMap<ScheduleFeatures> *features) const {
    for (auto it = inlined.begin(); it != inlined.end(); it++) {
        const auto *f = it.key();
        if (memoized_features.contains(&(f->stages[0]))) {
            continue;
        }

        const auto &inlined_feat = features->get(&(f->stages[0]));
        memoized_features.insert(&(f->stages[0]), inlined_feat);
    }

    if (!memoized_features.contains(stage)) {
        memoized_features.insert(stage, features->get(stage));
    }

    for (const auto &c : children) {
        c->memoize_features(memoized_features, features);
    }
}

void LoopNest::compute_working_set_from_features(int64_t *working_set,
                                                 const StageMap<ScheduleFeatures> *features) const {
    int64_t working_set_here = 0;

    for (const auto &c : children) {
        c->compute_working_set_from_features(&working_set_here, features);
    }

    for (const auto *node : store_at) {
        const auto &feat = features->get(&(node->stages[0]));
        working_set_here += feat.bytes_at_production;
    }

    *working_set += working_set_here;
}

void LoopNest::recompute_inlined_features(const StageMap<Sites> &sites,
                                          StageMap<ScheduleFeatures> *features) const {
    for (const auto &c : children) {
        c->recompute_inlined_features(sites, features);
    }

    for (auto it = inlined.begin(); it != inlined.end(); it++) {
        const auto *f = it.key();
        internal_assert(f);

        const auto &block = sites.get(stage).task;

        internal_assert(sites.contains(block->stage));
        uint64_t hash_of_producers = sites.get(block->stage).hash_of_producers_stored_at_root;

        internal_assert(block->feature_intermediates.count(hash_of_producers) > 0);
        auto &intermediate_map = block->feature_intermediates[hash_of_producers].get(&(f->stages[0]));
        auto &intermediate = intermediate_map.get(stage);

        auto &inlined_feat = features->get(&(f->stages[0]));
        inlined_feat.inlined_calls += intermediate.inlined_calls;
        inlined_feat.num_scalars += intermediate.num_scalars;
        if (inlined_feat.innermost_pure_loop_extent > 0) {
            inlined_feat.innermost_pure_loop_extent = std::min(inlined_feat.innermost_pure_loop_extent,
                                                               intermediate.innermost_pure_loop_extent);
        } else {
            inlined_feat.innermost_pure_loop_extent = intermediate.innermost_pure_loop_extent;
        }
        inlined_feat.outer_parallelism = intermediate.outer_parallelism;
        inlined_feat.num_blocks = intermediate.outer_parallelism;
        inlined_feat.num_warps_per_block += intermediate.num_warps_per_block;

        inlined_feat.num_threads_per_block += intermediate.num_threads_per_block;
        inlined_feat.points_computed_per_thread += intermediate.points_computed_per_thread;
    }
}

std::pair<int64_t, bool> LoopNest::compute_alloc_size_of_node_here(const FunctionDAG::Node *f) const {
    const auto &bounds = get_bounds(f);

    int64_t bytes = f->bytes_per_point;
    bool is_constant = true;
    for (int i = 0; i < f->dimensions; i++) {
        const auto &p = bounds->region_computed(i);
        bytes *= p.extent();
        is_constant = is_constant && p.constant_extent();
    }

    return {bytes, is_constant};
}

// Do a recursive walk over the loop nest computing features to feed the cost model.
void LoopNest::compute_features(const FunctionDAG &dag,
                                const Anderson2021Params &params,
                                const Target &target,
                                const StageMap<Sites> &sites,
                                int64_t instances,
                                int64_t parallelism,
                                const LoopNest *parent,
                                const LoopNest *grandparent,
                                const LoopNest &root,
                                GPULoopInfo gpu_loop_info,
                                bool use_memoized_features,
                                const StageMap<int64_t> &total_shared_mem_alloc_sizes,
                                int64_t *working_set,
                                int64_t *working_set_local_constant,
                                int64_t *working_set_local_dynamic,
                                StageMap<ScheduleFeatures> *features,
                                Statistics &stats,
                                bool verbose) const {

    gpu_loop_info.update(target, this);

    if (is_gpu_thread(target)) {
        (void)gpu_loop_info.create_thread_info();
    }

    int64_t working_set_here = 0;
    int64_t working_set_here_local_constant = 0;
    int64_t working_set_here_local_dynamic = 0;

    int64_t loop_instances = 1, parallel_tasks = 1;
    bool in_impure = false;
    for (int idx = (int)size.size() - 1; idx >= 0; idx--) {
        size_t i = size[idx];
        loop_instances *= i;
        if (stage->loop[idx].pure && !in_impure) {
            if (params.parallelism > 1 && (parallel || (parent->is_root() && parallel_tasks < params.parallelism))) {
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
            feat.num_scalars = subinstances;
            for (int i = 0; i < (int)node->stages[s].loop.size(); i++) {
                const auto &p = bounds->loops(s, i);
                int64_t extent = p.extent();
                feat.points_computed_per_realization *= extent;
                if (i == sites.get(&(node->stages[s])).produce->vectorized_loop_index) {
                    // Assumes that we're not going to split
                    // things such that non-native-width
                    // vectorization is a problem, except for the
                    // tail.
                    feat.num_scalars *= extent % node->stages[s].vector_size;
                } else {
                    feat.num_scalars *= extent;
                }
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
                const auto &site = sites.get(&(node->stages[0]));
                if (site.is_stored_in_global_mem()) {
                    feat.global_bytes_at_task = feat.bytes_at_realization;
                    feat.global_innermost_bytes_at_task = feat.innermost_bytes_at_realization;
                } else if (site.is_stored_in_shared_mem()) {
                    feat.shared_bytes_at_task = feat.bytes_at_realization;
                    feat.shared_innermost_bytes_at_task = feat.innermost_bytes_at_realization;
                } else if (site.is_stored_in_local_mem()) {
                    // feat.local_bytes_at_task = feat.bytes_at_realization;
                    // feat.local_innermost_bytes_at_task = feat.innermost_bytes_at_realization;
                } else if (site.is_stored_in_registers()) {
                    feat.register_bytes_at_task = feat.bytes_at_realization;
                    feat.register_innermost_bytes_at_task = feat.innermost_bytes_at_realization;
                } else {
                    internal_assert(false);
                }
            }
        }
    }

    if (is_root()) {
        // TODO: This block of code is repeated below. Refactor
        for (const auto &c : children) {
            uint64_t hash_of_producers = sites.get(c->stage).hash_of_producers_stored_at_root;
            if (use_memoized_features) {
                if (c->features.count(hash_of_producers) > 0) {
                    ++stats.num_memoization_hits;

                    const auto &entry = c->features.at(hash_of_producers);
                    for (auto it = entry.begin(); it != entry.end(); it++) {
                        const auto &stage = *(it.key());
                        const auto &feat = it.value();

                        features->insert(&stage, feat);
                    }

                    // 'working_set_here' is required below for computing the
                    // root-level features so we compute the value that it
                    // would have had if the current loop nest had not been
                    // memoized
                    int64_t working_set_c{0};
                    c->compute_working_set_from_features(&working_set_c, features);
                    working_set_here += working_set_c;
                    continue;
                }

                ++stats.num_memoization_misses;
            }

            c->compute_features(dag,
                                params,
                                target,
                                sites,
                                subinstances,
                                parallelism,
                                this,
                                parent,
                                root,
                                gpu_loop_info,
                                use_memoized_features,
                                total_shared_mem_alloc_sizes,
                                &working_set_here,
                                &working_set_here_local_constant,
                                &working_set_here_local_dynamic,
                                features,
                                stats,
                                verbose);

            if (use_memoized_features) {
                c->features[hash_of_producers].make_large(dag.nodes[0].stages[0].max_id);
                c->memoize_features(c->features[hash_of_producers], features);
            }
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

            const auto *p = sites.get(stage).produce;
            if (p) {
                // Extent of the innermost dimension in the storage layout
                int64_t innermost_storage_extent = 1;
                int v = p->vector_dim;
                if (v >= 0 && v < node->dimensions) {
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
                feat.points_computed_minimum = std::min(feat.points_computed_minimum,
                                                        (double)points_computed_minimum_if_inlined);
            }

            // When memoizing, we need to recompute features for inlined Funcs
            // so we reset them here
            if (use_memoized_features && sites.get(stage).inlined) {
                feat.inlined_calls = 0;
                feat.num_scalars = 0;
                feat.innermost_pure_loop_extent = 0;
                feat.outer_parallelism = 0;
                feat.num_warps_per_block = 0;
                feat.num_threads_per_block = 0;
                feat.points_computed_per_thread = 0;
            }
        }

        if (use_memoized_features) {
            for (const auto &c : children) {
                uint64_t hash_of_producers = sites.get(c->stage).hash_of_producers_stored_at_root;

                // When computing feat.points_computed_minimum above, the order
                // of nodes considered is possibly different from the loop nest
                // traversal order so 'features->get(e->consumer).points_computed_minimum'
                // may not have been computed when it is accessed as a memoized
                // feature. We memoize 'points_computed_minimum' here to ensure
                // its value is always available
                if (c->features.count(hash_of_producers) > 0) {
                    c->memoize_points_computed_minimum(c->features[hash_of_producers], features);
                }
            }

            recompute_inlined_features(sites, features);
        }

        return;
    }

    int64_t subparallelism = parallel_tasks * parallelism;

    // Figure out the features at the compute_at level
    internal_assert(!stage->node->is_input);
    ScheduleFeatures &feat = features->get_or_create(stage);

    if (!innermost) {
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
        double bytes_at_task = 0;
        double innermost_bytes_at_task = 0;
        if (parallel) {
            const auto &bounds = get_bounds(node);
            bytes_at_task = node->bytes_per_point;
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
                bytes_at_task *= extent;
                if (i == vector_dim) {
                    innermost_storage_extent = extent;
                }
            }
            innermost_bytes_at_task = node->bytes_per_point * innermost_storage_extent;
        } else {
            // How this loop will be parallelized is not yet
            // determined. Use optimistic values for the features.
            bytes_at_task = (feat.bytes_at_realization + params.parallelism - 1) / params.parallelism;
            innermost_bytes_at_task = std::min(bytes_at_task, feat.innermost_bytes_at_realization);
        }

        const auto &site = sites.get(&node->stages[0]);
        if (site.is_stored_in_global_mem()) {
            feat.global_bytes_at_task = bytes_at_task;
            feat.global_innermost_bytes_at_task = innermost_bytes_at_task;
        } else if (site.is_stored_in_shared_mem()) {
            feat.shared_bytes_at_task = bytes_at_task;
            feat.shared_innermost_bytes_at_task = innermost_bytes_at_task;
        } else if (site.is_stored_in_local_mem()) {
            // feat.local_bytes_at_task = bytes_at_task;
            // feat.local_innermost_bytes_at_task = innermost_bytes_at_task;
        } else {
            internal_assert(false);
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
            if (done.count(e->producer)) {
                continue;
            }
            done.insert(e->producer);
            const auto &site = sites.get(&(e->producer->stages[0]));
            if (site.store->is_root()) {
                const auto &b = get_bounds(e->producer);
                int64_t bytes = e->producer->bytes_per_point, lines = 1;
                int64_t max_extent = 1;
                // clang-format off
                int vector_dim = (e->producer->is_input   ? 0 :
                                  site.produce != nullptr ? site.produce->vector_dim :
                                                            -1);
                // clang-format on
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
        c->compute_features(dag,
                            params,
                            target,
                            sites,
                            subinstances,
                            subparallelism,
                            this,
                            parent,
                            root,
                            gpu_loop_info,
                            use_memoized_features,
                            total_shared_mem_alloc_sizes,
                            &working_set_here,
                            &working_set_here_local_constant,
                            &working_set_here_local_dynamic,
                            features,
                            stats,
                            verbose);
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
        bool parent_unrolled = (feat.innermost_pure_loop_extent <= get_unroll_limit(target) && parent->node == node);

        if (parent_unrolled) {
            parent_unrolled = all(unrolled_loops(target, parent, grandparent));
        }

        if (parent_unrolled) {
            feat.unrolled_loop_extent = feat.innermost_pure_loop_extent;
        } else {
            feat.unrolled_loop_extent = 1;
        }

        ExprBranching branching{inlined};
        feat.expr_branching = branching.compute(node->func);
    }

    *working_set += working_set_here;
    *working_set_local_constant += working_set_here_local_constant;
    *working_set_local_dynamic += working_set_here_local_dynamic;

    // Analyze all memory dependencies of this stage, looking
    // through any Funcs inlined into it. This is where we track
    // things like vector gathers.
    int64_t global_bytes_loaded = 0, shared_bytes_loaded = 0, local_bytes_loaded = 0, register_bytes_loaded = 0;
    int64_t global_lines_loaded = 0, shared_lines_loaded = 0, local_lines_loaded = 0, register_lines_loaded = 0;
    int64_t global_bytes_loaded_per_thread = 0, shared_bytes_loaded_per_thread = 0, register_bytes_loaded_per_thread = 0;
    int64_t global_lines_loaded_per_thread = 0, shared_lines_loaded_per_thread = 0, register_lines_loaded_per_thread = 0;
    int64_t global_allocation_bytes_loaded = 0, shared_allocation_bytes_loaded = 0;
    GlobalMemInfo global_mem_loads;
    SharedMemInfo shared_mem_loads;
    LocalMemInfo local_mem_loads;

    if (innermost || at_production) {  // These are the sites at which we compute load footprints
        // Pick the site at which we will compute the footprint relationship
        const auto &consumer_site = sites.get(stage);

        // The store_at location of the consumer
        const auto *consumer_store_site = innermost ? parent : consumer_site.store;

        bool inner_serial_loop_extents_computed = false;
        std::vector<int64_t> inner_serial_loop_extents;

        if (innermost && !stage->store_jacobian->empty()) {
            const auto &bounds = consumer_site.store->get_bounds(stage->node);
            inner_serial_loop_extents = gpu_loop_info.get_inner_serial_loop_extents(this);
            inner_serial_loop_extents_computed = true;
            auto store_jac = *stage->store_jacobian;

            compute_gpu_store_features(store_jac,
                                       vector_dim,
                                       stage->node,
                                       bounds,
                                       gpu_loop_info,
                                       inner_serial_loop_extents,
                                       consumer_site,
                                       feat,
                                       parent,
                                       root,
                                       global_mem_loads,
                                       shared_mem_loads,
                                       local_mem_loads,
                                       verbose);
        }

        // The parallel loop of the consumer
        const auto *consumer_task_site = consumer_site.task;

        int64_t consumer_instances = innermost ? instances : feat.num_realizations;
        internal_assert(consumer_instances != 0);

        vector<pair<const FunctionDAG::Node::Stage *, vector<const FunctionDAG::Edge *>>> pending;
        vector<const FunctionDAG::Edge *> edge_chain;
        pending.emplace_back(stage, edge_chain);
        vector<pair<LoadJacobian, FunctionDAG::Node *>> jacobians;
        vector<pair<LoadJacobian, FunctionDAG::Node *>> thread_jacobians;
        set<const FunctionDAG::Node *> done;

        while (!pending.empty()) {
            auto p_pair = pending.back();
            pending.pop_back();

            const auto *p = p_pair.first;

            const auto &next_edges = p->incoming_edges;
            for (const auto *e : next_edges) {
                internal_assert(sites.contains(&(e->producer->stages[0])))
                    << "No site found for " << e->producer->func.name() << "\n";

                const auto &site = sites.get(&(e->producer->stages[0]));

                bool producer_has_been_scheduled = e->producer->is_input || (site.produce != nullptr);

                std::vector<const FunctionDAG::Edge *> edge_chain = p_pair.second;
                edge_chain.push_back(e);

                if (innermost) {
                    if (e->consumer == stage) {
                        for (const auto &j : e->load_jacobians) {
                            jacobians.emplace_back(j, e->producer);

                            if (!inner_serial_loop_extents_computed && !is_scalar()) {
                                inner_serial_loop_extents = gpu_loop_info.get_inner_serial_loop_extents(this);
                                inner_serial_loop_extents_computed = true;
                            }

                            // Thread loops may not be innermost so in the
                            // Jacobians we need to account for the stride
                            // of the inner loops (but only for non-scalars,
                            // since scalars never have inner serial loops)
                            thread_jacobians.emplace_back(is_scalar() ? j : j * inner_serial_loop_extents, e->producer);
                        }
                    } else {
                        // Consumer was inlined. Multiply the Jacobians to look through it.
                        decltype(jacobians) new_jacobians;
                        for (auto &j1 : jacobians) {
                            if (e->consumer->node == j1.second) {
                                for (const auto &j2 : e->load_jacobians) {
                                    LoadJacobian j = j2 * j1.first;
                                    new_jacobians.emplace_back(j, e->producer);
                                }
                            }

                            new_jacobians.emplace_back(std::move(j1));
                        }
                        jacobians.swap(new_jacobians);

                        // Consumer was inlined. Concat the jacobians to look through it.
                        decltype(jacobians) new_thread_jacobians;
                        for (auto &j1 : thread_jacobians) {
                            if (e->consumer->node == j1.second) {
                                for (const auto &j2 : e->load_jacobians) {
                                    LoadJacobian j = j2 * j1.first;
                                    new_thread_jacobians.emplace_back(j, e->producer);
                                }
                            }

                            new_thread_jacobians.emplace_back(std::move(j1));
                        }
                        thread_jacobians.swap(new_thread_jacobians);
                    }
                }

                if (site.inlined) {
                    // Recursively examine the inputs
                    pending.emplace_back(&(e->producer->stages[0]), edge_chain);
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
                int64_t thread_footprint = footprint;
                int64_t compute_footprint = footprint;
                int64_t store_footprint = footprint;
                int64_t line_footprint = 1;
                int64_t thread_line_footprint = 1;
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
                    int producer_innermost_dim =
                        (e->producer->is_input ? 0 :  // Assume default storage layout for inputs
                             !producer_has_been_scheduled ? -1 :
                                                            site.produce->vector_dim);

                    // Shared, global, or local memory?
                    bool is_global_mem = site.gpu_store_memory_type == GPUMemoryType::Global;
                    bool is_shared_mem = site.gpu_store_memory_type == GPUMemoryType::Shared;

                    // Grab the jacobians that describe the memory dependence
                    for (size_t i = 0; i < thread_jacobians.size(); ++i) {
                        const auto &jac = thread_jacobians[i];
                        const auto &serial_jac = jacobians[i];
                        internal_assert(jac.second == serial_jac.second);
                        if (jac.second != e->producer) {
                            continue;
                        }
                        int64_t n = jac.first.count();

                        if (is_shared_mem) {
                            if (verbose) {
                                std::string consumer_name = node->func.name();
                                sanitize_names(consumer_name);
                                std::string producer_name = e->producer->func.name();
                                sanitize_names(producer_name);
                                aslog(2) << "BEGIN MEM ACCESS shared_mem_load. "
                                         << "consumer: " << consumer_name
                                         << "_s" << stage->index
                                         << "; producer: " << producer_name << "\n";
                            }

                            int64_t points_accessed = points_accessed_per_thread(params,
                                                                                 target,
                                                                                 gpu_loop_info,
                                                                                 edge_chain,
                                                                                 jac.first,
                                                                                 parent,
                                                                                 grandparent,
                                                                                 n,
                                                                                 feat,
                                                                                 serial_jac.first,
                                                                                 producer_has_been_scheduled,
                                                                                 producer_innermost_dim,
                                                                                 GPUMemoryType::Shared,
                                                                                 verbose);

                            compute_mem_load_features<SharedMem>(jac.first,
                                                                 producer_innermost_dim,
                                                                 e->producer,
                                                                 producer_store_bounds,
                                                                 producer_has_been_scheduled,
                                                                 gpu_loop_info.get_thread_info(),
                                                                 shared_mem_loads,
                                                                 points_accessed,
                                                                 verbose);

                            if (verbose) {
                                aslog(2) << "num_blocks = " << gpu_loop_info.num_blocks << "\n";
                                aslog(2) << "END MEM ACCESS shared_mem_load. consumer: " << node->func.name()
                                         << "; producer: " << e->producer->func.name();
                                if (!jac.first.all_coeffs_exist()) {
                                    aslog(1) << " (not all coeffs exist)";
                                }
                                aslog(2) << "\n\n";
                            }

                        } else if (is_global_mem) {

                            if (verbose) {
                                std::string consumer_name = node->func.name();
                                sanitize_names(consumer_name);
                                std::string producer_name = e->producer->func.name();
                                sanitize_names(producer_name);
                                aslog(2) << "BEGIN MEM ACCESS global_mem_load. consumer: " << consumer_name << "_s"
                                         << stage->index << "; producer: " << producer_name << "\n";
                            }

                            int64_t points_accessed = points_accessed_per_thread(params,
                                                                                 target,
                                                                                 gpu_loop_info,
                                                                                 edge_chain,
                                                                                 jac.first,
                                                                                 parent,
                                                                                 grandparent,
                                                                                 n,
                                                                                 feat,
                                                                                 serial_jac.first,
                                                                                 producer_has_been_scheduled,
                                                                                 producer_innermost_dim,
                                                                                 GPUMemoryType::Global,
                                                                                 verbose);

                            compute_mem_load_features<GlobalMem>(jac.first,
                                                                 producer_innermost_dim,
                                                                 e->producer,
                                                                 producer_store_bounds,
                                                                 producer_has_been_scheduled,
                                                                 gpu_loop_info.get_thread_info(),
                                                                 global_mem_loads,
                                                                 points_accessed,
                                                                 verbose);

                            if (verbose) {
                                aslog(2) << "num_blocks = " << gpu_loop_info.num_blocks << "\n";
                                aslog(2) << "END MEM ACCESS global_mem_load. consumer: " << node->func.name()
                                         << "; producer: " << e->producer->func.name();
                                if (!jac.first.all_coeffs_exist()) {
                                    aslog(2) << " (not all coeffs exist)";
                                }
                                aslog(2) << "\n\n";
                            }
                        }
                    }

                    if (site.gpu_store_memory_type == GPUMemoryType::Local) {
                        internal_assert(false) << "Loop nest contains local_mem_load";
                        for (const auto &jac : jacobians) {
                            if (jac.second != e->producer) {
                                continue;
                            }
                            int64_t n = jac.first.count();

                            if (verbose) {
                                std::string consumer_name = node->func.name();
                                sanitize_names(consumer_name);
                                std::string producer_name = e->producer->func.name();
                                sanitize_names(producer_name);
                                aslog(2) << "BEGIN MEM ACCESS local_mem_load. consumer: " << consumer_name << "_s"
                                         << stage->index << "; producer: " << producer_name << "\n";
                            }

                            int64_t points_accessed = points_accessed_per_thread(params,
                                                                                 target,
                                                                                 gpu_loop_info,
                                                                                 edge_chain,
                                                                                 jac.first,
                                                                                 parent,
                                                                                 grandparent,
                                                                                 n,
                                                                                 feat,
                                                                                 jac.first,
                                                                                 producer_has_been_scheduled,
                                                                                 producer_innermost_dim,
                                                                                 GPUMemoryType::Local,
                                                                                 verbose);

                            compute_mem_load_features<LocalMem>(
                                jac.first,
                                producer_innermost_dim,
                                e->producer,
                                producer_store_bounds,
                                producer_has_been_scheduled,
                                gpu_loop_info.get_thread_info(),
                                local_mem_loads,
                                points_accessed,
                                verbose);

                            if (verbose) {
                                aslog(2) << "num_blocks = " << gpu_loop_info.num_blocks << "\n";
                                aslog(2) << "END MEM ACCESS local_mem_load. consumer: " << node->func.name()
                                         << "; producer: " << e->producer->func.name();
                                if (!jac.first.all_coeffs_exist()) {
                                    aslog(2) << " (not all coeffs exist)";
                                }
                                aslog(2) << "\n\n";
                            }
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
                int64_t max_extent = 1, max_thread_extent = 1, max_compute_extent = 1, max_store_extent = 1,
                        max_task_extent = 1;
                for (int i = 0; i < e->producer->dimensions; i++) {
                    auto p = bounds->region_required(i);
                    auto compute_p = producer_compute_bounds->region_computed(i);
                    auto store_p = producer_store_bounds->region_required(i);
                    auto task_p = task_bounds->region_required(i);

                    // Check some invariants
                    internal_assert(store_p.min() <= store_p.max()) << store_p.min() << " " << store_p.max() << "\n";
                    internal_assert(compute_p.min() <= compute_p.max())
                        << compute_p.min() << " " << compute_p.max() << "\n";
                    internal_assert(task_p.min() <= task_p.max()) << task_p.min() << " " << task_p.max() << "\n";

                    int64_t thread_extent = 1;
                    if (innermost) {
                        const auto &thread_bounds = gpu_loop_info.current_thread_loop->get_bounds(e->producer);
                        auto thread_p = thread_bounds->region_required(i);
                        thread_extent = thread_p.extent();
                    }

                    int64_t extent = p.extent();
                    int64_t compute_extent = compute_p.extent();
                    int64_t store_extent = store_p.extent();
                    int64_t task_extent = task_p.extent();

                    max_extent = std::max(extent, max_extent);
                    max_thread_extent = std::max(thread_extent, max_thread_extent);
                    max_compute_extent = std::max(compute_extent, max_compute_extent);
                    max_store_extent = std::max(store_extent, max_store_extent);
                    max_task_extent = std::max(task_extent, max_task_extent);

                    footprint *= extent;
                    thread_footprint *= thread_extent;
                    compute_footprint *= compute_extent;
                    store_footprint *= store_extent;

                    bool dense = ((e->producer->is_input && i == 0) ||
                                  (site.produce != nullptr && i == site.produce->vector_dim));
                    if (!dense) {
                        line_footprint *= extent;
                        thread_line_footprint *= thread_extent;
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
                    thread_line_footprint /= max_thread_extent;
                    compute_line_footprint /= max_compute_extent;
                    store_line_footprint /= max_store_extent;
                    task_line_footprint /= max_task_extent;
                }

                int64_t store_instances_per_consumption = 1;

                if (!e->producer->is_input) {
                    const int64_t producer_store_instances =
                        producer_has_been_scheduled ?
                            features->get_or_create(&(e->producer->stages[0])).num_realizations :
                            site.num_realizations;

                    internal_assert(producer_store_instances > 0);

                    if (producer_store_instances) {
                        // The producer's realization is nested inside this Func's realization
                        if (producer_store_instances > consumer_instances) {
                            store_instances_per_consumption = producer_store_instances / consumer_instances;
                        }
                    }
                }

                if (site.is_stored_in_global_mem()) {
                    global_allocation_bytes_loaded += compute_footprint;
                } else if (site.is_stored_in_shared_mem()) {
                    shared_allocation_bytes_loaded += compute_footprint;
                } else if (site.is_stored_in_local_mem()) {
                } else if (site.is_stored_in_registers()) {
                } else {
                    internal_assert(false);
                }

                if (store_instances_per_consumption > 1) {
                    if (site.is_stored_in_global_mem()) {
                        // The producer is nested inside the consumer
                        global_bytes_loaded += store_footprint;
                        // Due to folding, the actual buffer size is smaller than the bounds at the store level
                        global_lines_loaded += store_line_footprint;

                        global_bytes_loaded_per_thread += store_footprint;
                        global_lines_loaded_per_thread += store_line_footprint;
                    } else if (site.is_stored_in_shared_mem()) {
                        shared_bytes_loaded += store_footprint;
                        shared_lines_loaded += store_line_footprint;

                        shared_bytes_loaded_per_thread += store_footprint;
                        shared_lines_loaded_per_thread += store_line_footprint;
                    } else if (site.is_stored_in_local_mem()) {
                        local_bytes_loaded += store_footprint;
                        local_lines_loaded += store_line_footprint;
                    } else if (site.is_stored_in_registers()) {
                        register_bytes_loaded += store_footprint;
                        register_lines_loaded += store_line_footprint;

                        register_bytes_loaded_per_thread += store_footprint;
                        register_lines_loaded_per_thread += store_line_footprint;
                    } else {
                        internal_assert(false);
                    }

                } else {
                    // The consumer is consuming some portion of a larger producer computed earlier
                    if (site.is_stored_in_global_mem()) {
                        global_bytes_loaded += footprint;
                        global_lines_loaded += line_footprint;

                        global_bytes_loaded_per_thread += thread_footprint;
                        global_lines_loaded_per_thread += thread_line_footprint;
                    } else if (site.is_stored_in_shared_mem()) {
                        shared_bytes_loaded += footprint;
                        shared_lines_loaded += line_footprint;

                        shared_bytes_loaded_per_thread += thread_footprint;
                        shared_lines_loaded_per_thread += thread_line_footprint;
                    } else if (site.is_stored_in_local_mem()) {
                        local_bytes_loaded += footprint;
                        local_lines_loaded += line_footprint;
                    } else if (site.is_stored_in_registers()) {
                        register_bytes_loaded += footprint;
                        register_lines_loaded += line_footprint;

                        if (producer_store_site == gpu_loop_info.current_thread_loop) {
                            register_bytes_loaded_per_thread += thread_footprint;
                            register_lines_loaded_per_thread += thread_line_footprint;
                        } else {
                            internal_assert(producer_store_site->gpu_label == GPU_parallelism::Serial);
                            register_bytes_loaded_per_thread += store_footprint;
                            register_lines_loaded_per_thread += store_line_footprint;
                        }
                    } else {
                        internal_assert(false);
                    }
                }

                // We compute (but never use) these; computing them is cheap,
                // so let's leave in for future reference, but mark as 'ignore me'
                // to avoid clang-tidy warnings.
                (void)compute_line_footprint;
                (void)task_line_footprint;
            }
        }
    }

    if (at_production) {
        // Properties of the realization, but the values are
        // computable at the production site because that's where
        // the consumers are.
        internal_assert(global_bytes_loaded >= 0) << "Negative global bytes loaded: " << global_bytes_loaded << "\n";
        internal_assert(shared_bytes_loaded >= 0) << "Negative shared bytes loaded: " << shared_bytes_loaded << "\n";
        internal_assert(local_bytes_loaded >= 0) << "Negative local bytes loaded: " << local_bytes_loaded << "\n";
        internal_assert(register_bytes_loaded >= 0)
            << "Negative register bytes loaded: " << register_bytes_loaded << "\n";

        feat.global_allocation_bytes_read_per_realization = global_allocation_bytes_loaded;
        feat.shared_allocation_bytes_read_per_realization = shared_allocation_bytes_loaded;

        feat.unique_global_bytes_read_per_realization = global_bytes_loaded;
        feat.unique_shared_bytes_read_per_realization = shared_bytes_loaded;
        feat.unique_register_bytes_read_per_realization = register_bytes_loaded;

        feat.unique_global_lines_read_per_realization = global_lines_loaded;
        feat.unique_shared_lines_read_per_realization = shared_lines_loaded;
        feat.unique_register_lines_read_per_realization = register_lines_loaded;

        if (!at_pure_production) {
            // Also pessimistically assume this update definition relies on the entirety of the produced region so far.
            // TODO: This overbills scatters, or writes to a sub-window.
            internal_assert(feat.bytes_at_production >= 0)
                << "Negative bytes at production: " << feat.bytes_at_production << "\n";

            const auto &consumer_site = sites.get(&node->stages[0]);
            if (consumer_site.is_stored_in_global_mem()) {
                feat.unique_global_bytes_read_per_realization += feat.bytes_at_production;
                feat.unique_global_lines_read_per_realization +=
                    feat.bytes_at_production / feat.innermost_bytes_at_production;
                feat.global_allocation_bytes_read_per_realization += feat.bytes_at_production;
            } else if (consumer_site.is_stored_in_shared_mem()) {
                feat.unique_shared_bytes_read_per_realization += feat.bytes_at_production;
                feat.unique_shared_lines_read_per_realization +=
                    feat.bytes_at_production / feat.innermost_bytes_at_production;
                feat.shared_allocation_bytes_read_per_realization += feat.bytes_at_production;
            } else if (consumer_site.is_stored_in_local_mem()) {
                // feat.unique_local_bytes_read_per_realization += feat.bytes_at_production;
                // feat.unique_local_lines_read_per_realization += feat.bytes_at_production /
                // feat.innermost_bytes_at_production; feat.local_allocation_bytes_read_per_realization +=
                // feat.bytes_at_production;
            } else if (consumer_site.is_stored_in_registers()) {
                feat.unique_register_bytes_read_per_realization += feat.bytes_at_production;
                feat.unique_register_lines_read_per_realization +=
                    feat.bytes_at_production / feat.innermost_bytes_at_production;
                feat.register_allocation_bytes_read_per_realization += feat.bytes_at_production;
            } else {
                internal_assert(false);
            }
        }
    }

    if (innermost) {
        feat.points_computed_per_thread = gpu_loop_info.total_serial_extents();

        feat.unique_global_bytes_read_per_thread = global_bytes_loaded_per_thread;
        feat.unique_shared_bytes_read_per_thread = shared_bytes_loaded_per_thread;
        feat.unique_register_bytes_read_per_thread = register_bytes_loaded_per_thread;

        feat.unique_global_lines_read_per_thread = global_lines_loaded_per_thread;
        feat.unique_shared_lines_read_per_thread = shared_lines_loaded_per_thread;
        feat.unique_register_lines_read_per_thread = register_lines_loaded_per_thread;

        feat.points_computed_per_production = subinstances / feat.num_productions;

        feat.unique_bytes_read_per_point =
            global_bytes_loaded + shared_bytes_loaded + local_bytes_loaded + register_bytes_loaded;
        feat.unique_lines_read_per_point =
            global_lines_loaded + shared_lines_loaded + local_lines_loaded + register_bytes_loaded;

        feat.num_global_mem_loads_per_block = global_mem_loads.num_transactions();
        feat.global_mem_load_efficiency = global_mem_loads.efficiency();

        feat.num_shared_mem_loads_per_block = shared_mem_loads.num_transactions();
        feat.shared_mem_load_efficiency = shared_mem_loads.efficiency();

        internal_assert(in_range_zero_one(feat.global_mem_load_efficiency))
            << "Invalid global mem load efficiency: " << feat.global_mem_load_efficiency;

        internal_assert(in_range_zero_one(feat.shared_mem_load_efficiency))
            << "Invalid shared mem load efficiency: " << feat.shared_mem_load_efficiency;
    }

    // Track features for inlined Funcs
    for (auto it = inlined.begin(); it != inlined.end(); it++) {
        const auto *f = it.key();
        internal_assert(f);
        auto &inlined_feat = features->get_or_create(&(f->stages[0]));
        inlined_feat.inlined_calls += it.value() * subinstances;
        inlined_feat.num_scalars += it.value() * feat.num_scalars;
        if (inlined_feat.innermost_pure_loop_extent > 0) {
            inlined_feat.innermost_pure_loop_extent =
                std::min(inlined_feat.innermost_pure_loop_extent, feat.innermost_pure_loop_extent);
        } else {
            inlined_feat.innermost_pure_loop_extent = feat.innermost_pure_loop_extent;
        }
        inlined_feat.inner_parallelism = 1;
        inlined_feat.outer_parallelism = parallelism;
        inlined_feat.num_blocks = parallelism;

        internal_assert(is_scalar() || gpu_loop_info.get_thread_info());

        auto num_warps_per_block = it.value();
        auto num_threads_per_block = 1;

        // If the func is being inlined into a scalar, then the scalar will not
        // be surrounded by block/thread/serial loops so there's no need to take
        // them into account when computing these features
        if (!is_scalar()) {
            num_warps_per_block *= gpu_loop_info.total_serial_extents() * gpu_loop_info.get_thread_info()->num_warps_per_block * inlined_feat.num_blocks;
            num_threads_per_block = gpu_loop_info.get_thread_info()->num_threads;
        }
        inlined_feat.num_warps_per_block += num_warps_per_block;
        inlined_feat.num_threads_per_block += num_threads_per_block;
        double points_computed_per_thread = it.value() * feat.points_computed_per_thread;
        inlined_feat.points_computed_per_thread += points_computed_per_thread;

        if (use_memoized_features) {
            const auto &block = sites.get(stage).task;
            uint64_t hash_of_producers = sites.get(block->stage).hash_of_producers_stored_at_root;
            auto &intermediate_map = block->feature_intermediates[hash_of_producers].get_or_create(&(f->stages[0]));
            auto &intermediate = intermediate_map.get_or_create(stage);
            intermediate.inlined_calls = it.value() * subinstances;
            intermediate.num_scalars = it.value() * feat.num_scalars;

            intermediate.innermost_pure_loop_extent = feat.innermost_pure_loop_extent;
            intermediate.outer_parallelism = parallelism;
            intermediate.num_warps_per_block = num_warps_per_block;

            intermediate.num_threads_per_block = num_threads_per_block;
            intermediate.points_computed_per_thread = points_computed_per_thread;
        }
    }

    compute_shared_mem_occupancy(params, target, total_shared_mem_alloc_sizes.get(stage), feat);

    if (innermost && !is_scalar()) {
        compute_warp_features(feat, gpu_loop_info);
        compute_warp_and_block_occupancy(params, feat, gpu_loop_info);
    }
}

// Get the region required of a Func at this site (but only to satisfy the
// consumers along the given edge chain), from which we know what region
// would be computed if it were scheduled here and what its loop nest
// would be.
// This is useful for computing load memory features along a particular edge
// e.g. if out(x) = f(x) + g(x)
// and f(x) = g(x - 100) + g(x + 100)
// and g(x) = x
// we want to be able to compute load memory features by 'out' loading from 'g'.
// For this we need the region required of 'g', but it should only include the
// region required by the edge from 'g' -> 'out' and ignore the region required by the
// edge 'g' -> 'f' (which is what get_bounds() would compute i.e. the region
// required of 'g' should be 1 point for each point of 'out' but get_bounds()
// will also include the edge 'g' -> 'f' and give the result 201 points for every point
// of 'out')
Bound LoopNest::get_bounds_along_edge_chain(const FunctionDAG::Node *f,
                                            const vector<const FunctionDAG::Edge *> &edge_chain) const {
    internal_assert(!edge_chain.empty());

    internal_assert(edge_chain[0]->consumer == stage)
        << "get_bounds_along_edge_chain must be called with an edge chain that begins from the current loop nest's "
           "node. But the given edge chain begins with "
        << edge_chain[0]->consumer->node->func.name() << " not " << node->func.name();

    internal_assert(edge_chain.back()->producer == f)
        << "get_bounds_along_edge_chain must be called with an edge chain that ends with the given node. But the given "
           "edge chain ends with "
        << edge_chain.back()->producer->func.name() << " not " << f->func.name();

    vector<Bound> bounds;
    BoundContents *bound;

    // For the final consumer, we rely on get_bounds() (i.e. on the bounds for it to
    // satisfy all of its downstream consumers instead of just along a single edge). This should be
    // okay because it is computed in the current loop nest so its bounds need
    // to account for all its downstream consumers.
    const auto &c_bounds = get_bounds(edge_chain[0]->consumer->node);
    Bound cur_consumer_bounds = c_bounds;

    for (const auto *e : edge_chain) {
        const auto *producer = e->producer;

        bound = producer->make_bound();
        auto init = Span::empty_span();
        for (int i = 0; i < producer->dimensions; i++) {
            bound->region_required(i) = init;
        }

        // Get the concrete sizes of the consuming loop
        const auto *consumer_loop = &(cur_consumer_bounds->loops(e->consumer->index, 0));

        // Use the bounds relationship between the nodes to
        // map from the consumer's loop to the required region
        // of the producer.
        e->expand_footprint(consumer_loop, &(bound->region_required(0)));

        // Given a required region of this producer, use the bounds
        // analysis to figure out what region actually gets
        // computed. For most funcs, these are the same. Some things,
        // like histograms or scans, you can only really compute all
        // of at once.
        producer->required_to_computed(&(bound->region_required(0)), &(bound->region_computed(0)));

        // Finally, figure out what loop nests will be used to compute
        // this region.
        for (int i = 0; i < (int)producer->stages.size(); i++) {
            producer->loop_nest_for_region(i, &(bound->region_computed(0)), &(bound->loops(i, 0)));
        }

        bounds.emplace_back(bound);
        cur_consumer_bounds = bound;
    }

    return bounds.back();
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
    auto *bound = f->make_bound();

    // Compute the region required
    if (f->is_output && is_root()) {
        // It's an output. Use the bounds estimate.
        for (int i = 0; i < f->dimensions; i++) {
            bound->region_required(i) = f->estimated_region_required[i];
        }
    } else {
        internal_assert(!f->outgoing_edges.empty()) << "No consumers of " << f->func.name() << " at loop over "
                                                    << (is_root() ? "root" : node->func.name()) << "\n";
        auto init = Span::empty_span();
        for (int i = 0; i < f->dimensions; i++) {
            bound->region_required(i) = init;
        }

        for (const auto *e : f->outgoing_edges) {
            // Ignore consumers outside of this loop nest
            if (!is_root() && (stage != e->consumer) && (!stage->downstream_of(*(e->consumer->node)))) {
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

void LoopNest::dump() const {
    auto stream = aslog(1);
    dump(stream, "", nullptr);
}

std::string LoopNest::to_string() const {
    std::ostringstream stream;
    dump(stream, "", nullptr);
    return stream.str();
}

// Recursively print a loop nest representation to the given stream
template<typename T>
void LoopNest::dump(T &stream, string prefix, const LoopNest *parent) const {
    if (!is_root()) {
        // Non-root nodes always have parents.
        internal_assert(parent != nullptr);

        stream << prefix << node->func.name();
        prefix += " ";

        for (size_t i = 0; i < size.size(); i++) {
            stream << " " << size[i];
            // The vectorized loop gets a 'v' suffix
            if (innermost && i == (size_t)vectorized_loop_index) {
                stream << "v";
            }
            // Loops that have a known constant size get a
            // 'c'. Useful for knowing what we can unroll.
            if (parent->get_bounds(node)->loops(stage->index, i).constant_extent()) {
                stream << "c";
            }
        }

        // Uncomment when debugging the representative loop bounds selected.
        /*
        const auto &bounds = get_bounds(node);
        for (size_t i = 0; i < size.size(); i++) {
            const auto &p = bounds->loops(stage->index, i);
            stream << " [" << p.first << ", " << p.second << "]";
        }
        */

        stream << " (" << vectorized_loop_index << ", " << vector_dim << ")";
    }

    if (tileable) {
        stream << " t";
    }
    if (innermost) {
        stream << " *";
    }
    if (gpu_label == GPU_parallelism::Block) {
        stream << " gpu_block\n";
    } else if (gpu_label == GPU_parallelism::Serial) {
        stream << " gpu_serial\n";
    } else if (gpu_label == GPU_parallelism::None) {
        stream << " gpu_none\n";
    } else if (gpu_label == GPU_parallelism::Simd) {
        stream << " gpu_simd\n";
    } else if (gpu_label == GPU_parallelism::Thread) {
        stream << " gpu_thread\n";
    } else if (gpu_label == GPU_parallelism::Parallelized) {
        stream << " gpu_parallelized\n";
    } else if (parallel) {
        stream << " p\n";
    } else {
        stream << "\n";
    }
    for (const auto *p : store_at) {
        stream << prefix << "realize: " << p->func.name() << " [";
        for (int i = 0; i < p->dimensions; i++) {
            if (i > 0) {
                stream << ", ";
            }
            const auto &region = get_bounds(p)->region_computed(i);
            stream << region.extent();
            if (region.constant_extent()) {
                stream << "c";
            }
        }
        stream << "] with " << p->stages.size() << " stages\n";
    }
    for (size_t i = children.size(); i > 0; i--) {
        children[i - 1]->dump(stream, prefix, this);
    }
    for (auto it = inlined.begin(); it != inlined.end(); it++) {
        stream << prefix << "inlined: " << it.key()->func.name() << " " << it.value() << "\n";
    }
}

template void LoopNest::dump(aslog &stream, string prefix, const LoopNest *parent) const;

template void LoopNest::dump(std::ostringstream &stream, string prefix, const LoopNest *parent) const;

// Does this loop nest access the given Func
bool LoopNest::calls(const FunctionDAG::Node *f) const {
    for (const auto &c : children) {
        if (c->calls(f)) {
            return true;
        }
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
        if (c->accesses_input_buffer()) {
            return true;
        }
    }
    if (is_root()) {
        return false;
    }

    auto check = [&](const FunctionDAG::Node::Stage *s) {
        for (const auto *e : s->incoming_edges) {
            if (e->producer->is_input) {
                return true;
            }
        }

        for (int t = 0; t < (int)PipelineFeatures::ScalarType::NumScalarTypes; t++) {
            if (s->features.op_histogram[(int)PipelineFeatures::OpType::ImageCall][t] > 0) {
                return true;
            }
        }
        return false;
    };

    if (check(stage)) {
        return true;
    }
    for (auto it = inlined.begin(); it != inlined.end(); it++) {
        if (check(&(it.key()->stages[0]))) {
            return true;
        }
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
        if (c->computes(f)) {
            return true;
        }
    }
    return false;
}

// Above here most methods query the loop nest. Below we have
// methods that mutate the loop nest.

// Inline a Func into all consumers within this loop.
void LoopNest::inline_func(const FunctionDAG::Node *f) {
    // Inline it into the children
    for (auto &i : children) {
        if (i->calls(f)) {
            std::unique_ptr<LoopNest> new_child{new LoopNest};
            new_child->copy_from(*i);
            new_child->inline_func(f);
            i = new_child.release();
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
bool LoopNest::compute_here(const FunctionDAG::Node *f,
                            bool tileable,
                            int v,
                            bool in_threads_loop,
                            const Anderson2021Params &params,
                            const Target &target) {
    const auto &bounds = get_bounds(f);

    if (!may_subtile(params)) {
        // If we are restricting ourselves to the Mullapudi et al
        // scheduling space, then once something is computed here
        // we may not subtile this loop.
        this->tileable = false;
    }

    bool skip_vector_dim = false;

    for (int s = (int)f->stages.size() - 1; s >= 0; s--) {
        LoopNest *node = new LoopNest;
        node->node = f;
        node->stage = &f->stages[s];
        node->innermost = true;
        node->vectorized_loop_index = -1;
        node->tileable = tileable && (is_root() || may_subtile(params));

        // always set gpu_label as thread if legal.
        // if !in_threads_loop we are computing either at root level or inside a serial loop
        // set gpu_label to none, then call parallelize_in_tiles to create a parallel, serial, SIMD loop
        // if compute_root set gpu_label to none, parallelize_in_tiles creates block and thread loops later
        // if computing at serial loop set gpu_label to thread.
        if (target.has_gpu_feature()) {
            if (is_root()) {
                node->gpu_label = GPU_parallelism::None;
            } else if (!in_threads_loop) {
                node->gpu_label = GPU_parallelism::Thread;
            } else {
                node->gpu_label = GPU_parallelism::Serial;
            }
        }
        // Set up a bound for the inside of the
        // loop. computed/required is still the full region, but
        // the loop nest will be a single representative point.
        auto *single_point = bounds->make_copy();
        size_t loop_dim = f->stages[s].loop.size();
        node->size.resize(loop_dim);

        int64_t vector_size = 1;
        bool all_ones = true;
        for (size_t i = 0; i < loop_dim; i++) {
            const auto &l = bounds->loops(s, i);
            // Initialize the loop nest
            node->size[i] = l.extent();

            // Use the first loop iteration to represent the inner
            // loop. We'll shift it to a later one once we decide
            // on vectorization.
            single_point->loops(s, i) = Span(l.min(), l.min(), true);

            internal_assert(l.max() >= l.min()) << i << " " << l.max() << " " << l.min() << "\n";

            if (f->dimensions && node->size[i] >= 1 && f->stages[s].loop[i].var == f->func.args()[v]) {
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

            all_ones = all_ones && node->size[i] == 1;
        }

        // Leave region required blank inside the computation of a Func
        node->set_bounds(f, single_point);
        node->vector_dim = v;

        if (s == 0) {
            skip_vector_dim = !all_ones && node->size[v] == 1;
        }

        // Split off the single vector as an inner loop nest.
        node->innermost = false;

        LoopNest *one_vector = new LoopNest;
        one_vector->node = node->node;
        one_vector->stage = node->stage;
        one_vector->tileable = false;
        one_vector->vectorized_loop_index = node->vectorized_loop_index;
        one_vector->vector_dim = v;
        one_vector->size.resize(loop_dim, 1);
        one_vector->innermost = true;
        one_vector->gpu_label = GPU_parallelism::Simd;
        auto *b = node->get_bounds(f)->make_copy();
        // Set the region computed inside this node to be the first vector lane
        if (node->vectorized_loop_index >= 0) {
            b->loops(s, node->vectorized_loop_index).set_extent(1);
        } else {
            for (size_t i = 0; i < loop_dim; i++) {
                internal_assert(b->loops(s, i).extent() == 1);
            }
        }

        one_vector->set_bounds(f, b);
        if (node->vectorized_loop_index >= 0) {
            one_vector->size[node->vectorized_loop_index] = vector_size;
        }

        node->children.emplace_back(one_vector);

        children.emplace_back(node);
    }

    return skip_vector_dim;
}

// Parallelize this loop according to the given tiling.
IntrusivePtr<const LoopNest> LoopNest::parallelize_in_tiles(const vector<int64_t> &tiling,
                                                            const LoopNest *parent,
                                                            const Anderson2021Params &params,
                                                            const Target &target,
                                                            bool inner_tiling,
                                                            bool adjust_tiling,
                                                            bool move_all_rvars_inward,
                                                            const vector<int> &rvars_to_move_inward) const {
    // Split this loop and move factors to the inner loop
    LoopNest *inner = new LoopNest, *outer = new LoopNest;
    inner->node = outer->node = node;
    inner->stage = outer->stage = stage;
    inner->tileable = outer->tileable = tileable && may_subtile(params);
    inner->vector_dim = outer->vector_dim = vector_dim;
    inner->vectorized_loop_index = outer->vectorized_loop_index = vectorized_loop_index;

    if (target.has_gpu_feature()) {
        if (gpu_label == GPU_parallelism::None) {
            inner->gpu_label = GPU_parallelism::Serial;
            outer->gpu_label = GPU_parallelism::Parallelized;
            outer->parallel = true;
        } else if (gpu_label == GPU_parallelism::Parallelized) {
            inner->gpu_label = GPU_parallelism::Thread;  // compute root funcs always allowed to use GPU threads
            outer->gpu_label = GPU_parallelism::Block;
            outer->parallel = true;
        } else if (gpu_label == GPU_parallelism::Thread) {
            inner->gpu_label = GPU_parallelism::Serial;
            outer->gpu_label = GPU_parallelism::Thread;
            outer->parallel = false;
        } else if (gpu_label == GPU_parallelism::Serial) {
            inner->gpu_label = GPU_parallelism::Serial;
            outer->gpu_label = GPU_parallelism::Serial;
            outer->parallel = false;
        } else {
            internal_error << "invalid gpu label " << stringify(gpu_label) << " for parallelized loop\n";
        }
    }

    outer->size = size;
    outer->innermost = false;

    if (!target.has_gpu_feature()) {
        outer->parallel = true;
    }

    outer->tileable = may_subtile(params);

    // First make an inner loop representing a 1x1x1... tile
    inner->size.resize(size.size(), 1);
    inner->innermost = innermost;
    inner->children = children;
    inner->inlined = inlined;
    inner->bounds = bounds;
    inner->store_at = store_at;

    auto *b = inner->get_bounds(node)->make_copy();

    // Then move factors from the outer loop to the inner loop
    const auto &parent_bounds = parent->get_bounds(node);

    for (size_t i = 0; i < stage->loop.size(); i++) {
        int l = stage->loop[i].pure_dim;

        int64_t outer_extent;
        if (inner_tiling) {
            if (l >= 0) {
                internal_assert(l < (int)tiling.size()) << l << " " << tiling.size() << "\n";
                outer_extent = (outer->size[i] + tiling[l] - 1) / tiling[l];
                inner->size[i] = tiling[l];
            } else if (move_all_rvars_inward || (i < rvars_to_move_inward.size() && rvars_to_move_inward[i])) {
                // RVars are moved inwards
                outer_extent = 1;
                inner->size[i] = outer->size[i];
            } else {
                outer_extent = outer->size[i];
                inner->size[i] = 1;
            }
            if (adjust_tiling) {
                inner->size[i] = (outer->size[i] + outer_extent - 1) / outer_extent;
            }
        } else {
            if (l >= 0) {
                internal_assert(l < (int)tiling.size()) << l << " " << tiling.size() << "\n";
                inner->size[i] = (outer->size[i] + tiling[l] - 1) / tiling[l];
                outer_extent = tiling[l];
            } else if (move_all_rvars_inward || (i < rvars_to_move_inward.size() && rvars_to_move_inward[i])) {
                outer_extent = 1;
                inner->size[i] = outer->size[i];
            } else {
                outer_extent = outer->size[i];
                inner->size[i] = 1;
            }
            if (adjust_tiling) {
                outer_extent = (outer->size[i] + inner->size[i] - 1) / inner->size[i];
            }
        }
        outer->size[i] = outer_extent;
        const auto &p = parent_bounds->loops(stage->index, i);
        int64_t min = p.min();
        int64_t extent = p.extent();
        extent = inner->product_of_self_and_descendants(i);

        // Pick a better representative loop iteration for the
        // inner loops.
        min += (outer_extent / 2) * extent;
        bool compile_time_constant_bounds = p.constant_extent() || stage->loop[i].pure;
        b->loops(stage->index, i) = Span(min, min + extent - 1, compile_time_constant_bounds);
    }
    outer->set_bounds(node, b);

    outer->children.emplace_back(inner);
    return outer;
}

int64_t LoopNest::get_total_local_mem_alloc_size(bool constant_allocs_only, bool in_threads_loop) const {
    int64_t result = 0;

    in_threads_loop = in_threads_loop || gpu_label == GPU_parallelism::Thread;

    if (in_threads_loop) {
        for (const auto *store_node : store_at) {
            const auto &bounds = get_bounds(store_node);

            int64_t alloc_size = store_node->bytes_per_point;
            bool is_constant_alloc = true;
            for (int i = 0; i < store_node->dimensions; i++) {
                const auto &p = bounds->region_computed(i);
                alloc_size *= p.extent();
                is_constant_alloc = is_constant_alloc && p.constant_extent();
            }

            if (store_node->dimensions > 0 && (!constant_allocs_only || is_constant_alloc)) {
                result += alloc_size;
            }
        }
    }

    for (const auto &c : children) {
        result += c->get_total_local_mem_alloc_size(constant_allocs_only, in_threads_loop);
    }

    return result;
}

int64_t LoopNest::get_total_constant_local_mem_alloc_size() const {
    return get_total_local_mem_alloc_size(true);
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

// Is the region_computed smaller here than at its parent?
bool LoopNest::region_computed_shrinks(const FunctionDAG::Node *f, const LoopNest *parent) const {
    const auto &bounds_here = get_bounds(f);
    const auto &bounds_at_parent = parent->get_bounds(f);

    int64_t total_here = 1, total_at_parent = 1;
    for (int i = 0; i < f->dimensions; i++) {
        const auto &range_here = bounds_here->region_computed(i);
        const auto &range_at_parent = bounds_at_parent->region_computed(i);
        total_here *= range_here.extent();
        total_at_parent *= range_at_parent.extent();
    }

    return total_here < total_at_parent;
}

// Return all possible ways to compute f in tiles somewhere within
// this loop nest.
// in_threads_loop tracks whether or not function is going to be placed inside a
// loop marked gpu_threads, in which case f's loops cannot be gpu_threads
vector<IntrusivePtr<const LoopNest>> LoopNest::compute_in_tiles(const FunctionDAG::Node *f,
                                                                const LoopNest *parent,
                                                                const Anderson2021Params &params,
                                                                const Target &target,
                                                                const SearchSpaceOptions &search_space_options,
                                                                int v,
                                                                bool in_realization,
                                                                bool in_threads_loop,
                                                                bool is_pre_pass,
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
        if (ep >= f->vector_size && e < f->vector_size) {
            return result;
        }

        // Don't descend into loops if the bounds required don't
        // shrink.
        if (!region_computed_shrinks(f, parent)) {
            return result;
        }
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

    if (gpu_label == GPU_parallelism::Block) {
        // once we enter a gpu block loop compute union thread counts to pass down
        union_counts = get_union_thread_counts(f);
    }

    bool is_block_level = !is_root() && !in_threads_loop;
    bool can_compute_here = (is_root() && search_space_options.compute_root()) || f->is_output;
    can_compute_here = can_compute_here || (is_block_level && search_space_options.compute_at_block());
    can_compute_here = can_compute_here || (in_threads_loop && search_space_options.compute_at_thread());

    // Place the computation directly inside this loop (provided it's not a SIMD loop)
    if (!innermost && (!in_realization || size.empty() || vector_dim == -1 || size[vector_dim] == 1) &&
        can_compute_here) {

        std::unique_ptr<LoopNest> r{new LoopNest};
        r->copy_from(*this);
        r->compute_here(f, true, v, in_threads_loop, params, target);
        if (!in_realization) {
            r->store_at.insert(f);
        } else {
            r->tileable = false;
        }

        // if GPU and creating a threads loop INSIDE a block loop, create child for each thread tiling
        if (!is_root() && !in_threads_loop && target.has_gpu_feature()) {
            bool made_child = r->add_gpu_thread_tilings(f, params, target, v, result, union_counts);
            if (!made_child) {  // no good thread tilings, just keep r with the untiled loop inserted as serial
                result.emplace_back(r.release());
            }
        } else {  // computing at root or inside a threads loop
            result.emplace_back(r.release());
        }
    }

    bool stop_here = is_root() && !search_space_options.compute_at_block() && !search_space_options.compute_at_thread();
    stop_here = stop_here || (in_threads_loop && !search_space_options.compute_at_thread());
    if (stop_here || f->is_output || is_pre_pass) {
        // Outputs must be compute_root, so we're done.
        return result;
    }

    if (child >= 0 && !called_by_multiple_children && !in_realization && (may_subtile(params) || is_root())) {
        // Push the Func further inwards in the loop nest

        const auto &c = children[child];
        int num_ones = 0;
        for (long s : c->size) {
            num_ones += (s == 1) ? 1 : 0;
        }

        for (int store_here = 0; store_here < 1; store_here++) {
            if (is_root() && num_ones == (int)c->size.size() && params.parallelism > 1) {
                // Don't fuse into serial loops, or we could never parallelize this Func.
                continue;
            }

            in_threads_loop |= (children[child]->gpu_label == GPU_parallelism::Thread);
            // we must pass down union thread count constraints computed at block level when computing further in
            auto opts = children[child]->compute_in_tiles(f,
                                                          this,
                                                          params,
                                                          target,
                                                          search_space_options,
                                                          v,
                                                          store_here,
                                                          in_threads_loop,
                                                          false,
                                                          union_counts);
            for (IntrusivePtr<const LoopNest> &n : opts) {
                // (Only valid if one child calls f) Push the
                // computation into the child. Possibly leaving
                // the storage out here.
                LoopNest *r = new LoopNest;
                r->copy_from(*this);
                r->store_at.insert(f);
                r->children[child] = n;
                result.emplace_back(r);
            }
        }
    }

    return result;
}

int64_t LoopNest::product_of_self_and_descendants(int loop_index) const {
    return size[loop_index] * product_of_descendants(loop_index);
}

int64_t LoopNest::product_of_descendants(int loop_index) const {
    int64_t prod = 1;
    const LoopNest *cur = this;
    while (!cur->innermost) {
        bool found = false;
        for (const auto &c : cur->children) {
            if (c->stage != stage) {
                continue;
            }

            prod *= c->size[loop_index];
            found = true;
            cur = c.get();
            break;
        }

        internal_assert(found);
    }

    return prod;
}

bool LoopNest::has_constant_region_computed(const FunctionDAG::Node *node) const {
    const auto &bounds = get_bounds(node);
    for (int i = 0; i < node->dimensions; i++) {
        if (!bounds->region_computed(i).constant_extent()) {
            return false;
        }
    }
    return true;
}

bool LoopNest::has_constant_region_required(const FunctionDAG::Node *node) const {
    const auto &bounds = get_bounds(node);
    for (int i = 0; i < node->dimensions; i++) {
        if (!bounds->region_required(i).constant_extent()) {
            return false;
        }
    }
    return true;
}

bool LoopNest::other_stage_has_same_producer(const FunctionDAG::Node *producer) const {
    for (const auto &other_stage : node->stages) {
        if (stage->index == other_stage.index) {
            continue;
        }

        for (const auto *e : other_stage.incoming_edges) {
            if (producer == e->producer) {
                return true;
            }
        }
    }
    return false;
}

int LoopNest::num_serial_loops(const FunctionDAG::Node::Stage *stage) const {
    int num_serial_loops = 0;
    for (const auto &child : children) {
        if (child->stage == stage) {
            continue;
        }

        for (auto s : child->size) {
            if (s > 1) {
                ++num_serial_loops;
                break;
            }
        }

        num_serial_loops += child->num_serial_loops(stage);
    }

    return num_serial_loops;
}

int LoopNest::num_serial_loops() const {
    return num_serial_loops(stage);
}

bool LoopNest::producer_computed_here_or_further_in(const FunctionDAG::Node *producer) const {
    for (const auto &child : children) {
        if (child->node == producer) {
            return true;
        }

        if (child->producer_computed_here_or_further_in(producer)) {
            return true;
        }
    }

    return false;
}

void LoopNest::get_stages_computed_in_each_compute_root_loop(StageMap<StageMap<bool>> &descendants,
                                                             const LoopNest *compute_root_loop_nest) const {
    if (is_root()) {
        for (const auto &c : children) {
            descendants.emplace(c->stage, {});
        }

        for (const auto &c : children) {
            c->get_stages_computed_in_each_compute_root_loop(descendants, c.get());
        }

        return;
    }

    descendants.get(compute_root_loop_nest->stage).emplace(stage, true);

    for (const auto &c : children) {
        c->get_stages_computed_in_each_compute_root_loop(descendants, compute_root_loop_nest);
    }
}

// Apply the schedule represented by this loop nest to a Halide pipeline.
void LoopNest::apply(LoopLevel here,
                     StageMap<std::unique_ptr<StageScheduleState>> &state_map,
                     double num_cores,
                     int depth,
                     const LoopNest *parent,
                     const LoopNest *compute_site,
                     const Target &target,
                     std::vector<StageScheduleState *> &ancestors,
                     const NodeMap<bool> &all_inlined) const {
    if (is_root()) {
        for (const auto &c : children) {
            Func(c->node->func).compute_root();
            c->apply(LoopLevel::root(), state_map, num_cores, 1, this, c.get(), target, ancestors, all_inlined);
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
            state->stage = stage;
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
                fv.parallel = l.pure && target.has_gpu_feature() ? gpu_label == GPU_parallelism::Block : parallel;
                fv.exists = true;
                fv.pure = l.pure;
                fv.index = i;
                fv.innermost_pure_dim = (i == (size_t)vectorized_loop_index);
                state->vars.push_back(fv);
            }
            // Bubble the innermost pure dimension to the front of the pure dimensions
            for (int i = vectorized_loop_index - 1; i >= 0 && state->vars[i].pure; i--) {
                std::swap(state->vars[i], state->vars[i + 1]);
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
                // In case the threads loop is innermost
                for (size_t i = 0; i < symbolic_loop.size(); i++) {
                    StageScheduleState::FuncVar &v = state.vars[i];
                    v.gpu_threads = gpu_label == GPU_parallelism::Thread && symbolic_loop[i].pure;
                }

                if (vectorized_loop_index >= 0) {
                    size_t i = 0;
                    while (!state.vars[i].innermost_pure_dim) {
                        i++;
                    }
                    auto &v = state.vars[i];
                    internal_assert(v.innermost_pure_dim && v.exists) << v.var.name() << "\n";
                    // Is the result of a split

                    // The vector size for gpu depends on the width of the
                    // stage's types and will often be 1, in which case we
                    // don't want to vectorize the loop
                    if (!target.has_gpu_feature() || stage->vector_size > 1) {
                        state.schedule_source << "\n    .vectorize(" << v.var.name() << ")";
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

                    parent.gpu_threads = gpu_label == GPU_parallelism::Thread && symbolic_loop[i].pure;

                    int64_t factor = product_of_descendants(parent.index);

                    int64_t innermost_size = innermost_loop->size[parent.index];

                    if (child && innermost_size > factor) {
                        factor = innermost_size;
                    }

                    if (!parent.exists || factor == 1) {
                        v.exists = false;
                        v.extent = 1;
                    } else if (size[parent.index] == 1 && parent.var.is_rvar) {
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
                        // If it's an RVar, or not the outermost split and we're in an update, we need a guard with if
                        // instead.

                        // If the factor evenly divides the parent extent, then
                        // no tail strategy is needed
                        if (parent.var.is_rvar || (stage->index != 0 && !parent.outermost)) {
                            tail_strategy = TailStrategy::GuardWithIf;
                        }

                        if (factor > parent.extent && tail_strategy == TailStrategy::ShiftInwards) {
                            // Don't shift all the way off the image.
                            tail_strategy = TailStrategy::GuardWithIf;
                        }

                        s.split(parent.var, parent.var, inner, (int)factor, tail_strategy);
                        state.schedule_source << "\n    .split(" << parent.var.name() << ", " << parent.var.name()
                                              << ", " << inner.name() << ", " << factor << ", "
                                              << "TailStrategy::" << tail_strategy << ")";
                        v = parent;
                        parent.extent = size[parent.index];
                        v.constant_extent = (!parent.var.is_rvar && parent.exists);
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
                    bool all_loops_are_pure = true;
                    for (size_t i = 0; i < symbolic_loop.size(); i++) {
                        if (state.vars[i].pure) {
                            product_of_pure_loops *= state.vars[i].extent;
                            all_pure_loops_constant_size &= state.vars[i].constant_extent;
                        } else if (state.vars[i].exists) {
                            all_loops_are_pure = false;
                        }
                    }

                    if (product_of_pure_loops <= get_unroll_limit(target) && all_pure_loops_constant_size) {
                        state.all_innermost_unrolled = all_loops_are_pure;
                        // There's a hope we can fit anything compute-at this level into registers if we fully unroll
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
                    if (!v.exists) {
                        continue;
                    }
                    here = LoopLevel(node->func, v.var);
                    found = true;
                    break;
                }
                if (!found) {
                    here = LoopLevel(node->func, Var::outermost());
                }
                // internal_assert(found) << "Could not find appropriate compute_at location for children of " <<
                // node->func.name() << "\n";
                state.vars.insert(state.vars.begin(), new_inner.begin(), new_inner.end());
            }
        }
        if (innermost) {
            internal_assert(store_at.empty());
            internal_assert(children.empty());
            return;
        }

        for (const auto *f : store_at) {
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

        for (const auto &c : children) {
            if (c->node != node) {
                Func(c->node->func).compute_at(here);
            }
            ancestors.push_back(state_map.get(stage).get());
            c->apply(here, state_map, num_cores, depth + 1, this, compute_site, target, ancestors, all_inlined);
            ancestors.pop_back();
            if (c->node != node && c->stage->index == 0) {
                auto &state = *(state_map.get(c->stage));
                state.schedule_source << "\n    .compute" << loop_level;
            }
        }

        if (gpu_label == GPU_parallelism::Thread && state.all_innermost_unrolled && num_serial_loops() <= 1) {
            update_producers_to_be_staged(state, all_inlined);
        }

        for (const auto *f : store_at) {
            bool computed_here = false;
            for (const auto &c : children) {
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

void LoopNest::update_producers_to_be_staged(StageScheduleState &state,
                                             const NodeMap<bool> &all_inlined) const {
    std::vector<pair<const FunctionDAG::Node::Stage *, vector<const FunctionDAG::Edge *>>> pending;
    std::vector<const FunctionDAG::Edge *> edge_chain;
    pending.emplace_back(stage, edge_chain);
    NodeMap<bool> done;

    while (!pending.empty()) {
        auto cur_pair = pending.back();
        pending.pop_back();

        const auto *s = cur_pair.first;

        for (const auto *e : s->incoming_edges) {
            std::vector<const FunctionDAG::Edge *> edge_chain = cur_pair.second;
            edge_chain.push_back(e);

            // If the producer is inlined, then its producers should potentially be
            // staged
            if (all_inlined.contains(e->producer) && all_inlined.get(e->producer)) {
                pending.emplace_back(&e->producer->stages[0], edge_chain);
                continue;
            }

            if (done.contains(e->producer) && done.get(e->producer)) {
                continue;
            }

            done.get_or_create(e->producer) = true;

            if (e->producer->is_input || !has_constant_region_required(e->producer)) {
                continue;
            }

            if (other_stage_has_same_producer(e->producer) || producer_computed_here_or_further_in(e->producer) ||
                !e->all_load_jacobian_coeffs_exist()) {
                continue;
            }

            state.producers_to_be_staged.get_or_create(e->producer).emplace_back(this, edge_chain);
        }
    }
}

double LoopNest::max_idle_lane_wastage(const Target &target, GPULoopInfo gpu_loop_info) const {
    gpu_loop_info.update(target, this);

    if (is_gpu_thread(target)) {
        const ThreadInfo *thread_info = gpu_loop_info.create_thread_info();
        return thread_info->idle_lane_wastage();
    }

    double max_wastage = 0;

    for (const auto &c : children) {
        max_wastage = std::max(max_wastage, c->max_idle_lane_wastage(target, gpu_loop_info));
    }

    return max_wastage;
}

bool LoopNest::has_valid_thread_extents() const {
    for (const auto &c : children) {
        if (!are_valid_thread_extents(c->get_union_thread_counts(nullptr))) {
            return false;
        }
    }

    return true;
}

void LoopNest::collect_nodes_that_should_be_inlined(const NodeMap<bool> &nodes_to_freeze,
                                                    NodeMap<bool> &inlined_nodes) const {
    if (innermost) {
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            const auto *f = it.key();
            if (nodes_to_freeze.contains(f)) {
                inlined_nodes.insert(f, true);
                std::cerr << "Freezing as inlined: " << f->func.name() << "\n";
            }
        }
    }

    for (const auto &c : children) {
        c->collect_nodes_that_should_be_inlined(nodes_to_freeze, inlined_nodes);
    }
}

void LoopNest::collect_all_inlined(NodeMap<bool> &all_inlined) const {
    if (innermost) {
        for (auto it = inlined.begin(); it != inlined.end(); it++) {
            const auto *f = it.key();
            all_inlined.insert(f, true);
        }
    }

    for (const auto &c : children) {
        c->collect_all_inlined(all_inlined);
    }
}

bool Filter::enable_filter_printing() {
    static bool enabled = ([]() -> bool {
        std::string var = get_env_variable("ENABLE_FILTER_PRINTING");
        if (!var.empty()) {
            return var == "1";
        }
        return false;
    })();
    return enabled;
}

}  // namespace Autoscheduler

template<>
RefCount &ref_count<Autoscheduler::LoopNest>(const Autoscheduler::LoopNest *t) noexcept {
    return t->ref_count;
}

template<>
void destroy<Autoscheduler::LoopNest>(const Autoscheduler::LoopNest *t) {
    delete t;
}

}  // namespace Internal
}  // namespace Halide
