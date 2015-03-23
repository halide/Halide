#include "ScheduleOptimizationLevels.h"

namespace Halide {
namespace Internal {

namespace {
/** Return the optimization level controlled by HL_SCHED_OPT
 * environment variable. */
ScheduleOptimization::Level get_optimization_level() {
    char *level = getenv("HL_SCHED_OPT");
    int i = level ? atoi(level) : 0;
    switch (i) {
    case 0:
        return ScheduleOptimization::LEVEL_0;
    default:
        return ScheduleOptimization::LEVEL_0;
    }
}

/** Return the optimization corresponding to the given level. */
ScheduleOptimization *get_optimization(ScheduleOptimization::Level level) {
    switch (level) {
    case ScheduleOptimization::LEVEL_0:
        return new OptimizationLevel0();
    }
    return NULL;
}
} // end anonymous namespace

void apply_schedule_optimization(Func func) {
    ScheduleOptimization::Level level = get_optimization_level();
    ScheduleOptimization *opt = get_optimization(level);
    internal_assert(opt);
    opt->apply(func);
}

}
}
