#ifndef HALIDE_DEVICE_INTERFACE_H
#define HALIDE_DEVICE_INTERFACE_H

/** \file
 * Methods for managing device allocations when jitting
 */

#include "Target.h"

namespace Halide {

/** Gets the appropriate halide_device_interface_t * for a
 * DeviceAPI. If error_site is non-null, e.g. the name of the routine
 * calling get_device_interface_for_device_api, a user_error is
 * reported if the requested device API is not enabled in or supported
 * by the target, Halide has been compiled without this device API, or
 * the device API is None or Host or a bad value. The error_site
 * argument is printed in the error message. If error_site is null,
 * this routine returns nullptr instead of calling user_error. */
const halide_device_interface_t *get_device_interface_for_device_api(DeviceAPI d,
                                                                     const Target &t = get_jit_target_from_environment(),
                                                                     const char *error_site = nullptr);

/** Get the specific DeviceAPI that Halide would select when presented
 * with DeviceAPI::Default_GPU for a given target. If no suitable api
 * is enabled in the target, returns DeviceAPI::Host. */
DeviceAPI get_default_device_api_for_target(const Target &t);

/** This attempts to sniff whether a given Target (and its implied DeviceAPI) is usable on
 * the current host. If it appears to be usable, return true; if not, return false.
 * Note that a return value of true does *not* guarantee that future usage of
 * that device will succeed; it is intended mainly as a simple diagnostic
 * to allow early-exit when a desired device is definitely not usable.
 * Also note that this call is *NOT* threadsafe, as it temporarily redirect various
 * global error-handling hooks in Halide. */
bool host_supports_target_device(const Target &t);

namespace Internal {
/** Get an Expr which evaluates to the device interface for the given device api at runtime. */
Expr make_device_interface_call(DeviceAPI device_api);
}  // namespace Internal

}  // namespace Halide

#endif
