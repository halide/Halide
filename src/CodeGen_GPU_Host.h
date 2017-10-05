#ifndef HALIDE_CODEGEN_GPU_HOST_H
#define HALIDE_CODEGEN_GPU_HOST_H

/** \file
 * Defines the code-generator for producing GPU host code
 */

#include <map>

#include "CodeGen_ARM.h"
#include "CodeGen_X86.h"
#include "CodeGen_MIPS.h"
#include "CodeGen_PowerPC.h"

#include "IR.h"

namespace Halide {
namespace Internal {

struct CodeGen_GPU_Dev;
struct GPU_Argument;

/** A code generator that emits GPU code from a given Halide stmt. */
template<typename CodeGen_CPU>
class CodeGen_GPU_Host : public CodeGen_CPU {
public:

    /** Create a GPU code generator. GPU target is selected via
     * CodeGen_GPU_Options. Processor features can be enabled using the
     * appropriate flags from Target */
    CodeGen_GPU_Host(Target);

    virtual ~CodeGen_GPU_Host();

protected:
    void compile_func(const LoweredFunc &func, const std::string &simple_name, const std::string &extern_name);

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
    using CodeGen_CPU::llvm_type_of;
    using CodeGen_CPU::create_alloca_at_entry;
    using CodeGen_CPU::i8_t;
    using CodeGen_CPU::i32_t;
    using CodeGen_CPU::i64_t;
    using CodeGen_CPU::buffer_t_type;
    using CodeGen_CPU::allocations;
    using CodeGen_CPU::register_destructor;

    /** Nodes for which we need to override default behavior for the GPU runtime */
    // @{
    void visit(const For *);
    // @}

    std::string function_name;

    llvm::Value *get_module_state(const std::string &api_unique_name,
                                  bool create = true);

private:
    /** Child code generator for device kernels. */
    std::map<DeviceAPI, CodeGen_GPU_Dev *> cgdev;
};

}}

#endif
