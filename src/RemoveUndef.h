#ifndef HALIDE_REMOVE_UNDEF
#define HALIDE_REMOVE_UNDEF

#include "IR.h"

/** \file
 * Defines a lowering pass that elides stores that depend on unitialized values.
 */

namespace Halide {
namespace Internal {

/** Removes stores that depend on undef values, and statements that
 * only contain such stores. */
Stmt remove_undef(Stmt s);

}  // namespace Internal
}  // namespace Halide

#endif
