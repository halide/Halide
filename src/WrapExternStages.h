#ifndef HALIDE_WRAP_EXTERN_STAGES_H
#define HALIDE_WRAP_EXTERN_STAGES_H

#include "Module.h"

/** \file
 *
 * Defines a pass over a Module that adds wrapper LoweredFuncs to any
 * extern stages that need them */

namespace Halide {
namespace Internal {

/** Add wrappers for any LoweredFuncs that need them to support
 * backwards compatibility. This currently wraps extern calls to
 * stages that expect the old buffer_t type. */
void wrap_legacy_extern_stages(Module m);

/** Add a wrapper for a LoweredFunc that accepts old buffers and
 * upgrades them. */
void add_legacy_wrapper(Module m, const LoweredFunc &fn);

}  // namespace Internal
}  // namespace Halide

#endif
