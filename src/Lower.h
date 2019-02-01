#ifndef HALIDE_INTERNAL_LOWER_H
#define HALIDE_INTERNAL_LOWER_H

/** \file
 *
 * Defines the function that generates a statement that computes a
 * Halide function using its schedule.
 */

#include <iterator>

#include "Argument.h"
#include "IR.h"
#include "Module.h"
#include "Target.h"

namespace Halide {
namespace Internal {

class IRMutator;

/** Given a vector of scheduled halide functions, create a Module that
 * evaluates it. Automatically pulls in all the functions f depends
 * on. Some stages of lowering may be target-specific. The Module may
 * contain submodules for computation offloaded to another execution
 * engine or API as well as buffers that are used in the passed in
 * Stmt. Multiple LoweredFuncs are added to support legacy buffer_t
 * calling convention. */
Module lower(const std::vector<Function> &output_funcs, const std::string &pipeline_name, const Target &t,
                    const std::vector<Argument> &args, const LinkageType linkage_type,
                    const std::vector<IRMutator *> &custom_passes = std::vector<IRMutator *>());

/** Given a halide function with a schedule, create a statement that
 * evaluates it. Automatically pulls in all the functions f depends
 * on. Some stages of lowering may be target-specific. Mostly used as
 * a convenience function in tests that wish to assert some property
 * of the lowered IR. */
Stmt lower_main_stmt(const std::vector<Function> &output_funcs, const std::string &pipeline_name, const Target &t,
                            const std::vector<IRMutator *> &custom_passes = std::vector<IRMutator *>());

void lower_test();

}  // namespace Internal
}  // namespace Halide

#endif
