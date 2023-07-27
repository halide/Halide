/** This file defines the LoopNest, which is our
 * representation of a Halide schedule, and contains methods to
 * generate candidates for scheduling as well as extract a
 * featurization that can be used to cost each candidate. */

#ifndef LOOP_NEST_H
#define LOOP_NEST_H

#include "ASLog.h"
#include "CostModel.h"
#include "FunctionDAG.h"
#include "GPULoopInfo.h"
#include "GPUMemInfo.h"
#include "PerfectHashMap.h"
#include "SearchSpaceOptions.h"
#include "Statistics.h"
#include "ThreadInfo.h"
#include "Tiling.h"
#include <set>
#include <vector>

namespace Halide {
namespace Internal {
namespace Autoscheduler {

template<typename T>
using NodeMap = PerfectHashMap<FunctionDAG::Node, T>;

template<typename T>
using StageMap = PerfectHashMap<FunctionDAG::Node::Stage, T>;

enum class GPU_parallelism { Block,
                             Thread,
                             Serial,
                             Simd,
                             Parallelized,
                             None };

std::string stringify(GPU_parallelism label);

// inlined => func is inlined so has no memory store location
enum class GPUMemoryType { Global,
                           Shared,
                           Local,
                           Registers,
                           Inlined };

bool may_subtile(const Anderson2021Params &params);

int64_t get_shared_memory_limit(const Anderson2021Params &params);

int64_t get_active_block_hardware_limit(const Anderson2021Params &params);

int64_t get_active_warp_hardware_limit(const Anderson2021Params &params);

constexpr int64_t get_register_mem_alloc_limit() {
    return 128;
}

int get_unroll_limit(const Target &target);

bool in_range_zero_one(double x);

bool are_valid_thread_extents(const vector<int64_t> &counts);

double get_idle_lane_wastage_limit_env_var();
double get_idle_lane_wastage_limit();

bool all(const vector<int> &v);
bool accessed_at_constant_indices(const std::vector<int> &unrolled, const FunctionDAG::Edge *e);

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
    std::set<const FunctionDAG::Node *> store_at;

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

    // Apply gpu threads to this loop nest
    mutable GPU_parallelism gpu_label = GPU_parallelism::None;

    struct FeatureIntermediates {
        double inlined_calls;
        double num_vectors;
        double num_scalars;
        double vector_size;
        double innermost_pure_loop_extent;
        double outer_parallelism;
        double num_warps_per_block;
        double num_threads_per_block;
        double points_computed_per_thread;
    };

    mutable std::map<uint64_t, StageMap<StageMap<FeatureIntermediates>>> feature_intermediates;
    mutable std::map<uint64_t, StageMap<ScheduleFeatures>> features;

    bool is_gpu_serial(const Target &target) const {
        return target.has_gpu_feature() && gpu_label == GPU_parallelism::Serial;
    }

    bool is_gpu_thread(const Target &target) const {
        return target.has_gpu_feature() && gpu_label == GPU_parallelism::Thread;
    }

    bool is_gpu_block(const Target &target) const {
        return target.has_gpu_feature() && gpu_label == GPU_parallelism::Block;
    }

    bool is_scalar() const {
        return size.empty();
    }

    // given a newly inserted node f into this LoopNest, get union of thread counts in each dimension
    // across all siblings of f.
    vector<int64_t> get_union_thread_counts(const FunctionDAG::Node *f) const;

    // given a newly inserted node f into this LoopNest, gets the size of
    // all of f's stages and their pure_dim indices
    void get_stage_sizes(const FunctionDAG::Node *f,
                         vector<vector<int64_t>> &stage_sizes,
                         vector<vector<int>> &pure_dims,
                         vector<int> &vectorized_indices) const;

    // given the loop nest of a stage to parallelize at root, figure out if using odd tile sizes
    // for the vectorized dimension will allow the resulting thread tiles to be multiples of 32
    // if so, we will include these in the serial loop sizes
    void generate_vec_dim_serial_tilings(vector<int> &serial_sizes) const;

