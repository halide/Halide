/** This file defines the LoopNest, which is our
 * representation of a Halide schedule, and contains methods to
 * generate candidates for scheduling as well as extract a
 * featurization that can be used to cost each candidate. */

#ifndef LOOP_NEST_H
#define LOOP_NEST_H

#include "FunctionDAG.h"
#include "GlobalMemInfo.h"
#include "GPULoopInfo.h"
#include "PerfectHashMap.h"
#include "ThreadInfo.h"
#include <set>
#include <vector>

namespace Halide {
namespace Internal {
namespace Autoscheduler {

template<typename T>
using NodeMap = PerfectHashMap<FunctionDAG::Node, T>;

template<typename T>
using StageMap = PerfectHashMap<FunctionDAG::Node::Stage, T>;

enum GPU_parallelism { block, thread, serial, simd, parallelized, none };

bool may_subtile();

int64_t get_shared_memory_limit();

int64_t get_active_block_hardware_limit();

int64_t get_active_warp_hardware_limit();

int get_unroll_limit(const Target& target);

bool in_range_zero_one(double x);

bool are_valid_thread_extents(const vector<int64_t>& counts);


/** moves vectorized dimension first and also removes dimensions with size 1
    to reflect actual thread dimensions when loop nests are lowered **/
void lowered_dims(const vector<int64_t> &size, int vector_loop_i, vector<int64_t> &lowered_size);

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
        int d, const vector<int> &vectorized_indices, bool serial_inner);

// used for creating default serial loop tiling options inside gpu threads loop
vector<vector<int64_t>> generate_serial_tilings(const vector<int64_t> &s, int d,
                                                int vectorized_index,
                                                const vector<int> &vec_dim_serial_sizes);


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
                                         const vector<int> &inner_sizes = vector<int>());

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
    mutable GPU_parallelism gpu_label = none;

    bool is_gpu_serial(const Target& target) const {
        return target.has_gpu_feature() && gpu_label == serial;
    }

    bool is_gpu_thread(const Target& target) const {
        return target.has_gpu_feature() && gpu_label == thread;
    }

    bool is_gpu_block(const Target& target) const {
        return target.has_gpu_feature() && gpu_label == block;
    }

    bool is_scalar() const {
        return size.size() == 0;
    }

    // given a newly inserted node f into this LoopNest, get union of thread counts in each dimension
    // across all siblings of f.
    vector<int64_t> get_union_thread_counts(const FunctionDAG::Node *f) const;

    // given a newly inserted node f into this LoopNest, gets the size of
    // all of f's stages and their pure_dim indices
    void get_stage_sizes(const FunctionDAG::Node *f,
        vector<vector<int64_t>> &stage_sizes,
        vector<vector<int>> &pure_dims,
        vector<int> &vectorized_indices);

    // given the loop nest of a stage to parallelize at root, figure out if using odd tile sizes
    // for the vectorized dimension will allow the resulting thread tiles to be multiples of 32
    // if so, we will include these in the serial loop sizes
    void generate_vec_dim_serial_tilings(vector<int> &serial_sizes) const;

    // get the loop nests of a newly inserted node, f, that is marked GPU threads. Tiles
    // the newly inserted loop nests of f into a threads loop outside a serial loop.
    // V is the vectorized dimension of f. Adds loopnests created from each tiling option in result.
    bool add_gpu_thread_tilings(const FunctionDAG::Node *f,
                                const MachineParams &params,
                                const Target &target,
                                int v,
                                vector<IntrusivePtr<const LoopNest>> &result,
                                vector<int64_t> max_size);

