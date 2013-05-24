#ifndef HALIDE_TRACING_H
#define HALIDE_TRACING_H

/** \file
 * Defines the lowering pass that injects print statements when tracing is turned on 
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Take a statement representing a halide pipeline, and (depending on
 * the environment variable HL_TRACE), inject print statements at
 * interesting points, such as allocations. Should be done before
 * storage flattening, but after all bounds inference. */
Stmt inject_tracing(Stmt);

/** Gets the current tracing level (by reading HL_TRACE) */
int tracing_level();

}
}

#endif
