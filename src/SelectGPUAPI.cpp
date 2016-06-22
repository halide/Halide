#include "SelectGPUAPI.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

DeviceAPI fixup_device_api(DeviceAPI device_api, const Target &target, bool must_be_compute) {
    if (device_api == DeviceAPI::Default_GPU) {
        if (target.has_feature(Target::Metal)) {
            return target.has_feature(Target::Textures) ? DeviceAPI::MetalTextures : DeviceAPI::Metal;
        } else if (target.has_feature(Target::OpenCL)) {
            return target.has_feature(Target::Textures) ? DeviceAPI::OpenCLTextures : DeviceAPI::OpenCL;
        } else if (target.has_feature(Target::CUDA)) {
            return DeviceAPI::CUDA;
        } else if (target.has_feature(Target::OpenGLCompute)) {
            return DeviceAPI::OpenGLCompute;
        } else if (must_be_compute) {
            user_error << "Schedule uses Default_GPU without a valid GPU (Metal, OpenCL or CUDA) specified in target.\n";
        } else if (target.has_feature(Target::OpenGLCompute)) {
            return DeviceAPI::OpenGLCompute;
        } else if (target.has_feature(Target::Renderscript)) {
            return DeviceAPI::Renderscript;
        } else if (target.has_feature(Target::OpenGL)) {
            return DeviceAPI::GLSL;
        } else {
            return DeviceAPI::Host;
        }
    }
    return device_api;
}

class SelectGPUAPI : public IRMutator {
    using IRMutator::visit;

    Target target;

    DeviceAPI default_api, parent_api;

    void visit(const For *op) {
        DeviceAPI selected_api = op->device_api;
        if (op->device_api == DeviceAPI::Default_GPU) {
            selected_api = default_api;
        }

        DeviceAPI old_parent_api = parent_api;
        parent_api = selected_api;
        IRMutator::visit(op);
        parent_api = old_parent_api;

        op = stmt.as<For>();
        internal_assert(op);

        if (op->device_api != selected_api) {
            stmt = For::make(op->name, op->min, op->extent, op->for_type, selected_api, op->body);
        }
    }
public:
    SelectGPUAPI(Target t) : target(t) {
        default_api = fixup_device_api(DeviceAPI::Default_GPU, target);
        parent_api = DeviceAPI::Host;
    };
};

Stmt select_gpu_api(Stmt s, Target t) {
    return SelectGPUAPI(t).mutate(s);
}

}
}
