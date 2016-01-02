#ifndef HALIDE_LLVM_HEADERS_H
#define HALIDE_LLVM_HEADERS_H

// This seems to be required by some LLVM header, which is likely an LLVM bug.
#include <stddef.h>

// No msvc warnings from llvm headers please
#ifdef _WIN32
#pragma warning(push, 0)
#endif

#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/ExecutionEngine/JITEventListener.h>

#if LLVM_VERSION < 35
#include <llvm/Analysis/Verifier.h>
#include <llvm/Linker.h>
#else
#include <llvm/IR/Verifier.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/FileSystem.h>
#endif

#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#if LLVM_VERSION < 36
#include <llvm/ExecutionEngine/JITMemoryManager.h>
#endif
#if LLVM_VERSION < 37
#include <llvm/PassManager.h>
#else
#include <llvm/IR/LegacyPassManager.h>
#endif
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/DataExtractor.h>
#if LLVM_VERSION > 36
#include <llvm/Analysis/TargetLibraryInfo.h>
#else
#include <llvm/Target/TargetLibraryInfo.h>
#endif
#include <llvm/Target/TargetSubtargetInfo.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/Object/ObjectFile.h>

// Temporary affordance to compile with both llvm 3.2 and 3.3+
// Protected as at least one installation of llvm elides version macros.
#if LLVM_VERSION < 33

// LLVM 3.2 includes
#include <llvm/Value.h>
#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/DataLayout.h>
#include <llvm/IRBuilder.h>
#include <llvm/Intrinsics.h>
#include <llvm/TargetTransformInfo.h>
#include <llvm/MDBuilder.h>
#else

// Equivalent LLVM 3.3 includes
#include <llvm/IR/Value.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/MDBuilder.h>
#endif

#if WITH_NATIVE_CLIENT
#include <llvm/Transforms/NaCl.h>
#endif

// No msvc warnings from llvm headers please
#ifdef _WIN32
#pragma warning(pop)
#endif

// llvm may sometimes define NDEBUG, which is annoying, because we always want asserts
#ifdef NDEBUG
#undef NDEBUG
#include <assert.h>
#define NDEBUG
#endif

namespace Halide { namespace Internal {
#if (LLVM_VERSION >= 36) && !(WITH_NATIVE_CLIENT)
typedef llvm::Metadata *LLVMMDNodeArgumentType;
inline llvm::Metadata *value_as_metadata_type(llvm::Value *val) { return llvm::ValueAsMetadata::get(val); }
#else
typedef llvm::Value *LLVMMDNodeArgumentType;
inline llvm::Value *value_as_metadata_type(llvm::Value *val) { return val; }
#endif

template <typename T>
typename T::value_type *iterator_to_pointer(T iter) {
    return &*iter;
}

}}


#endif
