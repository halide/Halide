#ifndef HALIDE_INTERNAL_AUTO_SCHEDULE_NEW_H
#define HALIDE_INTERNAL_AUTO_SCHEDULE_NEW_H

/** \file
 *
 * Defines the method that does automatic scheduling of Funcs within a pipeline.
 */

#include "Function.h"
#include "Target.h"
#include "AutoSchedule.h"

namespace Halide {
namespace Internal {

/** Generate schedules for Funcs within a pipeline. The Funcs should not already
 * have specializations or schedules as the current auto-scheduler does not take
 * into account user-defined schedules or specializations. This applies the
 * schedules and returns a string representation of the schedules. The target
 * architecture is specified by 'target'. */
std::string generate_schedules_new(const std::vector<Function> &outputs,
                                   const Target &target,
                                   const MachineParams &arch_params);

std::string generate_schedules_autotune(const std::vector<Function> &output_funcs,
                                        const Target &target,
                                        const MachineParams &params);

void autoschedule_test();

}
}

#endif
