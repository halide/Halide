#ifndef HALIDE_PYTHON_EXTENSION_GEN_H_
#define HALIDE_PYTHON_EXTENSION_GEN_H_

#include <string>
#include "Module.h"
#include "Target.h"

namespace Halide {

class Module;
struct Target;

namespace Internal {

class PythonExtensionGen {
public:
    PythonExtensionGen(std::ostream &dest);

    void compile(const Module &module);

private:
    std::ostream &dest;

    void compile(const LoweredFunc &f);
    void convert_buffer(std::string name, const LoweredArgument* arg);
};

}
}

#endif // HALIDE_PYTHON_EXTENSION_GEN_H_
