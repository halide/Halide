#ifndef HALIDE_CODEGEN_PTX_HOST_H
#define HALIDE_CODEGEN_PTX_HOST_H

/** \file
 * Defines the code-generator for producing CUDA host code
 */

#include "CodeGen_X86.h"
#include "CodeGen_PTX_Dev.h"

namespace Halide { 
namespace Internal {

/** A code generator that emits GPU code from a given Halide stmt. */
class CodeGen_PTX_Host : public CodeGen_X86 {
public:

    /** Create an x86 code generator. Processor features can be
     * enabled using the appropriate flags from CodeGen_X86_Options */
    CodeGen_PTX_Host(uint32_t options = 0);
        
    /** Compile to an internally-held llvm module. Takes a halide
     * statement, the name of the function produced, and the arguments
     * to the function produced. After calling this, call
     * CodeGen::compile_to_file or
     * CodeGen::compile_to_function_pointer to get at the x86 machine
     * code. */
    void compile(Stmt stmt, std::string name, const std::vector<Argument> &args);

protected:
    using CodeGen_X86::visit;

    class Closure;

    /** Nodes for which we need to override default behavior for the GPU runtime */
    // @{
    void visit(const For *);
    void visit(const Allocate *);
    void visit(const Pipeline *);
    void visit(const Call *);
    // @}

    // We track buffer_t's for each allocation in order to manage dirty bits
    bool track_buffers() {return true;}
    
    //** Runtime function handles */
    // @{
    llvm::Function *dev_malloc_fn;
    llvm::Function *dev_free_fn;
    llvm::Function *copy_to_dev_fn;
    llvm::Function *copy_to_host_fn;
    llvm::Function *dev_run_fn;
    llvm::Function *dev_sync_fn;
    // @}

    /** Finds and links in the CUDA runtime symbols prior to jitting */
    void jit_init(llvm::ExecutionEngine *ee, llvm::Module *mod);

    /** Reaches inside the module at sets it to use a single shared
     * cuda context */
    void jit_finalize(llvm::ExecutionEngine *ee, llvm::Module *mod);

    static bool lib_cuda_linked;

private:
    /** Child code generator for device kernels. */
    CodeGen_PTX_Dev cgdev;
};

}}

#endif
