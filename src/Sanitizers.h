#ifndef HALIDE_SANITIZERS_H
#define HALIDE_SANITIZERS_H

/** \file
 * Defines the lowering passes that deal with helping LLVM Sanitizers.
 */

#include "IR.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** Inject calls to MSAN helpers as needed. */
EXPORT Stmt inject_msan_helpers(Stmt s);

}
}

#endif
