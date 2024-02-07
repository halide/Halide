#include "DeviceInterface.h"
#include "IR.h"
#include "IROperator.h"
#include "JITModule.h"
#include "Target.h"
#include "runtime/HalideBuffer.h"

using namespace Halide;
using namespace Halide::Internal;

namespace Halide {

namespace {

template<typename fn_type>
bool lookup_runtime_routine(const std::string &name,
                            const Target &target,
                            fn_type &result) {
    std::vector<JITModule> runtime(
        JITSharedRuntime::get(nullptr, target.with_feature(Target::JIT)));

    for (const auto &module : runtime) {
        std::map<std::string, JITModule::Symbol>::const_iterator f =
            module.exports().find(name);
        if (f != module.exports().end()) {
            result = reinterpret_bits<fn_type>(f->second.address);
            return true;
        }
    }
    return false;
}

}  // namespace

bool host_supports_target_device(const Target &t) {
    const DeviceAPI d = t.get_required_device_api();
    if (d == DeviceAPI::None) {
        // If the target requires no DeviceAPI, then
        // the host trivially supports the target device.
        return true;
    }

    const struct halide_device_interface_t *i = get_device_interface_for_device_api(d, t);
    if (!i) {
        debug(1) << "host_supports_device_api: get_device_interface_for_device_api() failed for d=" << (int)d << " t=" << t << "\n";
        return false;
    }

    Halide::Runtime::Buffer<uint8_t> temp(8, 8, 3);
    temp.fill(0);
    temp.set_host_dirty();

    Halide::JITHandlers handlers;
    handlers.custom_error = [](JITUserContext *user_context, const char *msg) {
        debug(1) << "host_supports_device_api: saw error (" << msg << ")\n";
    };
    Halide::JITHandlers old_handlers = Halide::Internal::JITSharedRuntime::set_default_handlers(handlers);

    int result = temp.copy_to_device(i);

    Halide::Internal::JITSharedRuntime::set_default_handlers(old_handlers);

    if (result != 0) {
        debug(1) << "host_supports_device_api: copy_to_device() failed for with result=" << result << " for d=" << (int)d << " t=" << t << "\n";
        return false;
    }
    return true;
}

const halide_device_interface_t *get_device_interface_for_device_api(DeviceAPI d,
                                                                     const Target &t,
                                                                     const char *error_site) {

    if (d == DeviceAPI::Default_GPU) {
        d = get_default_device_api_for_target(t);
        if (d == DeviceAPI::Host) {
            if (error_site) {
                user_error
                    << "get_device_interface_for_device_api called from "
                    << error_site
                    << " requested a default GPU but no GPU feature is specified in target ("
                    << t.to_string()
                    << ").\n";
            }
            return nullptr;
        }
    }

    const struct halide_device_interface_t *(*fn)();
    std::string name;
    if (d == DeviceAPI::Metal) {
        name = "metal";
    } else if (d == DeviceAPI::OpenCL) {
        name = "opencl";
    } else if (d == DeviceAPI::CUDA) {
        name = "cuda";
    } else if (d == DeviceAPI::Hexagon) {
        name = "hexagon";
    } else if (d == DeviceAPI::HexagonDma) {
        name = "hexagon_dma";
    } else if (d == DeviceAPI::D3D12Compute) {
        name = "d3d12compute";
    } else if (d == DeviceAPI::Vulkan) {
        name = "vulkan";
    } else if (d == DeviceAPI::WebGPU) {
        name = "webgpu";
    } else {
        if (error_site) {
            user_error
                << "get_device_interface_for_device_api called from "
                << error_site
                << " requested unknown DeviceAPI ("
                << (int)d
                << ").\n";
        }
        return nullptr;
    }

    if (!t.supports_device_api(d)) {
        if (error_site) {
            user_error
                << "get_device_interface_for_device_api called from "
                << error_site
                << " DeviceAPI ("
                << name
                << ") is not supported by target ("
                << t.to_string()
                << ").\n";
        }
        return nullptr;
    }

    if (lookup_runtime_routine("halide_" + name + "_device_interface", t, fn)) {
        return (*fn)();
    } else {
        if (error_site) {
            user_error
                << "get_device_interface_for_device_api called from "
                << error_site
                << " cannot find runtime or device interface symbol for "
                << name
                << ".\n";
        }
        return nullptr;
    }
}

DeviceAPI get_default_device_api_for_target(const Target &target) {
    if (target.has_feature(Target::Metal)) {
        return DeviceAPI::Metal;
    } else if (target.has_feature(Target::OpenCL)) {
        return DeviceAPI::OpenCL;
    } else if (target.has_feature(Target::CUDA)) {
        return DeviceAPI::CUDA;
    } else if (target.arch != Target::Hexagon && target.has_feature(Target::HVX)) {
        return DeviceAPI::Hexagon;
    } else if (target.has_feature(Target::HexagonDma)) {
        return DeviceAPI::HexagonDma;
    } else if (target.has_feature(Target::D3D12Compute)) {
        return DeviceAPI::D3D12Compute;
    } else if (target.has_feature(Target::Vulkan)) {
        return DeviceAPI::Vulkan;
    } else if (target.has_feature(Target::WebGPU)) {
        return DeviceAPI::WebGPU;
    } else {
        return DeviceAPI::Host;
    }
}

namespace Internal {
Expr make_device_interface_call(DeviceAPI device_api, MemoryType memory_type) {
    if (device_api == DeviceAPI::Host) {
        return make_zero(type_of<const halide_device_interface_t *>());
    }

    std::string interface_name;
    switch (device_api) {
    case DeviceAPI::CUDA:
        interface_name = "halide_cuda_device_interface";
        break;
    case DeviceAPI::OpenCL:
        if (memory_type == MemoryType::GPUTexture) {
            interface_name = "halide_opencl_image_device_interface";
        } else {
            interface_name = "halide_opencl_device_interface";
        }
        break;
    case DeviceAPI::Metal:
        interface_name = "halide_metal_device_interface";
        break;
    case DeviceAPI::Hexagon:
        interface_name = "halide_hexagon_device_interface";
        break;
    case DeviceAPI::HexagonDma:
        interface_name = "halide_hexagon_dma_device_interface";
        break;
    case DeviceAPI::D3D12Compute:
        interface_name = "halide_d3d12compute_device_interface";
        break;
    case DeviceAPI::Vulkan:
        interface_name = "halide_vulkan_device_interface";
        break;
    case DeviceAPI::WebGPU:
        interface_name = "halide_webgpu_device_interface";
        break;
    case DeviceAPI::Default_GPU:
        // Will be resolved later
        interface_name = "halide_default_device_interface";
        break;
    default:
        internal_error << "Bad DeviceAPI " << static_cast<int>(device_api) << "\n";
        break;
    }
    return Call::make(type_of<const halide_device_interface_t *>(), interface_name, {}, Call::Extern);
}
}  // namespace Internal

}  // namespace Halide
