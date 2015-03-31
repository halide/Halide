#ifndef HALIDE_AUTOMATIC_SCHEDULING_H
#define HALIDE_AUTOMATIC_SCHEDULING_H

/** \file
 * Defines various automatic scheduling routines.
 */

#include "IR.h"
#include "Target.h"

namespace Halide {

class Func;

typedef enum {
    ComputeRootAllStencils,
    ParallelizeOuter,
    VectorizeInner
} AutoScheduleStrategy;

namespace Internal {

/** Base class for all automatic scheduling strategy implementations. */  
class AutoScheduleStrategyImpl {
public:
    /** Apply the schedule strategy to the pipeline. 'func' should
     * be the output of the pipeline. */
    virtual void apply(Func root, const Target &target) = 0;
    virtual ~AutoScheduleStrategyImpl() {}
};

/** Performs the following pipeline optimization:
 * - Functions called as a stencil are compute_root.
 */
class ComputeRootAllStencils : public AutoScheduleStrategyImpl {
public:
    virtual void apply(Func root, const Target &target);
};

/** Performs the following pipeline optimization:
 * - Parallelize the outermost dimension of all non-inlined functions.
 */
class ParallelizeOuter : public AutoScheduleStrategyImpl {
public:
    virtual void apply(Func root, const Target &target);
private:
    void parallelize_outer(Function f);
    void vectorize_inner(Function f);
};

/** Performs the following pipeline optimization:
 * - Vectorize the innermost dimension of all non-inlined functions.
 */
class VectorizeInner : public AutoScheduleStrategyImpl {
public:
    virtual void apply(Func root, const Target &target);
};

/** Apply the given schedule strategy to the pipeline with output
 * 'root'. */
EXPORT void apply_automatic_schedule(Func root, AutoScheduleStrategy strategy,
                                     bool reset_schedules, const Target &target);

}
}

#endif
