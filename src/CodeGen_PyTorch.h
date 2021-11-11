#ifndef HALIDE_CODEGEN_PYTORCH_H
#define HALIDE_CODEGEN_PYTORCH_H

/** \file
 *
 * Defines an IRPrinter that emits C++ code that:
 * 1. wraps PyTorch's C++ tensor into Halide * buffers,
 * 2. calls the corresponding Halide operator.
 * 3. maps the output buffer back to a PyTorch tensor.
 *
 * The generated code checks for runtime errors and raises PyTorch exception
 * accordingly. It also makes sure the GPU device and stream are consistent when
 * the PyTorch input, when applicable.
 */

#include "IRPrinter.h"

namespace Halide {

class Module;

namespace Internal {

struct LoweredFunc;

/** This class emits C++ code to wrap a Halide pipeline so that it can
 * be used as a C++ extension operator in PyTorch.
 */
class CodeGen_PyTorch : public IRPrinter {
public:
    CodeGen_PyTorch(std::ostream &dest);
    ~CodeGen_PyTorch() override = default;

    /** Emit the PyTorch C++ wrapper for the Halide pipeline. */
    void compile(const Module &module);

private:
    void compile(const LoweredFunc &func, bool is_cuda);
};

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_CODEGEN_PYTORCH_H
