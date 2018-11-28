#ifndef HALIDE_CODEGEN_PTX_DEV_H
#define HALIDE_CODEGEN_PTX_DEV_H

/** \file
 * Defines the code-generator for producing CUDA host code
 */

#include "CodeGen_GPU_Dev.h"
#include "CodeGen_GPU_Host.h"
#include "CodeGen_LLVM.h"

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
    ~CodeGen_PTX_Dev() override;

    void add_kernel(Stmt stmt,
                    const std::string &name,
                    const std::vector<DeviceArgument> &args) override;

    static void test();

    std::vector<char> compile_to_src() override;
    std::string get_current_kernel_name() override;

    void dump() override;

    std::string print_gpu_name(const std::string &name) override;

    std::string api_unique_name() override { return "cuda"; }

protected:
    using CodeGen_LLVM::visit;

    /** (Re)initialize the PTX module. This is separate from compile, since
     * a PTX device module will often have many kernels compiled into it for
     * a single pipeline. */
    /* override */ void init_module() override;

    /** We hold onto the basic block at the start of the device
     * function in order to inject allocas */
    llvm::BasicBlock *entry_block;

    /** Nodes for which we need to override default behavior for the GPU runtime */
    // @{
    virtual void visit(const Call *) override;
    void visit(const For *) override;
    void visit(const Allocate *) override;
    void visit(const Free *) override;
    void visit(const AssertStmt *) override;
    void visit(const Load *) override;
    void visit(const Store *) override;
    // @}

    std::string march() const;
    std::string mcpu() const override;
    std::string mattrs()  const override;
    bool use_soft_float_abi()  const override;
    int native_vector_bits()  const override;
    bool promote_indices()  const override {return false;}

    /** Map from simt variable names (e.g. foo.__block_id_x) to the llvm
     * ptx intrinsic functions to call to get them. */
    std::string simt_intrinsic(const std::string &name);
};

}  // namespace Internal
}  // namespace Halide

#endif
