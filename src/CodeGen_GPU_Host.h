#ifndef HALIDE_CODEGEN_GPU_HOST_H
#define HALIDE_CODEGEN_GPU_HOST_H

/** \file
 * Defines the code-generator for producing GPU host code
 */

#include <map>
#include <string>

#include "CodeGen_GPU_Dev.h"
#include "CodeGen_LLVM.h"
#include "IR.h"

namespace Halide {

struct Target;
namespace Internal {

// Sniff the contents of a kernel to extracts the bounds of all the
// thread indices (so we know how many threads to launch), and the
// amount of shared memory to allocate.
class ExtractBounds : public IRVisitor {
public:
    Expr num_threads[4];
    Expr num_blocks[4];
    Expr shared_mem_size;

    ExtractBounds();

private:
    bool found_shared = false;

    using IRVisitor::visit;

    void visit(const For *op) override;

    void visit(const LetStmt *op) override;

    void visit(const Allocate *allocate) override;
};

/** A code generator that emits GPU code from a given Halide stmt. */
template<typename CodeGen_CPU>
class CodeGen_GPU_Host : public CodeGen_CPU {
public:
    /** Create a GPU code generator. GPU target is selected via
     * CodeGen_GPU_Options. Processor features can be enabled using the
     * appropriate flags from Target */
    CodeGen_GPU_Host(const Target &);

protected:
    void compile_func(const LoweredFunc &func, const std::string &simple_name, const std::string &extern_name) override;

    /** Declare members of the base class that must exist to help the
     * compiler do name lookup. Annoying but necessary, because the
     * compiler doesn't know that CodeGen_CPU will in fact inherit
     * from CodeGen for every instantiation of this template. */
    using CodeGen_CPU::allocations;
    using CodeGen_CPU::builder;
    using CodeGen_CPU::codegen;
    using CodeGen_CPU::context;
    using CodeGen_CPU::create_alloca_at_entry;
    using CodeGen_CPU::function;
    using CodeGen_CPU::get_user_context;
    using CodeGen_CPU::halide_buffer_t_type;
    using CodeGen_CPU::i16_t;
    using CodeGen_CPU::i32_t;
    using CodeGen_CPU::i64_t;
    using CodeGen_CPU::i8_t;
    using CodeGen_CPU::init_module;
    using CodeGen_CPU::llvm_type_of;
    using CodeGen_CPU::module;
    using CodeGen_CPU::register_destructor;
    using CodeGen_CPU::sym_exists;
    using CodeGen_CPU::sym_get;
    using CodeGen_CPU::sym_pop;
    using CodeGen_CPU::sym_push;
    using CodeGen_CPU::target;
    using CodeGen_CPU::type_t_type;
    using CodeGen_CPU::visit;

    /** Nodes for which we need to override default behavior for the GPU runtime */
    // @{
    void visit(const For *) override;
    // @}

    std::string function_name;

private:
    /** Child code generator for device kernels. */
    std::map<DeviceAPI, std::unique_ptr<CodeGen_GPU_Dev>> cgdev;
};

}  // namespace Internal
}  // namespace Halide

#endif
