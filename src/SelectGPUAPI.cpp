#include "SelectGPUAPI.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

class SelectGPUAPI : public IRMutator {
    using IRMutator::visit;

    Target target;

    DeviceAPI default_api, parent_api;

    void visit(const For *op) {
        DeviceAPI selected_api;
        if (op->device_api == DeviceAPI::Default_GPU) {
            selected_api = default_api;
        } else if (op->device_api == DeviceAPI::Parent) {
            selected_api = parent_api;
        } else {
            selected_api = op->device_api;
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
        if (target.has_feature(Target::OpenGL)) {
            default_api = DeviceAPI::GLSL;
        } else if (target.has_feature(Target::Renderscript)) {
            default_api = DeviceAPI::Renderscript;
        } else if (target.has_feature(Target::OpenGLCompute)) {
            default_api = DeviceAPI::OpenGLCompute;
        } else if (target.has_feature(Target::CUDA)) {
            default_api = DeviceAPI::CUDA;
        } else if (target.has_feature(Target::OpenCL)) {
            default_api = DeviceAPI::OpenCL;
        } else {
            default_api = DeviceAPI::Host;
        }
        parent_api = DeviceAPI::Host;
    };
};

Stmt select_gpu_api(Stmt s, Target t) {
    return SelectGPUAPI(t).mutate(s);
}

}
}
