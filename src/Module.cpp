#include "Module.h"
#include "Debug.h"

namespace Halide {

namespace Internal {

std::ostream &operator << (std::ostream &stream, const FunctionDecl &function) {
    stream << function.linkage << " func " << function.name << " (";
    for (size_t i = 0; i < function.args.size(); i++) {
        stream << function.args[i].name;
        if (i + 1 < function.args.size()) {
            stream << ", ";
        }
    }
    stream << ") {\n";
    stream << function.body;
    stream << "}\n\n";
    return stream;
}

std::ostream &operator << (std::ostream &stream, const BufferDecl &buffer) {
    return stream << "buffer " << buffer.buffer.name() << " = {...}\n";
}

std::ostream &operator<<(std::ostream &out, const FunctionDecl::LinkageType &type) {
    switch (type) {
    case FunctionDecl::External:
        out << "external";
        break;
    case FunctionDecl::Internal:
        out << "internal";
        break;
    }
    return out;
}

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
        for (size_t i = 0; i < input.buffers.size(); i++) {
            output.append(input.buffers[i]);
        }
        for (size_t i = 0; i < input.functions.size(); i++) {
            output.append(input.functions[i]);
        }
    }

    return output;
}

}
