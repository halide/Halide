// Verify the host/runtime ABI for logical Bool scalar arguments to Vulkan kernels.
// Vulkan Uniform storage cannot represent OpTypeBool, so the compiler must pass
// four-byte UInt32 storage and a matching runtime argument size. This test inspects lowered IR
// without launching the Vulkan runtime or requiring a Vulkan device.
// Forcing the Vulkan target instead of inheriting HL_TARGET is intentional: this
// follows the compiler-only test pattern of exercising a specific backend in any
// CI configuration, while backend runtime tests require and self-select a device.

#include "Halide.h"

#include <cstdio>

using namespace Halide;
using namespace Halide::Internal;

namespace {

class CheckVulkanBoolABI : public IRVisitor {
public:
    using IRVisitor::visit;

    void visit(const Call *op) override {
        if (op->name != "halide_vulkan_run") {
            IRVisitor::visit(op);
            return;
        }

        if (op->args.size() != 12) {
            return;
        }
        const Call *arg_sizes = op->args[9].as<Call>();
        const Call *args = op->args[10].as<Call>();
        const Call *arg_is_buffer = op->args[11].as<Call>();
        if (!arg_sizes || !args || !arg_is_buffer ||
            arg_sizes->args.size() != args->args.size() ||
            args->args.size() != arg_is_buffer->args.size()) {
            return;
        }

        for (size_t i = 0; i < args->args.size(); i++) {
            const Call *storage = args->args[i].as<Call>();
            const auto is_buffer = as_const_uint(arg_is_buffer->args[i]);
            if (storage && storage->args.size() == 1 && is_buffer && *is_buffer == 0) {
                found = true;
                const auto size = as_const_int(arg_sizes->args[i]);
                valid = storage->args[0].type() == UInt(32) && size && *size == 4;
            }
        }
    }

    bool found = false;
    bool valid = false;
};

}  // namespace

int main(int argc, char **argv) {
    Target target = get_host_target().with_feature(Target::Vulkan);
    Var x("x"), xi("xi");
    Param<bool> enabled("enabled");

    Func output("output");
    output(x) = cast<uint32_t>(enabled && x >= 32);
    output.gpu_tile(x, xi, 8);

    Module module = output.compile_to_module(output.infer_arguments(), "vulkan_bool_scalar_abi", target);
    CheckVulkanBoolABI check;
    for (const LoweredFunc &function : module.functions()) {
        function.body.accept(&check);
    }

    if (!check.found || !check.valid) {
        printf("Vulkan Bool scalar was not passed as a four-byte UInt32\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}