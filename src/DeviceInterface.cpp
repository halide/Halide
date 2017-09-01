#include "DeviceInterface.h"
#include "JITModule.h"
#include "Target.h"

using namespace Halide;
using namespace Halide::Internal;

namespace Halide {

namespace {

template <typename fn_type>
bool lookup_runtime_routine(const std::string &name,
                            const Target &target,
                            fn_type &result) {
    std::vector<JITModule> runtime(
        JITSharedRuntime::get(nullptr, target.with_feature(Target::JIT)));

    for (size_t i = 0; i < runtime.size(); i++) {
        std::map<std::string, JITModule::Symbol>::const_iterator f =
          runtime[i].exports().find(name);
        if (f != runtime[i].exports().end()) {
            result = reinterpret_bits<fn_type>(f->second.address);
            return true;
        }
    }
    return false;
}

}

const halide_device_interface_t *get_default_device_interface_for_target(const Target &t) {
    return get_device_interface_for_device_api(DeviceAPI::Default_GPU, t);
}

const halide_device_interface_t *get_device_interface_for_device_api(const DeviceAPI &d, const Target &t) {
    if (d == DeviceAPI::Default_GPU) {
        return get_device_interface_for_device_api(get_default_device_api_for_target(t), t);
    }

    const struct halide_device_interface_t *(*fn)();
    std::string name;
    if (d == DeviceAPI::Metal) {
        name = "metal";
    } else if (d == DeviceAPI::OpenCL) {
        name = "opencl";
    } else if (d == DeviceAPI::CUDA) {
        name = "cuda";
    } else if (d == DeviceAPI::OpenGLCompute) {
        name = "openglcompute";
    } else if (d == DeviceAPI::GLSL) {
        name = "opengl";
    } else {
        return nullptr;
    }

    if (lookup_runtime_routine("halide_" + name + "_device_interface", t, fn)) {
        return (*fn)();
    } else {
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
    } else if (target.has_feature(Target::OpenGLCompute)) {
        return DeviceAPI::OpenGLCompute;
    } else if (target.has_feature(Target::OpenGL)) {
        return DeviceAPI::GLSL;
    } else {
        return DeviceAPI::Host;
    }
}
}
