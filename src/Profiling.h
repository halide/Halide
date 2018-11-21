#ifndef HALIDE_PROFILING_H
#define HALIDE_PROFILING_H

/** \file
 * Defines the lowering pass that injects print statements when profiling is turned on.
 * The profiler will print out per-pipeline and per-func stats, such as total time
 * spent and heap/stack allocation information. To turn on the profiler, set
 * HL_TARGET/HL_JIT_TARGET flags to 'host-profile'.
 *
 * Output format:
 * \<pipeline_name\>
 *  \<total time spent in this pipeline\> \<# of samples taken\> \<# of runs\> \<avg time/run\>
 *  \<# of heap allocations\> \<peak heap allocation\>
 *   \<func_name\> \<total time spent in this func\> \<percentage of time spent\>
 *     (\<peak heap alloc by this func\> \<num of allocs\> \<average alloc size\> |
 *      \<worst-case peak stack alloc by this func\>)?
 *
 * Sample output:
 * memory_profiler_mandelbrot
 *  total time: 59.832336 ms   samples: 43   runs: 1000   time/run: 0.059832 ms
 *  heap allocations: 104000   peak heap usage: 505344 bytes
 *   f0:          0.025673ms (42%)
 *   mandelbrot:  0.006444ms (10%)   peak: 505344   num: 104000   avg: 5376
 *   argmin:      0.027715ms (46%)   stack: 20
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

}  // namespace Internal
}  // namespace Halide

#endif
