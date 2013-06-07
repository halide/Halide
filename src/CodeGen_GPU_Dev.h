#ifndef HALIDE_CODEGEN_GPU_DEV_H
#define HALIDE_CODEGEN_GPU_DEV_H

/** \file
 * Defines the code-generator interface for producing GPU device code
 */

#include "Func.h"

namespace Halide { 
namespace Internal {

/** A code generator that emits GPU code from a given Halide stmt. */
struct CodeGen_GPU_Dev {
    virtual ~CodeGen_GPU_Dev();

    /** Compile a GPU kernel into the module. This may be called many times 
     * with different kernels, which will all be accumulated into a single 
     * source module shared by a given Halide pipeline. */
    virtual void compile(Stmt stmt, std::string name, const std::vector<Argument> &args) = 0;

    /** (Re)initialize the GPU kernel module. This is separate from compile,
     * since a GPU device module will often have many kernels compiled into it
     * for a single pipeline. */
    virtual void init_module() = 0;

    virtual std::string compile_to_src() = 0;

    virtual std::string get_current_kernel_name() = 0;

    virtual void dump() = 0;

    static bool is_gpu_var(const std::string &name);
};

}}

#endif
