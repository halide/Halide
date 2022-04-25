#include "DeviceArgument.h"
#include "CodeGen_GPU_Dev.h"
#include "IRPrinter.h"

namespace Halide {
namespace Internal {

std::vector<DeviceArgument> HostClosure::arguments() {
    if (debug::debug_level() >= 2) {
        debug(2) << *this;
    }

    std::vector<DeviceArgument> res;
    for (const auto &v : vars) {
        res.emplace_back(v.first, false, MemoryType::Auto, v.second, 0);
    }
    for (const auto &b : buffers) {
        DeviceArgument arg(b.first, true, b.second.memory_type, b.second.type, b.second.dimensions, b.second.size);
        arg.read = b.second.read;
        arg.write = b.second.write;
        res.push_back(arg);
    }
    return res;
}

void HostClosure::visit(const Call *op) {
    if (op->is_intrinsic(Call::image_load) ||
        op->is_intrinsic(Call::image_store)) {

        // The argument to the call is either a StringImm or a broadcasted
        // StringImm if this is part of a vectorized expression

        const StringImm *string_imm = op->args[0].as<StringImm>();
        if (!string_imm) {
            internal_assert(op->args[0].as<Broadcast>());
            string_imm = op->args[0].as<Broadcast>()->value.as<StringImm>();
        }

        internal_assert(string_imm);

        std::string bufname = string_imm->value;
        Buffer &ref = buffers[bufname];
        ref.type = op->type;
        ref.memory_type = op->is_intrinsic(Call::image_load) ||
                                  op->is_intrinsic(Call::image_store) ?
                              MemoryType::GPUTexture :
                              MemoryType::Auto;

        if (op->is_intrinsic(Call::image_load)) {
            ref.read = true;
            ref.dimensions = (op->args.size() - 2) / 2;
        } else if (op->is_intrinsic(Call::image_store)) {
            ref.write = true;
            ref.dimensions = op->args.size() - 3;
        }

        // The Func's name and the associated .buffer are mentioned in the
        // argument lists, but don't treat them as free variables.
        ScopedBinding<> p1(ignore, bufname);
        ScopedBinding<> p2(ignore, bufname + ".buffer");
        Internal::Closure::visit(op);
    } else {
        Internal::Closure::visit(op);
    }
}

void HostClosure::visit(const For *loop) {
    if (CodeGen_GPU_Dev::is_gpu_var(loop->name)) {
        // The size of the threads and blocks is not part of the closure
        ScopedBinding<> p(ignore, loop->name);
        loop->body.accept(this);
    } else {
        Internal::Closure::visit(loop);
    }
}

}  // namespace Internal
}  // namespace Halide
