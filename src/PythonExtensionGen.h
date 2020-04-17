#ifndef HALIDE_PYTHON_EXTENSION_GEN_H_
#define HALIDE_PYTHON_EXTENSION_GEN_H_

#include "Module.h"
#include "Target.h"
#include <string>

namespace Halide {
namespace Internal {

class PythonExtensionGen {
public:
    PythonExtensionGen(std::ostream &dest);

    void compile(const Module &module);

private:
    std::ostream &dest;
    std::vector<std::string> buffer_refs;

    void compile(const LoweredFunc &f);
    void convert_buffer(const std::string &name, const LoweredArgument *arg);
    void release_buffers(const std::string &prefix);
};

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_PYTHON_EXTENSION_GEN_H_
