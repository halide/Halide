#ifndef HALIDE_PROFILING_H
#define HALIDE_PROFILING_H

/** \file
 * Defines the lowering pass that injects print statements when profiling is turned on
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Take a statement representing a halide pipeline insert
 * high-resolution timing into the generated code (via spawning a
 * thread that acts as a sampling profiler); summaries of execution
 * times and counts will be logged at the end. Should be done before
 * storage flattening, but after all bounds inference.
 *
 */
Stmt inject_profiling(Stmt, std::string);

}
}

#endif
