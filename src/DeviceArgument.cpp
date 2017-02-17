#include "DeviceArgument.h"
#include "CodeGen_GPU_Dev.h"

namespace Halide {
namespace Internal {

HostClosure::HostClosure(Stmt s, const std::string &loop_variable) {
    if (!loop_variable.empty()) {
        ignore.push(loop_variable, 0);
    }
    s.accept(this);
}

std::vector<DeviceArgument> HostClosure::arguments() {
    std::vector<DeviceArgument> res;
    for (const std::pair<std::string, Type> &i : vars) {
        debug(2) << "var: " << i.first << "\n";
        res.push_back(DeviceArgument(i.first, false, false, i.second, 0));
    }
    for (const std::pair<std::string, Buffer> &i : buffers) {
        debug(2) << "buffer: " << i.first << " " << i.second.size;
        if (i.second.read) debug(2) << " (read)";
        if (i.second.write) debug(2) << " (write)";
        debug(2) << "\n";

        bool is_texture = textures.find(i.first) != textures.end();
        DeviceArgument arg(i.first, true, is_texture, i.second.type, i.second.dimensions, i.second.size);
        arg.read = i.second.read;
        arg.write = i.second.write;
        res.push_back(arg);
    }
    return res;
}

void HostClosure::visit(const Call *op) {
    if (op->is_intrinsic(Call::glsl_texture_load) ||
        op->is_intrinsic(Call::image_load) ||
        op->is_intrinsic(Call::glsl_texture_store) ||
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

        if (op->name == Call::glsl_texture_load ||
            op->name == Call::image_load) {
            ref.read = true;
            ref.dimensions = (op->args.size() - 2) / 2;
        } else if (op->name == Call::glsl_texture_store ||
                   op->name == Call::image_store) {
            ref.write = true;
            ref.dimensions = op->args.size() - 3;
        }

        // The Func's name and the associated .buffer are mentioned in the
        // argument lists, but don't treat them as free variables.
        ignore.push(bufname, 0);
        ignore.push(bufname + ".buffer", 0);
        Internal::Closure::visit(op);
        ignore.pop(bufname + ".buffer");
        ignore.pop(bufname);
        textures.insert(bufname);
    } else {
        Internal::Closure::visit(op);
    }
}

void HostClosure::visit(const For *loop) {
    if (CodeGen_GPU_Dev::is_gpu_var(loop->name)) {
        // The size of the threads and blocks is not part of the closure
        ignore.push(loop->name, 0);
        loop->body.accept(this);
        ignore.pop(loop->name);
    } else {
        Internal::Closure::visit(loop);
    }
}

}}