    // get the loop nests of a newly inserted node, f, that is marked GPU threads. Tiles
    // the newly inserted loop nests of f into a threads loop outside a serial loop.
    // V is the vectorized dimension of f. Adds loopnests created from each tiling option in result.
    bool add_gpu_thread_tilings(const FunctionDAG::Node *f,
                                const Anderson2021Params &params,
                                const Target &target,
                                int v,
                                vector<IntrusivePtr<const LoopNest>> &result,
                                const vector<int64_t> &max_size);

    void copy_from(const LoopNest &n);
    void copy_from_including_features(const LoopNest &n);

    static void hash_combine(uint64_t &h, uint64_t next) {
        // From boost
        h ^= (next + 0x9e3779b9 + (h << 6) + (h >> 2));
    }

    // Hash the loop structure and sizes up to a fixed depth. This is
    // used as the hash function for the coarse-to-fine beam search in
    // the paper.
    void structural_hash(uint64_t &h, int depth) const;

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
        const LoopNest *compute = nullptr;                 // Its containing compute_at site
        const LoopNest *store = nullptr;                   // Its containing store_at site
        const LoopNest *produce = nullptr;                 // Its own outermost node
        const LoopNest *innermost = nullptr;               // Its innermost node - usually a SIMD loop
        const LoopNest *task = nullptr;                    // The parallel for loop it belongs to
        const LoopNest *thread = nullptr;                  // Its containing gpu_thread loop
        GPUMemoryType gpu_store_memory_type;               // global, local, shared?
        int64_t allocation_size = 0;                       // Allocation size in bytes
        bool is_constant_allocation = false;               // Does the allocation have constant size?
        int64_t num_realizations = 0;                      // Number of times this stage is realized. Only valid for unscheduled producers
        bool inlined = false;                              // Is the Func inlined?
        std::vector<const LoopNest *> inlined_innermosts;  // Is the Func inlined?
        uint64_t hash_of_producers_stored_at_root;

        bool is_stored_in_global_mem() const {
            return gpu_store_memory_type == GPUMemoryType::Global;
        }
        bool is_stored_in_shared_mem() const {
            return gpu_store_memory_type == GPUMemoryType::Shared;
        }
        bool is_stored_in_local_mem() const {
            return gpu_store_memory_type == GPUMemoryType::Local;
        }
        bool is_stored_in_registers() const {
            return gpu_store_memory_type == GPUMemoryType::Registers;
        }
    };

    GPUMemoryType get_gpu_memory_type(bool in_block, bool in_thread, bool is_inlined = false) const;

    std::vector<int> unrolled_loops(const Target &target, const LoopNest *parent, const LoopNest *grandparent) const;

    void get_allocs_that_can_be_promoted_to_registers(const Target &target,
                                                      StageMap<Sites> &sites,
                                                      NodeMap<bool> &can_be_promoted_to_registers,
                                                      const LoopNest *grandparent,
                                                      const LoopNest *parent) const;

    bool promote_allocs_to_registers(const Target &target, StageMap<Sites> &sites) const;

    // Compute all the sites of interest for each pipeline stage
    void get_sites(const Target &target,
                   StageMap<Sites> &sites,
                   StageMap<int64_t> &shared_mem_alloc_sizes,
                   const LoopNest *task = nullptr,
                   const LoopNest *parent = nullptr,
                   const LoopNest *current_thread_loop = nullptr) const;

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

    bool exceeds_serial_extents_limit(const Target &target, const LoopNest *parent, bool in_threads_loop) const;

    bool node_has_dynamic_region_computed(const FunctionDAG::Node *f) const;

    bool has_dynamic_allocation_inside_thread(bool in_thread_loop) const;

    const LoopNest *find_pure_stage_loop_nest(const FunctionDAG::Node *node) const;

    int get_pure_stage_vectorized_loop_index(const FunctionDAG::Node *node) const;

    int get_vectorized_loop_index_from_pure_stage(const LoopNest &root) const;

    // Get the stride over "node's" storage for a unit increment in the vectorized loop's
    // index
    double storage_stride(const LoadJacobian &jac, int innermost_storage_dim, const FunctionDAG::Node *storage_node, const Bound &store_bounds, const LoopNest &root) const;

