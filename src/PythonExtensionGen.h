#ifndef HALIDE_PYTHON_EXTENSION_GEN_H_
#define HALIDE_PYTHON_EXTENSION_GEN_H_

#include "Target.h"

namespace Halide {

class Module;
struct Target;

namespace Internal {

class PythonExtensionGen {
public:
    PythonExtensionGen(std::ostream &dest, const std::string &header_name, Target target);

    void compile(const Module &module);
private:
    std::ostream &dest;
    std::string header_name;
    Target target;
};

}
}

#endif // HALIDE_PYTHON_EXTENSION_GEN_H_
