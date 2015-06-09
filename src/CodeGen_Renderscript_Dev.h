#ifndef HALIDE_CODEGEN_RENDERSCRIPT_DEV_H
#define HALIDE_CODEGEN_RENDERSCRIPT_DEV_H

/** \file
 * Defines the code-generator for producing Renderscript host code
 */

#include "CodeGen_LLVM.h"
#include "CodeGen_GPU_Host.h"
#include "CodeGen_GPU_Dev.h"

namespace llvm {
class BasicBlock;
}

namespace Halide {
namespace Internal {

/** A code generator that emits Renderscript code from a given Halide stmt. */
class CodeGen_Renderscript_Dev : public CodeGen_LLVM, public CodeGen_GPU_Dev {
public:
    friend class CodeGen_GPU_Host<CodeGen_X86>;
    friend class CodeGen_GPU_Host<CodeGen_ARM>;

    /** Create a Renderscript device code generator. */
    CodeGen_Renderscript_Dev(Target host);
    ~CodeGen_Renderscript_Dev();

    void add_kernel(Stmt stmt, const std::string &name,
                    const std::vector<GPU_Argument> &args);

    static void test();

    std::vector<char> compile_to_src();
    std::string get_current_kernel_name();

    void dump();

    virtual std::string print_gpu_name(const std::string &name);

    std::string api_unique_name() { return "renderscript"; }

protected:
    using CodeGen_LLVM::visit;

    /** (Re)initialize the Renderscript module. This is separate from compile, since
     * a Renderscript device module will often have many kernels compiled into it for
     * a single pipeline. */
    /* override */ virtual void init_module();

    /* override */ virtual llvm::Triple get_target_triple() const;
    /* override */ virtual llvm::DataLayout get_data_layout() const;

    /** We hold onto the basic block at the start of the device
     * function in order to inject allocas */
    llvm::BasicBlock *entry_block;

    /** Nodes for which we need to override default behavior for the Renderscript runtime
     */
    // @{
    void visit(const For *);
    void visit(const Allocate *);
    void visit(const Free *);
    // @}

    void visit(const Call *op);

    std::string march() const;
    std::string mcpu() const;
    std::string mattrs() const;
    bool use_soft_float_abi() const;
    int native_vector_bits() const;

    /** Map from simt variable names (e.g. foo.__block_id_x) to the coresponding
     * parameter name. */
    std::string params_mapping(const std::string &name);

    llvm::Function *fetch_GetElement_func(Type type);
    llvm::Function *fetch_SetElement_func(Type type);
    std::vector<llvm::Value *> add_x_y_c_args(Expr name, Expr x, Expr y,
                                              Expr c);
private:
    // Metadata records keep track of all Renderscript kernels.
    llvm::NamedMDNode *rs_export_foreach_name;
    llvm::NamedMDNode *rs_export_foreach;

    std::map<std::string, llvm::GlobalVariable*> rs_global_vars;
};
}
}

#endif
