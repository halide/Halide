#ifndef HALIDE_LLVM_HEADERS_H
#define HALIDE_LLVM_HEADERS_H

// This seems to be required by some LLVM header, which is likely an LLVM bug.
#include <stddef.h>

// No msvc warnings from llvm headers please
#ifdef _WIN32
#pragma warning(push, 0)
#endif

// MCJIT doesn't seem to work right on os x or windows yet
#ifdef __APPLE__
#else
#ifdef _WIN32
#else
#define USE_MCJIT
#endif
#endif

#ifdef USE_MCJIT
#include <llvm/ExecutionEngine/MCJIT.h>
#else
#include <llvm/ExecutionEngine/JIT.h>
#endif
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
#include <llvm/ExecutionEngine/JITMemoryManager.h>
#include <llvm/PassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/DataExtractor.h>
#include <llvm/Target/TargetLibraryInfo.h>
#include <llvm/Target/TargetSubtargetInfo.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/IPO.h>
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

#endif
