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
    CodeGen_Posix(Target t);

protected:

    /** Some useful llvm types for subclasses */
    // @{
    llvm::Type *i8x8, *i8x16, *i8x32;
    llvm::Type *i16x4, *i16x8, *i16x16;
    llvm::Type *i32x2, *i32x4, *i32x8;
    llvm::Type *i64x2, *i64x4;
    llvm::Type *f32x2, *f32x4, *f32x8;
    llvm::Type *f64x2, *f64x4;
    llvm::Type *i32x16;
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
    Expr wild_i32x16; //512 bit signed ints.
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
     * on the stack. The rest go on the heap by calling "halide_malloc"
     * and "halide_free" in the standard library. */
    // @{
    void visit(const Allocate *);
    void visit(const Free *);
    // @}

    /** A struct describing heap or stack allocations. */
    struct Allocation {
        llvm::Value *ptr;

        /** How many bytes this allocation is, or 0 if not
         * constant. */
        int constant_bytes;

        /** How many bytes of stack space used. 0 implies it was a
         * heap allocation. */
        int stack_bytes;
    };

    /** The allocations currently in scope. The stack gets pushed when
     * we enter a new function. */
    Scope<Allocation> allocations;

    /** Free all heap allocations in scope. */
    void prepare_for_early_exit();

    /** Initialize the CodeGen internal state to compile a fresh module */
    void init_module();

private:

    /** Stack allocations that were freed, but haven't gone out of
     * scope yet.  This allows us to re-use stack allocations when
     * they aren't being used. */
    std::vector<Allocation> free_stack_allocs;

    /** Generates code for computing the size of an allocation from a
     * list of its extents and its size. Fires a runtime assert
     * (halide_error) if the size overflows 2^31 -1, the maximum
     * positive number an int32_t can hold. */
    llvm::Value *codegen_allocation_size(const std::string &name, Type type, const std::vector<Expr> &extents);

    /** Allocates some memory on either the stack or the heap, and
     * returns an Allocation object describing it. For heap
     * allocations this calls halide_malloc in the runtime, and for
     * stack allocations it either reuses an existing block from the
     * free_stack_blocks list, or it saves the stack pointer and calls
     * alloca.
     *
     * This call returns the allocation, pushes it onto the
     * 'allocations' map, and adds an entry to the symbol table called
     * name.host that provides the base pointer.
     *
     * When the allocation can be freed call 'free_allocation', and
     * when it goes out of scope call 'destroy_allocation'. */
    Allocation create_allocation(const std::string &name, Type type,
                                 const std::vector<Expr> &extents,
                                 Expr condition);

    /** Free the memory backing an allocation and pop it from the
     * symbol table and the allocations map. For heap allocations it
     * calls halide_free in the runtime, for stack allocations it
     * marks the block as free so it can be reused. */
    void free_allocation(const std::string &name);
};

}}

#endif
