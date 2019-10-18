#ifndef HALIDE_INTERNAL_DEBUG_ARGUMENTS_H
#define HALIDE_INTERNAL_DEBUG_ARGUMENTS_H

#include "Target.h"

/** \file
 *
 * Defines a lowering pass that injects debug statements inside a
 * LoweredFunc. Intended to be used when Target::Debug is on.
 */

namespace Halide {
namespace Internal {

struct LoweredFunc;

/** Injects debug prints in a LoweredFunc that describe the target and
 * arguments. Mutates the given func. */
void debug_arguments(LoweredFunc *func, const Target &t);

}  // namespace Internal
}  // namespace Halide

#endif
