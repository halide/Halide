#ifndef HALIDE_CODEGEN_POSIX_H
#define HALIDE_CODEGEN_POSIX_H

/** \file
 * Defines a base-class for code-generators on posixy cpu platforms
 */

#include "CodeGen_LLVM.h"

namespace Halide {
namespace Internal {

/** A code generator that emits posix code from a given Halide stmt. */
class CodeGen_Posix : public CodeGen_LLVM {
public:

    /** Create an posix code generator. Processor features can be
     * enabled using the appropriate arguments */
    CodeGen_Posix(Target t);

protected:

    using CodeGen_LLVM::visit;

    /** Posix implementation of Allocate. Small constant-sized allocations go
     * on the stack. The rest go on the heap by calling "halide_malloc"
     * and "halide_free" in the standard library. */
    // @{
    void visit(const Allocate *);
    void visit(const Free *);
    // @}

    /** A struct describing heap or stack allocations. */
    struct Allocation {
        /** The memory */
        llvm::Value *ptr;

        /** Destructor stack slot for this allocation. */
        llvm::Value *destructor;

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

};

}}

#endif
