#include "HalideRuntime.h"

/** \file Ahead of time compiled code reference counting support. */

/* The runtime makes calls to keep a reference count on the code
 * itself because some allocated data structures returned from the
 * runtime may contain function pointers back to the code. For ahead
 * of time compilation, this is not done because any needed reference
 * counting involves support from the hosting client application and
 * often it is not needed at all as the code is loaded for the
 * lifetime of a process. This file provides a nop implementation of
 * the use and release calls.
 */

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK void halide_use_jit_module() {
}

WEAK void halide_release_jit_module() {
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
