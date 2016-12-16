#ifndef HALIDE_DEVICE_INTERFACE_H
#define HALIDE_DEVICE_INTERFACE_H

/** \file
 * Methods for managing device allocations when jitting
 */

#include "Target.h"

namespace Halide {

/** Get the appropriate halide_device_interface_t * for a
 * target. Corresponds to the device interface that would be used for
 * DeviceAPI::Default_GPU. Creates a GPU runtime module for the target
 * if necessary. Returns nullptr if no device APIs are enabled in the
 * target. */
halide_device_interface_t *get_default_device_interface_for_target(const Target &t);

/** Gets the appropriate halide_device_interface_t * for a
 * DeviceAPI. Asserts that the argument is not Default_GPU, None, or
 * Host. */
halide_device_interface_t *get_device_interface_for_device_api(const DeviceAPI &d);

/** Get the specific DeviceAPI that Halide would select when presented
 * with DeviceAPI::Default_GPU for a given target. If no suitable api
 * is enabled in the target, returns DeviceAPI::Host. */
DeviceAPI get_default_device_api_for_target(const Target &t);

}

#endif