    Strides compute_strides(const LoadJacobian &jac, int innermost_storage_dim, const FunctionDAG::Node *storage_node, const Bound &store_bounds, const ThreadInfo &thread_info, bool verbose = false) const;

    bool all_strides_exist(const LoadJacobian &jac, const FunctionDAG::Node *storage_node, const LoopNest &root) const;

    int get_actual_vector_dim(const Bound &store_bounds) const;

    void compute_gpu_store_features(const LoadJacobian &jac, int consumer_innermost_dim, const FunctionDAG::Node *node, const Bound &consumer_store_bounds, const GPULoopInfo &gpu_loop_info, const std::vector<int64_t> &inner_serial_loop_extents, const Sites &consumer_site, ScheduleFeatures &feat, const LoopNest *parent, const LoopNest &root, GlobalMemInfo &global_mem_loads, SharedMemInfo &shared_mem_loads, LocalMemInfo &local_mem_loads, bool verbose = false) const;

    bool can_vectorize_access_for_innermost_dim(const LoadJacobian &jac, const FunctionDAG::Node *accessed, int innermost_dim, int loop_index) const;

    bool can_vectorize_store_access(const LoadJacobian &jac, const FunctionDAG::Node *accessed, bool accessed_has_been_scheduled, int innermost_dim, int loop_index, const GPUMemoryType &mem_type) const;

    int vectorized_load_access_size(const LoadJacobian &jac, const FunctionDAG::Node *accessed, bool accessed_has_been_scheduled, int innermost_dim, const GPUMemoryType &mem_type, bool verbose = false) const;

    int vectorized_access_size(size_t loop_index, bool verbose = false) const;

    template<typename T>
    void compute_num_mem_accesses_per_block(const LoadJacobian &jac, const FunctionDAG::Node *node, const Bound &store_bounds, const ThreadInfo &thread_info, int innermost_dim, double num_requests_per_warp, MemInfoType<T> &mem_info, bool verbose = false) const;

    std::pair<double, double> compute_local_mem_store_features(const LoadJacobian &jac, int consumer_innermost_dim, const FunctionDAG::Node *node, const Bound &consumer_store_bounds, const LoopNest &root, double serial_loop_extents) const;

    template<typename T>
    MemInfoType<T> compute_mem_store_info(const LoadJacobian &jac, int consumer_innermost_dim, const FunctionDAG::Node *node, const Bound &consumer_store_bounds, const ThreadInfo &thread_info, double serial_loop_extents, bool verbose) const;

    template<typename T>
    void compute_mem_load_features(const LoadJacobian &jac, int producer_innermost_dim, const FunctionDAG::Node *node, const Bound &producer_store_bounds, bool producer_has_been_scheduled, const ThreadInfo &thread_info, MemInfoType<T> &mem_info, double serial_loop_extents, bool verbose = false) const;

    double compute_local_mem_stride(double stride, double bytes) const;

    // Assumes block, serial, thread or block, thread nesting
    const LoopNest *get_enclosing_block(const LoopNest *parent, const LoopNest *grandparent) const;

    std::pair<int64_t, int64_t> get_block_and_serial_extents(const LoopNest *block) const;

    bool all_paths_to_leaves_have_thread_loop() const;

    bool has_thread_loop_descendant() const;

    void compute_warp_features(ScheduleFeatures &features, const GPULoopInfo &gpu_loop_info) const;

    // Assume that when a block is active, all its warps are active
    void compute_warp_and_block_occupancy(const Anderson2021Params &params, ScheduleFeatures &feat, const GPULoopInfo &gpu_loop_info) const;

    void compute_shared_mem_occupancy(const Anderson2021Params &params, const Target &target, int64_t total_shared_mem_alloc_size, ScheduleFeatures &feat) const;

    std::pair<const LoopNest *, const LoopNest *> find_innermost_and_parent() const;

    int64_t points_accessed_per_thread(const Anderson2021Params &params, const Target &target, const GPULoopInfo &gpu_loop_info, const std::vector<const FunctionDAG::Edge *> &edge_chain, const LoadJacobian &jac, const LoopNest *parent, const LoopNest *grandparent, int64_t n, const ScheduleFeatures &feat, const LoadJacobian &serial_jac, bool producer_has_been_scheduled, int producer_innermost_dim, const GPUMemoryType &mem_type, bool verbose = false) const;

