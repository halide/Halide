#ifndef HALIDE_EARLY_FREE_H
#define HALIDE_EARLY_FREE_H

/** \file
 * Defines the lowering pass that injects markers just after
 * the last use of each buffer so that they can potentially be freed
 * earlier.
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** Take a statement with allocations and inject markers (of the form
 * of calls to "mark buffer dead") after the last use of each
 * allocation. Targets may use this to free buffers earlier than the
 * close of their Allocate node. */
Stmt inject_early_frees(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
