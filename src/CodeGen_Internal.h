#ifndef HALIDE_CODEGEN_INTERNAL_H
#define HALIDE_CODEGEN_INTERNAL_H

/** \file
 *
 * Defines functionality that's useful to multiple target-specific
 * CodeGen paths, but shouldn't live in CodeGen_LLVM.h (because that's the
 * front-end-facing interface to CodeGen).
 */

#include "IR.h"
#include "IRVisitor.h"
#include "LLVM_Headers.h"
#include "Scope.h"
#include "Closure.h"

namespace Halide {
namespace Internal {

/** The llvm type of a struct containing all of the externally referenced state of a Closure. */
llvm::StructType *build_closure_type(const Closure& closure, llvm::StructType *buffer_t, llvm::LLVMContext *context);

/** Emit code that builds a struct containing all the externally
 * referenced state. Requires you to pass it a type and struct to fill in,
 * a scope to retrieve the llvm values from and a builder to place
 * the packing code. */
void pack_closure(llvm::Type *type, llvm::Value *dst,
                  const Closure& closure, const Scope<llvm::Value *> &src,
                  llvm::StructType *buffer_t,
                  llvm::IRBuilder<> *builder);

/** Emit code that unpacks a struct containing all the externally
 * referenced state into a symbol table. Requires you to pass it a
 * state struct type and value, a scope to fill, and a builder to place the
 * unpacking code. */
void unpack_closure(const Closure& closure, Scope<llvm::Value *> &dst,
                    llvm::Type *type, llvm::Value *src,
                    llvm::IRBuilder<> *builder);

/** Get the llvm type equivalent to a given halide type */
llvm::Type *llvm_type_of(llvm::LLVMContext *context, Halide::Type t);

/** Which built-in functions require a user-context first argument? */
bool function_takes_user_context(const std::string &name);

}}

#endif
