#ifndef HALIDE_CODEGEN_SIMPLE_OPENCL_H
#define HALIDE_CODEGEN_SIMPLE_OPENCL_H

/** \file
 *
 * Defines an IRPrinter that emits OpenCL & C++ code equivalent to a halide stmt
 */

#include "CodeGen_C.h"
#include "CodeGen_GPU_Dev.h"
#include "Target.h"

#include <map>

namespace Halide {
namespace Internal {

/** This class emits C++ code equivalent to a halide Stmt. It's
 * mostly the same as an IRPrinter, but it's wrapped in a function
 * definition, and some things are handled differently to be valid
 * C++.
 */
class CodeGen_Simple_OpenCL : public CodeGen_C {
    using CodeGen_C::visit;

public:
    /** Initialize a simple OpenCL code generator pointing at a particular output
     * stream (e.g. a file, or std::cout) */
    CodeGen_Simple_OpenCL(std::ostream &dest,
                          const Target &target,
                          OutputKind output_kind = CImplementation);
    // ~CodeGen_Simple_OpenCL() override;
    using CodeGen_C::compile;
    void compile(const Module &input) override;

protected:
    void visit(const For *) override;

    void compile(const LoweredFunc &func) override;

    /** Child code generator for device kernels. */
    std::unique_ptr<CodeGen_GPU_Dev> cgdev;

    /*Name of the current module states indexed by API (Here only OpenCL) */
    std::map<std::string, std::string> current__api_module_state;

    std::string print_array_assignment(Type t, const std::string &rhs);
};

}  // namespace Internal
}  // namespace Halide

#endif
