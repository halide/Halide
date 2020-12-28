/** This file defines the LoopNest, which is our
 * representation of a Halide schedule, and contains methods to
 * generate candidates for scheduling as well as extract a
 * featurization that can be used to cost each candidate. */

#ifndef LOOP_NEST_H
#define LOOP_NEST_H

#include "FunctionDAG.h"
#include "PerfectHashMap.h"
#include <set>
#include <vector>

namespace Halide {
namespace Internal {
namespace Autoscheduler {

template<typename T>
using NodeMap = PerfectHashMap<FunctionDAG::Node, T>;

template<typename T>
using StageMap = PerfectHashMap<FunctionDAG::Node::Stage, T>;

bool may_subtile();

// Given a multi-dimensional box of dimensionality d, generate a list
// of candidate tile sizes for it, logarithmically spacing the sizes
// using the given factor. If 'allow_splits' is false, every dimension
// must either be one, or the full extent of the box. This function is
// used to generate candidate tilings when tiling for
// producer-consumer fusion, or tiling for parallelism.
std::vector<std::vector<int64_t>> generate_tilings(const vector<int64_t> &s, int d, int factor, bool allow_splits);

struct LoopNest {
    mutable RefCount ref_count;

    // The extents of this loop. Put another way, the number of tiles,
    // not the size of each tile.
    std::vector<int64_t> size;

    // The nodes inside the loop body
    std::vector<IntrusivePtr<const LoopNest>> children;

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
        const LoopNest *compute = nullptr;    // Its containing compute_at site
        const LoopNest *store = nullptr;      // Its containing store_at site
        const LoopNest *produce = nullptr;    // Its own outermost node
        const LoopNest *innermost = nullptr;  // Its innermost node - usually a SIMD loop
        const LoopNest *task = nullptr;       // The parallel for loop it belongs to
        bool inlined = false;                 // Is the Func inlined?
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

    // Do a recursive walk over the loop nest computing features to feed the cost model.
    void compute_features(const FunctionDAG &dag,
                          const MachineParams &params,
                          const StageMap<Sites> &sites,
                          int64_t instances,
                          int64_t parallelism,
                          const LoopNest *parent,
                          const LoopNest *grandparent,
                          const LoopNest &root,
                          int64_t *working_set,
                          StageMap<ScheduleFeatures> *features) const;

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
    void compute_here(const FunctionDAG::Node *f, bool tileable, int v);

    // Parallelize this loop according to the given tiling.
    IntrusivePtr<const LoopNest> parallelize_in_tiles(const MachineParams &params,
                                                      const vector<int64_t> &tiling,
                                                      const LoopNest *parent) const;

    // Return all possible ways to compute f in tiles somewhere within
    // this loop nest.
    std::vector<IntrusivePtr<const LoopNest>> compute_in_tiles(const FunctionDAG::Node *f,
                                                               const LoopNest *parent,
                                                               const MachineParams &params,
                                                               int v,
                                                               bool in_realization) const;

    // Below here we have methods that apply a schedule to a Halide pipeline.

    // A model of the state of the loop nest of a Func while applying
    // Halide's scheduling directives.

    // Note that StageScheduleState is movable-but-not-copyable thanks
    // to its ostringstream member.
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
            FuncVar()
                : orig(Var()), var(Var()) {
            }
        };

        // In order from innermost to outermost. Each group of d is one tiling level.
        std::vector<FuncVar> vars;

        std::ostringstream schedule_source;
    };

    // Apply the schedule represented by this loop nest to a Halide pipeline.
    void apply(LoopLevel here,
               StageMap<std::unique_ptr<StageScheduleState>> &state_map,
               double num_cores,
               int depth,
               const LoopNest *parent,
               const LoopNest *compute_site) const;
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif  // LOOP_NEST_H
