#ifndef HALIDE_PYTHON_EXTENSION_GEN_H_
#define HALIDE_PYTHON_EXTENSION_GEN_H_

#include <iostream>

namespace Halide {

class Module;

namespace Internal {

class PythonExtensionGen {
public:
    PythonExtensionGen(std::ostream &dest);

    void compile(const Module &module);

private:
    std::ostream &dest;
};

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_PYTHON_EXTENSION_GEN_H_
