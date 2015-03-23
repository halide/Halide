#ifndef HALIDE_SCHEDULE_OPTIMIZATION_LEVELS_H
#define HALIDE_SCHEDULE_OPTIMIZATION_LEVELS_H

/** \file
 * Defines the various optimization levels for scheduling.
 */

#include "Func.h"
#include "IR.h"

namespace Halide {
namespace Internal {

class ScheduleOptimization {
public:
    typedef enum { LEVEL_0 } Level;
    virtual void apply(Func func) = 0;
};

class OptimizationLevel0 : public ScheduleOptimization {
public:
    virtual void apply(Func func) { }
};

/** Apply schedule optimizations, controled by the HL_SCHED_OPT
 * environment variable. */
void apply_schedule_optimization(Func func);

}
}

#endif
