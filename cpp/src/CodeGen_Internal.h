#ifndef HALIDE_CODEGEN_INTERNAL_H
#define HALIDE_CODEGEN_INTERNAL_H

#include "CodeGen.h"
#include "IRVisitor.h"

// No msvc warnings from llvm headers please
#ifdef _WIN32
#pragma warning(push, 0)
#endif
#include <llvm/Config/config.h>

// MCJIT doesn't seem to work right on os x yet
#ifdef __APPLE__
#else
#define USE_MCJIT
#endif

#ifdef USE_MCJIT
#include <llvm/ExecutionEngine/MCJIT.h>
#else
#include <llvm/ExecutionEngine/JIT.h>
#endif
#include <llvm/Analysis/Verifier.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetLibraryInfo.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/PassManager.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/IPO.h>

// Temporary affordance to compile with both llvm 3.2 and 3.3.
// Protected as at least one installation of llvm elides version macros.
#if defined(LLVM_VERSION_MINOR) && LLVM_VERSION_MINOR < 3
#include <llvm/Value.h>
#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/TargetTransformInfo.h>
#include <llvm/DataLayout.h>
#include <llvm/IRBuilder.h>
#include <llvm/ExecutionEngine/JITMemoryManager.h>
// They renamed this type in 3.3
typedef llvm::Attributes Attribute;
typedef llvm::Attributes::AttrVal AttrKind;
#else
#include <llvm/IR/Value.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
typedef llvm::Attribute::AttrKind AttrKind;
#endif

// No msvc warnings from llvm headers please
#ifdef _WIN32
#pragma warning(pop)
#endif

namespace Halide { 
namespace Internal {

/** A helper class to manage closures - used for parallel for loops */
class CodeGen::Closure : public IRVisitor {
protected:
    Scope<int> ignore;

    using IRVisitor::visit;

    void visit(const Let *op);
    void visit(const LetStmt *op);
    void visit(const For *op);
    void visit(const Load *op);
    void visit(const Store *op);
    void visit(const Allocate *op);
    void visit(const Variable *op);

public:
    Closure(Stmt s, const std::string &loop_variable);
    std::map<std::string, Type> vars, reads, writes;

    llvm::StructType *build_type(CodeGen *gen);
    void pack_struct(CodeGen *gen, llvm::Value *dst, const Scope<llvm::Value *> &src, llvm::IRBuilder<> *builder);
    void unpack_struct(CodeGen *gen, Scope<llvm::Value *> &dst, llvm::Value *src, llvm::IRBuilder<> *builder, llvm::Module *module, llvm::LLVMContext &context);
    std::vector<llvm::Type*> llvm_types(CodeGen *gen);
    std::vector<std::string> names();
};


// Wraps an execution engine. Takes ownership of the given module and
// the memory for jit compiled code.
class JITModuleHolder {
public:
        mutable RefCount ref_count;    
    JITModuleHolder(llvm::Module *module, class CodeGen *cg);
    ~JITModuleHolder();
    void (*shutdown_thread_pool)();
    llvm::ExecutionEngine *execution_engine;
    llvm::LLVMContext *context;
};

}}

#endif
