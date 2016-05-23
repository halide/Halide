#ifndef HALIDE_INTERNAL_AUTO_SCHEDULE_H
#define HALIDE_INTERNAL_AUTO_SCHEDULE_H

/** \file
 *
 * Defines the function that does automatic scheduling for a pipeline
*/

#include <map>

#include "IR.h"

namespace Halide {

struct Target;

namespace Internal {

class Function;

/* Determine a schedule for functions in the pipeline */
void generate_schedules(const std::vector<Function> &outputs,
                        const Target &target);
}
}

#endif
