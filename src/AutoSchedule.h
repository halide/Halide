#ifndef HALIDE_INTERNAL_AUTO_SCHEDULE_H
#define HALIDE_INTERNAL_AUTO_SCHEDULE_H

/** \file
 *
 * Defines the function that does automatic scheduling for a pipeline
*/

#include "IR.h"
#include "RealizationOrder.h"
#include "FindCalls.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Target.h"
#include "Function.h"
#include "Bounds.h"
#include "Var.h"
#include "IRPrinter.h"
#include "Func.h"
#include "ParallelRVar.h"
#include "RegionCosts.h"

namespace Halide {

namespace Internal {

/* Determine a schedule for functions in the pipeline */
string generate_schedules(const std::vector<Function> &outputs,
                          const Target &target, const MachineParams &arch_params);
}
}

#endif
