#ifndef HALIDE_REMOVE_EXTERN_LOOPS
#define HALIDE_REMOVE_EXTERN_LOOPS

#include "IR.h"

/** \file
 * Defines a lowering pass that elides stores that depend on unitialized values.
 */

namespace Halide {
namespace Internal {

/** Removes stores that depend on undef values, and statements that
 * only contain such stores. */
Stmt remove_extern_loops(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
