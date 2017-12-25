#ifndef HALIDE_CODEGEN_INTERNAL_H
#define HALIDE_CODEGEN_INTERNAL_H

/** \file
 *
 * Defines functionality that's useful to multiple target-specific
 * CodeGen paths, but shouldn't live in CodeGen_LLVM.h (because that's the
 * front-end-facing interface to CodeGen).
 */

#include <memory>

#include "Closure.h"
#include "IR.h"
#include "IRVisitor.h"
#include "LLVM_Headers.h"
#include "Scope.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** The llvm type of a struct containing all of the externally referenced state of a Closure. */
llvm::StructType *build_closure_type(const Closure& closure, llvm::StructType *buffer_t, llvm::LLVMContext *context);

/** Emit code that builds a struct containing all the externally
 * referenced state. Requires you to pass it a type and struct to fill in,
 * a scope to retrieve the llvm values from and a builder to place
 * the packing code. */
void pack_closure(llvm::StructType *type,
                  llvm::Value *dst,
                  const Closure& closure,
                  const Scope<llvm::Value *> &src,
                  llvm::StructType *buffer_t,
                  llvm::IRBuilder<> *builder);

/** Emit code that unpacks a struct containing all the externally
 * referenced state into a symbol table. Requires you to pass it a
 * state struct type and value, a scope to fill, and a builder to place the
 * unpacking code. */
void unpack_closure(const Closure& closure,
                    Scope<llvm::Value *> &dst,
                    llvm::StructType *type,
                    llvm::Value *src,
                    llvm::IRBuilder<> *builder);

/** Get the llvm type equivalent to a given halide type */
llvm::Type *llvm_type_of(llvm::LLVMContext *context, Halide::Type t);

/** Which built-in functions require a user-context first argument? */
bool function_takes_user_context(const std::string &name);

/** Given a size (in bytes), return True if the allocation size can fit
 * on the stack; otherwise, return False. This routine asserts if size is
 * non-positive. */
bool can_allocation_fit_on_stack(int64_t size);

/** Given a Halide Euclidean division/mod operation, define it in terms of
 * div_round_to_zero or mod_round_to_zero. */
///@{
Expr lower_euclidean_div(Expr a, Expr b);
Expr lower_euclidean_mod(Expr a, Expr b);
///@}

/** Replace predicated loads/stores with unpredicated equivalents
 * inside branches. */
Stmt unpredicate_loads_stores(Stmt s);

/** Given an llvm::Module, set llvm:TargetOptions, cpu and attr information */
void get_target_options(const llvm::Module &module, llvm::TargetOptions &options,
                        std::string &mcpu, std::string &mattrs);

/** Given two llvm::Modules, clone target options from one to the other */
void clone_target_options(const llvm::Module &from, llvm::Module &to);

/** Given an llvm::Module, get or create an llvm:TargetMachine */
std::unique_ptr<llvm::TargetMachine> make_target_machine(const llvm::Module &module);

/** Set the appropriate llvm Function attributes given a Target. */
void set_function_attributes_for_target(llvm::Function *, Target);

}}

#endif