    void copy_from(const LoopNest &n);

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
                   const LoopNest *parent = nullptr) const;

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

    bool exceeds_serial_extents_limit(bool in_threads_loop) const;

    bool node_has_dynamic_region_computed(const FunctionDAG::Node* f) const;

    bool has_dynamic_allocation_inside_thread(bool in_thread_loop) const;

    const LoopNest* find_pure_stage_loop_nest(const FunctionDAG::Node* node) const;

    int get_pure_stage_vectorized_loop_index(const FunctionDAG::Node* node) const;

    int get_vectorized_loop_index_from_pure_stage(const LoopNest& root) const;

    // Get the stride over "node's" storage for a unit increment in the vectorized loop's
    // index
    double storage_stride(const LoadJacobian& jac, int innermost_storage_dim, const FunctionDAG::Node* storage_node, const Bound& store_bounds, const LoopNest& root) const;

    bool all_strides_exist(const LoadJacobian& jac, const FunctionDAG::Node* storage_node, const LoopNest& root) const;

    std::pair<int, double> num_shared_mem_accesses(const FunctionDAG::Node* node, const ThreadInfo& thread_info, double serial_loop_extents, double stride) const;

    int num_banks_per_access(const FunctionDAG::Node* node) const;

    int compute_min_accesses(const FunctionDAG::Node* node, const ThreadInfo& thread_info, double stride, double serial_loop_extents) const;

    std::pair<double, double> compute_shared_mem_stores(const LoadJacobian& jac, int consumer_innermost_dim, const FunctionDAG::Node* node, const Bound& consumer_store_bounds, const ThreadInfo& thread_info, double serial_loop_extents, const LoopNest& root) const;

    std::pair<double, double> compute_shared_mem_load_features(const LoadJacobian& jac, int producer_innermost_dim, const FunctionDAG::Node* node, const Bound& producer_store_bounds, bool producer_has_been_scheduled, const ThreadInfo& thread_info, const LoopNest& root, double serial_loop_extents) const;

    void compute_gpu_store_features(const LoadJacobian& jac, int consumer_innermost_dim, const FunctionDAG::Node* node, const Bound& consumer_store_bounds, const ThreadInfo& thread_info, double serial_loop_extents, const Sites& consumer_site, ScheduleFeatures& feat, const LoopNest& root) const;

    int word_stride(const FunctionDAG::Node* node) const;

    int num_words_per_access(const FunctionDAG::Node* node) const;

    double min_global_mem_accesses(const FunctionDAG::Node* node, const ThreadInfo& thread_info, double serial_loop_extents, double stride) const;

    void compute_num_global_mem_accesses_per_block(const LoadJacobian& jac, const FunctionDAG::Node* node, const Bound& store_bounds, const ThreadInfo& thread_info, int innermost_dim, double serial_loop_extents, GlobalMemInfo& global_mem_info, const LoopNest& root) const;

    GlobalMemInfo compute_global_mem_store_features(const LoadJacobian& jac, int consumer_innermost_dim, const FunctionDAG::Node* node, const Bound& consumer_store_bounds, const ThreadInfo& thread_info, double serial_loop_extents, const LoopNest& root) const;

    void compute_global_mem_load_features(const LoadJacobian& jac, int producer_innermost_dim, const FunctionDAG::Node* node, const Bound& producer_store_bounds, bool producer_has_been_scheduled, const ThreadInfo& thread_info, GlobalMemInfo& global_mem_info, double serial_loop_extents_and_load_count, const LoopNest& root) const;

    // Assumes block, serial, thread or block, thread nesting
    const LoopNest* get_enclosing_block(const LoopNest *parent, const LoopNest *grandparent) const;

    std::pair<int64_t, int64_t> get_block_and_serial_extents(const LoopNest* block) const;

    bool all_paths_to_leaves_have_thread_loop() const;

    bool has_thread_loop_descendant() const;

    void compute_warp_features(ScheduleFeatures& features, const GPULoopInfo& gpu_loop_info) const;

    // Assume that when a block is active, all its warps are active
    void compute_warp_and_block_occupancy(ScheduleFeatures &feat, const GPULoopInfo& gpu_loop_info) const;

    void compute_shared_mem_occupancy(const Target& target, int64_t working_set_here, ScheduleFeatures &feat) const;

    std::pair<const LoopNest*, const LoopNest*> find_innermost_and_parent() const;

    int64_t compute_licm_amortization(const LoopNest* innermost, const LoopNest* parent, const ScheduleFeatures& feat, const LoadJacobian& jac, int producer_dims) const;

    // Do a recursive walk over the loop nest computing features to feed the cost model.
    void compute_features(const FunctionDAG &dag,
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
                          GPULoopInfo gpu_loop_info) const;

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

    // Recursively print a loop nest representation to stderr
    void dump(string prefix, const LoopNest *parent) const;

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
    void compute_here(const FunctionDAG::Node *f,
                      bool tileable,
                      int v,
                      bool in_threads_loop,
                      const Target &target);

    // Parallelize this loop according to the given tiling.
    IntrusivePtr<const LoopNest> parallelize_in_tiles(const MachineParams &params,
                                                      const vector<int64_t> &tiling,
                                                      const LoopNest *parent,
                                                      const Target& target,
                                                      bool inner_tiling,
                                                      bool adjust_tiling) const;

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
                                                          const MachineParams &params,
                                                          const Target &target,
                                                          int v,
                                                          bool in_realization,
                                                          bool in_threads_loop,
                                                          vector<int64_t> union_counts=vector<int64_t>()) const;

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

            FuncVar() : orig(Var()), var(Var()) {}
        };
        const FunctionDAG::Node* node;
        bool parallel = false;
        bool vectorized = false;
        FuncVar vectorized_var;

        // In order from innermost to outermost. Each group of d is one tiling level.
        vector<FuncVar> vars;

        // In order from innermost to outermost. Each group of d is one tiling level.
        vector<FuncVar> ordered_vars;
        vector<int64_t> gpu_thread_extents;

        // From outermost in
        vector<StageScheduleState*> ancestors;

        std::ostringstream schedule_source;
    };

    // Apply the schedule represented by this loop nest to a Halide pipeline.
    void apply(LoopLevel here,
               StageMap<std::unique_ptr<StageScheduleState>> &state_map,
               double num_cores,
               int depth,
               const LoopNest *parent,
               const LoopNest *compute_site,
               const Target& target,
               std::vector<StageScheduleState*>& ancestors) const;
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif  // LOOP_NEST_H
