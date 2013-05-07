#ifndef HALIDE_LLVM_HEADERS_H
#define HALIDE_LLVM_HEADERS_H

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
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/PassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetLibraryInfo.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/IPO.h>

// Temporary affordance to compile with both llvm 3.2 and 3.3.
// Protected as at least one installation of llvm elides version macros.
#if defined(LLVM_VERSION_MINOR) && LLVM_VERSION_MINOR < 3

// LLVM 3.2 includes
#include <llvm/Value.h>
#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/DataLayout.h>
#include <llvm/IRBuilder.h>
#include <llvm/Intrinsics.h>
#include <llvm/TargetTransformInfo.h>
#include <llvm/ExecutionEngine/JITMemoryManager.h>

// They renamed this type in 3.3
typedef llvm::Attributes Attribute;
typedef llvm::Attributes::AttrVal AttrKind;

#else

// Equivalent LLVM 3.3 includes
#include <llvm/IR/Value.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>

typedef llvm::Attribute Attribute;
typedef llvm::Attribute::AttrKind AttrKind;

#endif

// No msvc warnings from llvm headers please
#ifdef _WIN32
#pragma warning(pop)
#endif

// An adapter object to paper over the differences between Attrs in
// various versions of llvm.
//namespace {

class LLVMAPIAttributeAdapter {
    llvm::LLVMContext &llvm_context;    
    AttrKind kind;

public:
    LLVMAPIAttributeAdapter(llvm::LLVMContext &context, AttrKind kind_arg) :
        llvm_context(context), kind(kind_arg) {}
    
    operator AttrKind() { return kind; }
    operator Attribute() { return Attribute::get(llvm_context, kind); }
};

//}

#endif
