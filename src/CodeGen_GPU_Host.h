#ifndef HALIDE_CODEGEN_GPU_HOST_H
#define HALIDE_CODEGEN_GPU_HOST_H

/** \file
 * Defines the code-generator for producing GPU host code
 */

#include "CodeGen_ARM.h"
#include "CodeGen_X86.h"

namespace Halide {
namespace Internal {

struct CodeGen_GPU_Dev;

/** A code generator that emits GPU code from a given Halide stmt. */
template<typename CodeGen_CPU>
class CodeGen_GPU_Host : public CodeGen_CPU {
public:

    /** Create a GPU code generator. GPU target is selected via
     * CodeGen_GPU_Options. Processor features can be enabled using the
     * appropriate flags from CodeGen_X86_Options */
    CodeGen_GPU_Host(Target);

    virtual ~CodeGen_GPU_Host();

    /** Compile to an internally-held llvm module. Takes a halide
     * statement, the name of the function produced, and the arguments
     * to the function produced. After calling this, call
     * CodeGen::compile_to_file or
     * CodeGen::compile_to_function_pointer to get at the x86 machine
     * code. */
    void compile(Stmt stmt, std::string name,
                 const std::vector<Argument> &args,
                 const std::vector<Buffer> &images_to_embed);

protected:
    /** Declare members of the base class that must exist to help the
     * compiler do name lookup. Annoying but necessary, because the
     * compiler doesn't know that CodeGen_CPU will in fact inherit
     * from CodeGen for every instantiation of this template. */
    using CodeGen_CPU::module;
    using CodeGen_CPU::init_module;
    using CodeGen_CPU::target;
    using CodeGen_CPU::builder;
    using CodeGen_CPU::context;
    using CodeGen_CPU::function;
    using CodeGen_CPU::get_user_context;
    using CodeGen_CPU::visit;
    using CodeGen_CPU::codegen;
    using CodeGen_CPU::sym_push;
    using CodeGen_CPU::sym_pop;
    using CodeGen_CPU::sym_get;
    using CodeGen_CPU::sym_exists;
    using CodeGen_CPU::buffer_dev_dirty_ptr;
    using CodeGen_CPU::buffer_host_dirty_ptr;
    using CodeGen_CPU::buffer_elem_size_ptr;
    using CodeGen_CPU::buffer_min_ptr;
    using CodeGen_CPU::buffer_stride_ptr;
    using CodeGen_CPU::buffer_extent_ptr;
    using CodeGen_CPU::buffer_host_ptr;
    using CodeGen_CPU::buffer_dev;
    using CodeGen_CPU::buffer_dev_ptr;
    using CodeGen_CPU::llvm_type_of;
    using CodeGen_CPU::create_alloca_at_entry;
    using CodeGen_CPU::codegen_allocation_size;
    using CodeGen_CPU::create_allocation;
    using CodeGen_CPU::destroy_allocation;
    using CodeGen_CPU::i8;
    using CodeGen_CPU::i32;
    using CodeGen_CPU::i64;
    using CodeGen_CPU::buffer_t_type;

    /** Nodes for which we need to override default behavior for the GPU runtime */
    // @{
    void visit(const For *);
    void visit(const Allocate *);
    void visit(const Free *);
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
    void jit_finalize(llvm::ExecutionEngine *ee, llvm::Module *mod, std::vector<void (*)()> *cleanup_routines);

    static bool lib_cuda_linked;

    static CodeGen_GPU_Dev* make_dev(Target);

    llvm::Value *get_module_state();

private:
    /** Child code generator for device kernels. */
    CodeGen_GPU_Dev *cgdev;
};


}}

#endif
