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
    llvm::Type *i32x4, *i32x8;
    // @}

    /** Some wildcard variables used for peephole optimizations in
     * subclasses */
    // @{
    Expr wild_i8x8, wild_i16x4, wild_i32x2; // 64-bit signed ints
    Expr wild_u8x8, wild_u16x4, wild_u32x2; // 64-bit unsigned ints
    Expr wild_i8x16, wild_i16x8, wild_i32x4, wild_i64x2; // 128-bit signed ints
    Expr wild_u8x16, wild_u16x8, wild_u32x4, wild_u64x2; // 128-bit unsigned ints
    Expr wild_i8x32, wild_i16x16, wild_i32x8; // 256-bit signed ints
    Expr wild_u8x32, wild_u16x16, wild_u32x8; // 256-bit unsigned ints
    Expr wild_f32x4, wild_f64x2; // 128-bit floats
    Expr wild_f32x8, wild_f64x4; // 256-bit floats
    // @}

    using CodeGen::visit;

    /** Posix implementation of Allocate. Small constant-sized ones go
     * on the stack. The rest go on the heap by calling "fast_malloc"
     * and "fast_free" in the standard library */
    void visit(const Allocate *);        

    /** The heap allocations currently in scope */
    std::stack<llvm::Value *> heap_allocations;

    /** Free all heap allocations in scope */
    void prepare_for_early_exit();
};

}}

#endif
