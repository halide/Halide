#ifndef HALIDE_PROFILING_H
#define HALIDE_PROFILING_H

/** \file
 * Defines the lowering pass that injects print statements when profiling is turned on
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Take a statement representing a halide pipeline, and (depending on
 * the environment variable HL_PROFILE), insert high-resolution timing
 * into the generated code; summaries of execution times and counts will
 * be logged at the end. Should be done before storage flattening, but
 * after all bounds inference. Use util/HalideProf to analyze the output.
 *
 * NOTE: this makes no effort to provide accurate or useful information
 * when parallelization is scheduled; more work would need to be done
 * to safely record data from multiple threads.
 *
 * NOTE: this makes no effort to account for overhead from the profiling
 * instructions inserted; profile-enabled runtimes will be slower,
 * and inner loops will be more profoundly affected.
 */
Stmt inject_profiling(Stmt, std::string);

/** Gets the current profiling level (by reading HL_PROFILE) */
int profiling_level();

}
}

#endif
