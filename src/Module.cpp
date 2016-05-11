#include "Module.h"
#include "Debug.h"

namespace Halide {

namespace Internal {
struct ModuleContents {
    mutable RefCount ref_count;
    std::string name;
    Target target;
    std::vector<Buffer> buffers;
    std::vector<Internal::LoweredFunc> functions;
};

template<>
EXPORT RefCount &ref_count<ModuleContents>(const ModuleContents *f) {
    return f->ref_count;
}

template<>
EXPORT void destroy<ModuleContents>(const ModuleContents *f) {
    delete f;
}
}

Module::Module(const std::string &name, const Target &target) :
    contents(new Internal::ModuleContents) {
    contents.ptr->name = name;
    contents.ptr->target = target;
}

const Target &Module::target() const {
    return contents.ptr->target;
}

const std::string &Module::name() const {
    return contents.ptr->name;
}

const std::vector<Buffer> &Module::buffers() const {
    return contents.ptr->buffers;
}

const std::vector<Internal::LoweredFunc> &Module::functions() const {
    return contents.ptr->functions;
}

void Module::append(const Buffer &buffer) {
    contents.ptr->buffers.push_back(buffer);
}

void Module::append(const Internal::LoweredFunc &function) {
    contents.ptr->functions.push_back(function);
}

Module link_modules(const std::string &name, const std::vector<Module> &modules) {
    Module output(name, modules.front().target());

    for (size_t i = 0; i < modules.size(); i++) {
        const Module &input = modules[i];

        if (output.target() != input.target()) {
            user_error << "Mismatched targets in modules to link ("
                       << output.name() << ", " << output.target().to_string()
                       << "), ("
                       << input.name() << ", " << input.target().to_string() << ")\n";
        }

        // TODO(dsharlet): Check for naming collisions, maybe rename
        // internal linkage declarations in the case of collision.
        for (const auto &b : input.buffers()) {
            output.append(b);
        }
        for (const auto &f : input.functions()) {
            output.append(f);
        }
    }

    return output;
}

}
