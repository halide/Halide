#ifndef HALIDE_CODEGEN_INTERNAL_H
#define HALIDE_CODEGEN_INTERNAL_H

/** \file
 *
 * Defines functionality that's useful to multiple target-specific
 * CodeGen paths, but shouldn't live in CodeGen_LLVM.h (because that's the
 * front-end-facing interface to CodeGen).
 */

#include <memory>
#include <string>

#include "Closure.h"
#include "Expr.h"
#include "Scope.h"

namespace llvm {
class ConstantFolder;
class ElementCount;
class Function;
class IRBuilderDefaultInserter;
class LLVMContext;
class Module;
class StructType;
class TargetMachine;
class TargetOptions;
class Type;
class Value;
template<typename, typename>
class IRBuilder;
}  // namespace llvm

namespace Halide {

struct Target;

namespace Internal {

/** The llvm type of a struct containing all of the externally referenced state of a Closure. */
llvm::StructType *build_closure_type(const Closure &closure, llvm::StructType *halide_buffer_t_type, llvm::LLVMContext *context);

/** Emit code that builds a struct containing all the externally
 * referenced state. Requires you to pass it a type and struct to fill in,
 * a scope to retrieve the llvm values from and a builder to place
 * the packing code. */
void pack_closure(llvm::StructType *type,
                  llvm::Value *dst,
                  const Closure &closure,
                  const Scope<llvm::Value *> &src,
                  llvm::StructType *halide_buffer_t_type,
                  llvm::IRBuilder<llvm::ConstantFolder, llvm::IRBuilderDefaultInserter> *builder);

/** Emit code that unpacks a struct containing all the externally
 * referenced state into a symbol table. Requires you to pass it a
 * state struct type and value, a scope to fill, and a builder to place the
 * unpacking code. */
void unpack_closure(const Closure &closure,
                    Scope<llvm::Value *> &dst,
                    llvm::StructType *type,
                    llvm::Value *src,
                    llvm::IRBuilder<llvm::ConstantFolder, llvm::IRBuilderDefaultInserter> *builder);

/** Get the llvm type equivalent to a given halide type */
llvm::Type *llvm_type_of(llvm::LLVMContext *context, Halide::Type t);

/** Get the number of elements in an llvm vector type, or return 1 if
 * it's not a vector type. */
int get_vector_num_elements(llvm::Type *);

/** Get the scalar type of an llvm vector type. Returns the argument
 * if it's not a vector type. */
llvm::Type *get_vector_element_type(llvm::Type *);

llvm::ElementCount element_count(int e);

llvm::Type *get_vector_type(llvm::Type *, int);

/** Which built-in functions require a user-context first argument? */
bool function_takes_user_context(const std::string &name);

/** Given a size (in bytes), return True if the allocation size can fit
 * on the stack; otherwise, return False. This routine asserts if size is
 * non-positive. */
bool can_allocation_fit_on_stack(int64_t size);

/** Does a {div/mod}_round_to_zero using binary long division for int/uint.
 *  max_abs is the maximum absolute value of (a/b).
 *  Returns the pair {div_round_to_zero, mod_round_to_zero}. */
std::pair<Expr, Expr> long_div_mod_round_to_zero(const Expr &a, const Expr &b,
                                                 const uint64_t *max_abs = nullptr);

/** Given a Halide Euclidean division/mod operation, do constant optimizations
 * and possibly call lower_euclidean_div/lower_euclidean_mod if necessary.
 * Can introduce mulhi_shr and sorted_avg intrinsics as well as those from the
 * lower_euclidean_ operation -- div_round_to_zero or mod_round_to_zero. */
///@{
Expr lower_int_uint_div(const Expr &a, const Expr &b);
Expr lower_int_uint_mod(const Expr &a, const Expr &b);
///@}

/** Given a Halide Euclidean division/mod operation, define it in terms of
 * div_round_to_zero or mod_round_to_zero. */
///@{
Expr lower_euclidean_div(Expr a, Expr b);
Expr lower_euclidean_mod(Expr a, Expr b);
///@}

/** Given a Halide shift operation with a signed shift amount (may be negative), define
 * an equivalent expression using only shifts by unsigned amounts. */
///@{
Expr lower_signed_shift_left(const Expr &a, const Expr &b);
Expr lower_signed_shift_right(const Expr &a, const Expr &b);
///@}

/** Reduce a mux intrinsic to a select tree */
Expr lower_mux(const Call *mux);

/** Given an llvm::Module, set llvm:TargetOptions, cpu and attr information */
void get_target_options(const llvm::Module &module, llvm::TargetOptions &options, std::string &mcpu, std::string &mattrs);

/** Given two llvm::Modules, clone target options from one to the other */
void clone_target_options(const llvm::Module &from, llvm::Module &to);

/** Given an llvm::Module, get or create an llvm:TargetMachine */
std::unique_ptr<llvm::TargetMachine> make_target_machine(const llvm::Module &module);

/** Set the appropriate llvm Function attributes given a Target. */
void set_function_attributes_for_target(llvm::Function *, const Target &);

/** Save a copy of the llvm IR currently represented by the module as
 * data in the __LLVM,__bitcode section. Emulates clang's
 * -fembed-bitcode flag and is useful to satisfy Apple's bitcode
 * inclusion requirements.  */
void embed_bitcode(llvm::Module *M, const std::string &halide_command);

}  // namespace Internal
}  // namespace Halide

#endif
