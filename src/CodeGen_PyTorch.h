#ifndef HALIDE_CODEGEN_PYTORCH_H
#define HALIDE_CODEGEN_PYTORCH_H

/** \file
 *
 * 
 */

#include "IRPrinter.h"
#include "Module.h"
#include "Scope.h"

namespace Halide {

struct Argument;

namespace Internal {

/** 
 * 
 */
class CodeGen_PyTorch : public IRPrinter{
public:
    enum OutputKind {
        // PyTorchHeader,
        PyTorchImplementation,
    };

    CodeGen_PyTorch(
        std::ostream &dest, Target target,
        OutputKind output_kind, std::string name);
    ~CodeGen_PyTorch();

    /** Emit the declarations contained in the module as C code. */
    void compile(const Module &module);

    /** The target we're generating code for */
    const Target &get_target() const { return target; }

    // EXPORT static void test();

protected:
    virtual void compile(const LoweredFunc &func, bool isCuda);
    virtual std::string print_name(const std::string &);

    /** The target being generated for. */
    Target target;

    /** Controls whether this instance is generating declarations or
     * definitions. */
    OutputKind output_kind;

    std::string cpp_header;
};

}
}

#endif
