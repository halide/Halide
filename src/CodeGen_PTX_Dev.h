#ifndef HALIDE_CODEGEN_PTX_DEV_H
#define HALIDE_CODEGEN_PTX_DEV_H

/** \file
 * Defines the code-generator for producing CUDA host code
 */

#include "CodeGen_LLVM.h"
#include "CodeGen_GPU_Host.h"
#include "CodeGen_GPU_Dev.h"

namespace llvm {
class BasicBlock;
}

namespace Halide {
namespace Internal {

/** A code generator that emits GPU code from a given Halide stmt. */
class CodeGen_PTX_Dev : public CodeGen_LLVM, public CodeGen_GPU_Dev {
public:
    friend class CodeGen_GPU_Host<CodeGen_X86>;
    friend class CodeGen_GPU_Host<CodeGen_ARM>;

    /** Create a PTX device code generator. */
    CodeGen_PTX_Dev(Target host);
    ~CodeGen_PTX_Dev();

    void add_kernel(Stmt stmt,
                    const std::string &name,
                    const std::vector<DeviceArgument> &args);

    static void test();

    std::vector<char> compile_to_src();
    std::string get_current_kernel_name();

    void dump();

    virtual std::string print_gpu_name(const std::string &name);

    std::string api_unique_name() { return "cuda"; }

protected:
    using CodeGen_LLVM::visit;

    /** (Re)initialize the PTX module. This is separate from compile, since
     * a PTX device module will often have many kernels compiled into it for
     * a single pipeline. */
    /* override */ virtual void init_module();

    /** We hold onto the basic block at the start of the device
     * function in order to inject allocas */
    llvm::BasicBlock *entry_block;

    /** Nodes for which we need to override default behavior for the GPU runtime */
    // @{
    void visit(const For *);
    void visit(const Allocate *);
    void visit(const Free *);
    void visit(const AssertStmt *);
    void visit(const Load *);
    void visit(const Store *);
    // @}

    std::string march() const;
    std::string mcpu() const;
    std::string mattrs() const;
    bool use_soft_float_abi() const;
    int native_vector_bits() const;
    bool promote_indices() const {return false;}

    /** Map from simt variable names (e.g. foo.__block_id_x) to the llvm
     * ptx intrinsic functions to call to get them. */
    std::string simt_intrinsic(const std::string &name);
};

}}

#endif
