#ifndef HALIDE_UNSAFE_PROMISES_H
#define HALIDE_UNSAFE_PROMISES_H

/** \file
 * Defines the lowering pass that removes unsafe promises
 */

#include "IR.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Lower all unsafe promises into either assertions or unchecked
    code, depending on the target. */
Stmt lower_unsafe_promises(Stmt s, const Target &t);

}  // namespace Internal
}  // namespace Halide

#endif
