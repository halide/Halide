#ifndef HALIDE_INTERNAL_REGION_COSTS_H
#define HALIDE_INTERNAL_REGION_COSTS_H

/** \file
 *
 * Defines RegionCosts - used by the auto scheduler to query the cost of
 * computing some function regions.
 */

#include <map>
#include <string>
#include <vector>

#include "AutoScheduleUtils.h"
#include "Interval.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

struct Cost {
    // Estimate of cycles spent doing arithmetic.
    Expr arith;
    // Estimate of bytes loaded.
    Expr memory;

    Cost(int64_t arith, int64_t memory)
        : arith(arith), memory(memory) {
    }
    Cost(Expr arith, Expr memory)
        : arith(std::move(arith)), memory(std::move(memory)) {
    }
    Cost() = default;

    inline bool defined() const {
        return arith.defined() && memory.defined();
    }
    void simplify();

    friend std::ostream &operator<<(std::ostream &stream, const Cost &c) {
        stream << "[arith: " << c.arith << ", memory: " << c.memory << "]";
        return stream;
    }
};

/** Auto scheduling component which is used to assign costs for computing a
 * region of a function or one of its stages. */
struct RegionCosts {
    /** An environment map which contains all functions in the pipeline. */
    StringMap<Function> env;
    /** Realization order of functions in the pipeline. The first function to
     * be realized comes first. */
    std::vector<std::string> order;
    /** A map containing the cost of computing a value in each stage of a
     * function. The number of entries in the vector is equal to the number of
     * stages in the function. */
    StringMap<std::vector<Cost>> func_cost;
    /** A map containing the types of all image inputs in the pipeline. */
    StringMap<Type> inputs;
    /** A scope containing the estimated min/extent values of ImageParams
     * in the pipeline. */
    Scope<Interval> input_estimates;

    /** Return the cost of producing a region (specified by 'bounds') of a
     * function stage (specified by 'func' and 'stage'). 'inlines' specifies
     * names of all the inlined functions. */
    Cost stage_region_cost(std::string_view func, int stage, const DimBounds &bounds,
                           const StringSet &inlines = StringSet());

    /** Return the cost of producing a region of a function stage (specified
     * by 'func' and 'stage'). 'inlines' specifies names of all the inlined
     * functions. */
    Cost stage_region_cost(std::string_view func, int stage, const Box &region,
                           const StringSet &inlines = StringSet());

    /** Return the cost of producing a region of function 'func'. This adds up the
     * costs of all stages of 'func' required to produce the region. 'inlines'
     * specifies names of all the inlined functions. */
    Cost region_cost(std::string_view func, const Box &region,
                     const StringSet &inlines = StringSet());

    /** Same as region_cost above but this computes the total cost of many
     * function regions. */
    Cost region_cost(const StringMap<Box> &regions,
                     const StringSet &inlines = StringSet());

    /** Compute the cost of producing a single value by one stage of 'f'.
     * 'inlines' specifies names of all the inlined functions. */
    Cost get_func_stage_cost(const Function &f, int stage,
                             const StringSet &inlines = StringSet()) const;

    /** Compute the cost of producing a single value by all stages of 'f'.
     * 'inlines' specifies names of all the inlined functions. This returns a
     * vector of costs. Each entry in the vector corresponds to a stage in 'f'. */
    std::vector<Cost> get_func_cost(const Function &f,
                                    const StringSet &inlines = StringSet());

    /** Computes the memory costs of computing a region (specified by 'bounds')
     * of a function stage (specified by 'func' and 'stage'). This returns a map
     * containing the costs incurred to access each of the functions required
     * to produce 'func'. */
    StringMap<Expr>
    stage_detailed_load_costs(std::string_view func, int stage, DimBounds &bounds,
                              const StringSet &inlines = StringSet());

    /** Return a map containing the costs incurred to access each of the functions
     * required to produce a single value of a function stage. */
    StringMap<Expr>
    stage_detailed_load_costs(std::string_view func, int stage,
                              const StringSet &inlines = StringSet());

    /** Same as stage_detailed_load_costs above but this computes the cost of a region
     * of 'func'. */
    StringMap<Expr>
    detailed_load_costs(std::string_view func, const Box &region,
                        const StringSet &inlines = StringSet());

    /** Same as detailed_load_costs above but this computes the cost of many function
     * regions and aggregates them. */
    StringMap<Expr>
    detailed_load_costs(const StringMap<Box> &regions,
                        const StringSet &inlines = StringSet());

    /** Return the size of the region of 'func' in bytes. */
    Expr region_size(std::string_view func, const Box &region);

    /** Return the size of the peak amount of memory allocated in bytes. This takes
     * the realization (topological) order of the function regions and the early
     * free mechanism into account while computing the peak footprint. */
    Expr region_footprint(const StringMap<Box> &regions,
                          const StringSet &inlined = StringSet());

    /** Return the size of the input region in bytes. */
    Expr input_region_size(std::string_view input, const Box &region);

    /** Return the total size of the many input regions in bytes. */
    Expr input_region_size(const StringMap<Box> &input_regions);

    /** Display the cost of each function in the pipeline. */
    void disp_func_costs();

    /** Construct a region cost object for the pipeline. 'env' is a map of all
     * functions in the pipeline. 'order' is the realization order of functions
     * in the pipeline. The first function to be realized comes first. */
    RegionCosts(const StringMap<Function> &env,
                const std::vector<std::string> &order);
};

/** Return true if the cost of inlining a function is equivalent to the
 * cost of calling the function directly. */
bool is_func_trivial_to_inline(const Function &func);

}  // namespace Internal
}  // namespace Halide

#endif
