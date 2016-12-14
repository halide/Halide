#ifndef HALIDE_DEVICE_INTERFACE_H
#define HALIDE_DEVICE_INTERFACE_H

/** \file
 * Methods for managing device allocations when jitting
 */

#include "Target.h"

namespace Halide {

/** Get the appropriate halide_device_interface_t * for a target. The
 * target should have a single gpu api enabled. Creates a GPU runtime
 * module for the target if necessary. */
halide_device_interface_t *get_device_interface_for_target(const Target &t);

}

#endif
