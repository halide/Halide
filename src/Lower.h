#ifndef HALIDE_INTERNAL_LOWER_H
#define HALIDE_INTERNAL_LOWER_H

/** \file
 *
 * Defines the function that generates a statement that computes a
 * Halide function using its schedule.
 */

#include "IR.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Given a halide function with a schedule, create a statement that
 * evaluates it. Automatically pulls in all the functions f depends
 * on. Some stages of lowering may be target-specific. */
EXPORT Stmt lower(Function f, const Target &t);

void lower_test();

}
}

#endif
