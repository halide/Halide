#ifndef HALIDE_PYTHON_EXTENSION_GEN_H_
#define HALIDE_PYTHON_EXTENSION_GEN_H_

#include <iostream>
#include <string>
#include <vector>

namespace Halide {

class Module;

namespace Internal {

struct LoweredArgument;
struct LoweredFunc;

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
