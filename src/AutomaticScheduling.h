#ifndef HALIDE_AUTOMATIC_SCHEDULING_H
#define HALIDE_AUTOMATIC_SCHEDULING_H

/** \file
 * Defines various automatic scheduling routines.
 */

#include "Func.h"
#include "IR.h"

namespace Halide {
namespace Internal {

class ScheduleOptimization {
public:
    /** Level of optimization. */
    typedef enum { LEVEL_0, LEVEL_1, LEVEL_2 } Level;
    /** Apply the schedule optimization to the pipeline. 'func' should
     * be the output of the pipeline. */
    virtual void apply(Func func) = 0;
};

/** Optimization level 0 does nothing. */
class OptimizationLevel0 : public ScheduleOptimization {
public:
    virtual void apply(Func func) { }
};

/** Optimization level 1 performs the following simple optimization:
 * - Functions with a single callsite are inlined.
 * - Functions called as a stencil are compute_root.
 */
class OptimizationLevel1 : public ScheduleOptimization {
public:
    virtual void apply(Func func);
};

/** Optimization level 2 performs the following optimization:
 * - Run optimization level 1.
 * - For all compute_root functions, vectorize the innermost dimension
 *   and parallelize the outermost dimension.
 */
class OptimizationLevel2 : public ScheduleOptimization {
public:
    virtual void apply(Func func);
private:
    void parallelize_outer(Function f);
    void vectorize_inner(Function f);
};

/** Apply schedule optimizations, controled by the HL_SCHED_OPT
 * environment variable. */
void apply_schedule_optimization(Func func);

}
}

#endif
