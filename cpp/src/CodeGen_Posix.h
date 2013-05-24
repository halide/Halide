#ifndef HALIDE_CODEGEN_POSIX_H
#define HALIDE_CODEGEN_POSIX_H

/** \file
 * Defines a base-class for code-generators on posixy cpu platforms 
 */

#include "CodeGen.h"

namespace Halide { 
namespace Internal {

/** A code generator that emits posix code from a given Halide stmt. */
class CodeGen_Posix : public CodeGen {
public:

    /** Create an posix code generator. Processor features can be
     * enabled using the appropriate arguments */
    CodeGen_Posix();

protected:

    /** Some useful llvm types for subclasses */
    // @{
    llvm::Type *i8x8, *i8x16, *i8x32;
    llvm::Type *i16x4, *i16x8, *i16x16;
    llvm::Type *i32x2, *i32x4, *i32x8;
    llvm::Type *i64x2, *i64x4;
    llvm::Type *f32x2, *f32x4, *f32x8;
    llvm::Type *f64x2, *f64x4;
    // @}

    /** Some wildcard variables used for peephole optimizations in
     * subclasses */
    // @{
    Expr wild_i8x8, wild_i16x4, wild_i32x2; // 64-bit signed ints
    Expr wild_u8x8, wild_u16x4, wild_u32x2; // 64-bit unsigned ints
    Expr wild_i8x16, wild_i16x8, wild_i32x4, wild_i64x2; // 128-bit signed ints
    Expr wild_u8x16, wild_u16x8, wild_u32x4, wild_u64x2; // 128-bit unsigned ints
    Expr wild_i8x32, wild_i16x16, wild_i32x8, wild_i64x4; // 256-bit signed ints
    Expr wild_u8x32, wild_u16x16, wild_u32x8, wild_u64x4; // 256-bit unsigned ints
    Expr wild_f32x2; // 64-bit floats
    Expr wild_f32x4, wild_f64x2; // 128-bit floats
    Expr wild_f32x8, wild_f64x4; // 256-bit floats
    Expr min_i8, max_i8, max_u8;
    Expr min_i16, max_i16, max_u16;
    Expr min_i32, max_i32, max_u32;
    Expr min_i64, max_i64, max_u64;
    Expr min_f32, max_f32, min_f64, max_f64;
    // @}

    using CodeGen::visit;

    /** Posix implementation of Allocate. Small constant-sized allocations go
     * on the stack. The rest go on the heap by calling "hl_malloc"
     * and "hl_free" in the standard library. */
    void visit(const Allocate *);        

    /** Direct implementation of Posix allocation logic. The returned
     * `Value` is a pointer to the allocated memory. If you wish to
     * allow stack allocation, also pass in a pointer to the
     * llvm::Value you wish to store the saved stack in. If heap
     * allocation is performed, `*saved_stack` is set to `NULL`;
     * otherwise, it holds the saved stack pointer for later use with
     * the `stackrestore` intrinsic. */
    llvm::Value* malloc_buffer(const Allocate *alloc, llvm::Value **saved_stack = NULL);

    /** If `saved_stack` is non-`NULL`, this assumes the `ptr` was stack-allocated,
     * and frees it only by restoring the stack pointer; otherwise, it calls
     * `hl_free` in the standard library. */
    void free_buffer(llvm::Value *ptr, llvm::Value *saved_stack);

    /** Save and restore the stack directly. You only need to call
     * these if you're doing your own allocas. */
    // @{
    llvm::Value *save_stack();
    void restore_stack(llvm::Value *saved_stack);
    // @}

    /** The heap allocations currently in scope */
    std::vector<llvm::Value *> heap_allocations;

    /** Free all heap allocations in scope */
    void prepare_for_early_exit();

    /** Initialize the CodeGen internal state to compile a fresh module */
    void init_module();

};

}}

#endif
