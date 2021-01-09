#ifndef HALIDE_DEVICEAPI_H
#define HALIDE_DEVICEAPI_H

/** \file
 * Defines DeviceAPI.
 */

#include <string>
#include <vector>

namespace Halide {

/** An enum describing a type of device API. Used by schedules, and in
 * the For loop IR node. */
enum class DeviceAPI {
    None,  /// Used to denote for loops that run on the same device as the containing code.
    Host,
    Default_GPU,
    CUDA,
    OpenCL,
    OpenGLCompute,
    Metal,
    Hexagon,
    HexagonDma,
    D3D12Compute,
};

/** An array containing all the device apis. Useful for iterating
 * through them. */
const DeviceAPI all_device_apis[] = {DeviceAPI::None,
                                     DeviceAPI::Host,
                                     DeviceAPI::Default_GPU,
                                     DeviceAPI::CUDA,
                                     DeviceAPI::OpenCL,
                                     DeviceAPI::OpenGLCompute,
                                     DeviceAPI::Metal,
                                     DeviceAPI::Hexagon,
                                     DeviceAPI::HexagonDma,
                                     DeviceAPI::D3D12Compute};

}  // namespace Halide

#endif  // HALIDE_DEVICEAPI_H
