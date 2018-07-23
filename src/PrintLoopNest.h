#ifndef HALIDE_INTERNAL_PRINT_LOOP_NEST_H
#define HALIDE_INTERNAL_PRINT_LOOP_NEST_H

/** \file
 *
 * Defines methods to print out the loop nest corresponding to a schedule.
 */

#include <string>
#include <vector>

namespace Halide {
namespace Internal {

class Function;

/** Emit some simple pseudocode that shows the structure of the loop
 * nest specified by this pipeline's schedule, and the schedules of
 * the functions it uses. */
std::string print_loop_nest(const std::vector<Function> &output_funcs);

}  // namespace Internal
}  // namespace Halide

#endif