    int64_t compute_licm_amortization(const LoopNest *innermost, const LoopNest *parent, const ScheduleFeatures &feat, const LoadJacobian &jac, int producer_dims) const;

    void memoize_points_computed_minimum(StageMap<ScheduleFeatures> &memoized_features, const StageMap<ScheduleFeatures> *features) const;

    vector<pair<int, int>> collect_producers(const StageMap<Sites> &sites) const;

    uint64_t compute_hash_of_producers_stored_at_root(const StageMap<Sites> &sites) const;

    void collect_stages(std::set<const FunctionDAG::Node::Stage *> &stages) const;

    void memoize_features(StageMap<ScheduleFeatures> &memoized_features, const StageMap<ScheduleFeatures> *features) const;

    void compute_working_set_from_features(int64_t *working_set,
                                           const StageMap<ScheduleFeatures> *features) const;

    void recompute_inlined_features(const StageMap<Sites> &sites, StageMap<ScheduleFeatures> *features) const;

    std::pair<int64_t, bool> compute_alloc_size_of_node_here(const FunctionDAG::Node *f) const;

    // Do a recursive walk over the loop nest computing features to feed the cost model.
    void compute_features(const FunctionDAG &dag,
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
                          bool verbose = false) const;

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
    const Bound &get_bounds(const FunctionDAG::Node *f) const;

    // Get the region required of a Func at this site (but only to satisfy the
    // consumers along the given edge chain), from which we know what region
    // would be computed if it were scheduled here and what its loop nest
    // would be.
    Bound get_bounds_along_edge_chain(const FunctionDAG::Node *f, const vector<const FunctionDAG::Edge *> &edge_chain) const;

    void dump() const;

    std::string to_string() const;

    // Recursively print a loop nest representation to stderr
    template<typename T>
    void dump(T &stream, string prefix, const LoopNest *parent) const;

    // Does this loop nest access the given Func
    bool calls(const FunctionDAG::Node *f) const;

    // What is the maximum number of inlined calls to a Func that
    // occur within this loop. Used to prune states that would
    // generate too much code.
    int64_t max_inlined_calls() const;

    // Does this loop nest access an input buffer? Used to select
    // trail strategies when splitting loops. We don't want to read
    // out of bounds on inputs, even if we don't intend to use the
    // values read. It could create annoying assertion failures for
    // the user. It's OK to read out of range of the values computed
    // on internal Funcs though. Allocation bounds inference just pads
    // out the bounds so that it won't fault.
    bool accesses_input_buffer() const;

    // Does this loop nest contain a computation of the given Func.
    bool computes(const FunctionDAG::Node *f) const;

    // Above here most methods query the loop nest. Below we have
    // methods that mutate the loop nest.

    // Inline a Func into all consumers within this loop.
    void inline_func(const FunctionDAG::Node *f);

    // Compute a Func at this site.
    bool compute_here(const FunctionDAG::Node *f,
                      bool tileable,
                      int v,
                      bool in_threads_loop,
                      const Anderson2021Params &params,
                      const Target &target);

    // Parallelize this loop according to the given tiling.
    IntrusivePtr<const LoopNest> parallelize_in_tiles(const vector<int64_t> &tiling,
                                                      const LoopNest *parent,
                                                      const Anderson2021Params &params,
                                                      const Target &target,
                                                      bool inner_tiling,
                                                      bool adjust_tiling,
                                                      bool move_all_rvars_inward = true,
                                                      const vector<int> &rvars_to_move_inward = {}) const;

    int64_t get_total_local_mem_alloc_size(bool constant_allocs_only = false, bool in_threads_loop = false) const;
    int64_t get_total_constant_local_mem_alloc_size() const;

    // All store ats further in than the block level must be fixed
    // sized allocations. This method checks if f will require a dynamic
    // allocation
    bool requires_dynamic_allocation(const FunctionDAG::Node *f, const Target &target, bool in_threads_loop) const;

