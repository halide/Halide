#ifndef HALIDE_LLVM_HEADERS_H
#define HALIDE_LLVM_HEADERS_H

#if LLVM_VERSION >= 110
// We're good to go
#else
#error "Compiling Halide requires LLVM 11.0 or newer"
#endif

// This seems to be required by some LLVM header, which is likely an LLVM bug.
#include <cstddef>

// No msvc warnings from llvm headers please
#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#ifdef __GNUC__
#pragma GCC system_header
#endif
#ifdef __clang__
#pragma clang system_header
#endif

#if WITH_WABT
#include <lld/Common/Driver.h>
#endif

#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ErrorHandling.h"
#include <llvm/ADT/StringMap.h>
#if LLVM_VERSION >= 12
#include <llvm/Analysis/AliasAnalysis.h>
#endif
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/PassTimingInfo.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Object/ArchiveWriter.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CodeGen.h>
#if LLVM_VERSION >= 12
#include <llvm/Support/CommandLine.h>
#endif
#include <llvm/Support/DataExtractor.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/Inliner.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/Instrumentation.h>
#include <llvm/Transforms/Instrumentation/AddressSanitizer.h>
#include <llvm/Transforms/Instrumentation/ThreadSanitizer.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <llvm/Transforms/Utils/SymbolRewriter.h>

#include <llvm/Transforms/Scalar/GVN.h>

#include <llvm/Transforms/IPO/AlwaysInliner.h>

#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#ifdef WITH_HEXAGON
#include <llvm/IR/IntrinsicsHexagon.h>
#endif
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>

// No msvc warnings from llvm headers please
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// llvm may sometimes define NDEBUG, which is annoying, because we always want asserts
#ifdef NDEBUG
#undef NDEBUG
#include <assert.h>
#define NDEBUG
#endif

namespace Halide {
namespace Internal {

template<typename T>
auto iterator_to_pointer(T iter) -> decltype(&*std::declval<T>()) {
    return &*iter;
}

inline std::string get_llvm_function_name(const llvm::Function *f) {
    return f->getName().str();
}

inline std::string get_llvm_function_name(const llvm::Function &f) {
    return f.getName().str();
}

inline llvm::StructType *get_llvm_struct_type_by_name(llvm::Module *module, const char *name) {
#if LLVM_VERSION >= 120
    return llvm::StructType::getTypeByName(module->getContext(), name);
#else
    return module->getTypeByName(name);
#endif
}

}  // namespace Internal
}  // namespace Halide

#endif
