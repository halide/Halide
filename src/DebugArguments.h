#ifndef HALIDE_INTERNAL_DEBUG_ARGUMENTS_H
#define HALIDE_INTERNAL_DEBUG_ARGUMENTS_H

/** \file
 * 
 * Defines a lowering pass that injects debug statements inside a
 * LoweredFunc. Intended to be used when Target::Debug is on. 
 */

namespace Halide {
namespace Internal {

struct LoweredFunc;

/** Injects debug prints in a LoweredFunc that describe the arguments. Mutates the given func. */
void debug_arguments(LoweredFunc *func);

}
}


#endif