    // Return all possible ways to compute f in tiles somewhere within
    // this loop nest.
    // in_threads_loop tracks whether or not function is going to be placed inside a
    // loop marked gpu_threads, in which case f's loops cannot be gpu_threads
    vector<IntrusivePtr<const LoopNest>> compute_in_tiles(const FunctionDAG::Node *f,
                                                          const LoopNest *parent,
                                                          const Anderson2021Params &params,
                                                          const Target &target,
                                                          const SearchSpaceOptions &search_space_options,
                                                          int v,
                                                          bool in_realization,
                                                          bool in_threads_loop,
                                                          bool is_pre_pass,
                                                          vector<int64_t> union_counts = vector<int64_t>()) const;

    // Below here we have methods that apply a schedule to a Halide pipeline.

    // A model of the state of the loop nest of a Func while applying
    // Halide's scheduling directives.

    // Note that StageScheduleState is movable-but-not-copyable thanks to its ostringstream member.
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

            bool vectorized = false;
            bool gpu_threads = false;

            FuncVar()
                : orig(Var()), var(Var()) {
            }
        };
        const FunctionDAG::Node *node;
        const FunctionDAG::Node::Stage *stage;
        bool parallel = false;
        bool vectorized = false;
        bool all_innermost_unrolled = false;
        FuncVar vectorized_var;

        // In order from innermost to outermost. Each group of d is one tiling level.
        vector<FuncVar> vars;

        // In order from innermost to outermost. Each group of d is one tiling level.
        vector<FuncVar> ordered_vars;
        vector<int64_t> gpu_thread_extents;

        NodeMap<std::vector<std::pair<const LoopNest *, std::vector<const FunctionDAG::Edge *>>>> producers_to_be_staged;

        // From outermost in
        vector<StageScheduleState *> ancestors;

        std::ostringstream schedule_source;
    };

    bool has_constant_region_computed(const FunctionDAG::Node *node) const;
    bool has_constant_region_required(const FunctionDAG::Node *node) const;
    bool other_stage_has_same_producer(const FunctionDAG::Node *producer) const;
    int num_serial_loops(const FunctionDAG::Node::Stage *stage) const;
    int num_serial_loops() const;
    bool producer_computed_here_or_further_in(const FunctionDAG::Node *producer) const;

    void update_producers_to_be_staged(StageScheduleState &state, const NodeMap<bool> &all_inlined) const;
    bool region_computed_shrinks(const FunctionDAG::Node *f, const LoopNest *parent) const;

    // Apply the schedule represented by this loop nest to a Halide pipeline.
    void apply(LoopLevel here,
               StageMap<std::unique_ptr<StageScheduleState>> &state_map,
               double num_cores,
               int depth,
               const LoopNest *parent,
               const LoopNest *compute_site,
               const Target &target,
               std::vector<StageScheduleState *> &ancestors,
               const NodeMap<bool> &all_inlined) const;

    double max_idle_lane_wastage(const Target &target, GPULoopInfo gpu_loop_info) const;

    bool has_valid_thread_extents() const;

    void collect_nodes_that_should_be_inlined(const NodeMap<bool> &nodes_to_freeze, NodeMap<bool> &inlined_nodes) const;

    void collect_all_inlined(NodeMap<bool> &all_inlined) const;

    int64_t product_of_self_and_descendants(int loop_index) const;
    int64_t product_of_descendants(int loop_index) const;

    void get_stages_computed_in_each_compute_root_loop(StageMap<StageMap<bool>> &descendants, const LoopNest *compute_root_loop_nest = nullptr) const;
};

struct Filter {
    const LoopNest *loop_nest;
    bool logging = false;

    explicit Filter(const LoopNest *loop_nest)
        : loop_nest{loop_nest}, logging{enable_filter_printing()} {
        if (logging) {
            std::cerr << "\nState filtered: \n";
            loop_nest->dump();
            std::cerr << "Reason: ";
        }
    }

    template<typename T>
    Filter &operator<<(T &&x) {
        if (logging) {
            std::cerr << std::forward<T>(x);
        }
        return *this;
    }

    static bool enable_filter_printing();
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif  // LOOP_NEST_H
