#include "SelectGPUAPI.h"
#include "DeviceInterface.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

namespace {

class SelectGPUAPI : public IRMutator {
    using IRMutator::visit;

    DeviceAPI default_api, parent_api;

    Expr visit(const Call *op) override {
        if (op->name == "halide_default_device_interface") {
            return make_device_interface_call(default_api);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const For *op) override {
        DeviceAPI selected_api = op->device_api;
        if (op->device_api == DeviceAPI::Default_GPU) {
            selected_api = default_api;
        }

        DeviceAPI old_parent_api = parent_api;
        parent_api = selected_api;
        Stmt stmt = IRMutator::visit(op);
        parent_api = old_parent_api;

        op = stmt.as<For>();
        internal_assert(op);

        if (op->device_api != selected_api) {
            return For::make(op->name, op->min, op->extent, op->for_type, op->partition_policy, selected_api, op->body);
        }
        return stmt;
    }

public:
    SelectGPUAPI(const Target &t) {
        default_api = get_default_device_api_for_target(t);
        parent_api = DeviceAPI::Host;
    }
};

}  // namespace

Stmt select_gpu_api(const Stmt &s, const Target &t) {
    return SelectGPUAPI(t).mutate(s);
}

}  // namespace Internal
}  // namespace Halide
