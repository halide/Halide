#include "SelectGPUAPI.h"
#include "IRMutator.h"
#include "DeviceInterface.h"

namespace Halide {
namespace Internal {

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
        default_api = get_default_device_api_for_target(t);
        parent_api = DeviceAPI::Host;
    };
};

Stmt select_gpu_api(Stmt s, Target t) {
    return SelectGPUAPI(t).mutate(s);
}

}
}
