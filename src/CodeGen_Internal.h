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

namespace Halide {
namespace Internal {

/** A helper class to manage closures. Walks over a statement and
 * retrieves all the references within it to external symbols
 * (variables and allocations). It then helps you build a struct
 * containing the current values of these symbols that you can use as
 * a closure if you want to migrate the body of the statement to its
 * own function (e.g. because it's the body of a parallel for loop. */
class Closure : public IRVisitor {
protected:
    Scope<int> ignore;

    using IRVisitor::visit;

    void visit(const Let *op);
    void visit(const LetStmt *op);
    void visit(const For *op);
    void visit(const Load *op);
    void visit(const Store *op);
    void visit(const Allocate *op);
    void visit(const Variable *op);

    llvm::StructType *buffer_t;

public:
    /** Information about a buffer reference from a closure. */
    struct BufferRef
    {
        /** The type of the buffer referenced. */
        Type type;

        /** The dimensionality of the buffer. */
        uint8_t dimensions;

        /** The buffer is read from. */
        bool read;

        /** The buffer is written to. */
        bool write;

        /** The size of the buffer if known, otherwise zero. */
        size_t size;

        BufferRef() : dimensions(0), read(false), write(false), size(0) { }
    };

public:
    Closure() : buffer_t(NULL) {}

    /** Traverse a statement and find all references to external
     * symbols.
     *
     * When the closure encounters a read or write to 'foo', it
     * assumes that the host pointer is found in the symbol table as
     * 'foo.host', and any buffer_t pointer is found under
     * 'foo.buffer'. */
    Closure(Stmt s, const std::string &loop_variable, llvm::StructType *buffer_t);

    /** External variables referenced. */
    std::map<std::string, Type> vars;

    /** External allocations referenced. */
    std::map<std::string, BufferRef> buffers;

    /** The llvm types of the external symbols. */
    std::vector<llvm::Type *> llvm_types(llvm::LLVMContext *context);

    /** The Halide names of the external symbols (in the same order as llvm_types). */
    std::vector<std::string> names();

    /** The llvm type of a struct containing all of the externally referenced state. */
    llvm::StructType *build_type(llvm::LLVMContext *context);

    /** Emit code that builds a struct containing all the externally
     * referenced state. Requires you to pass it a type and struct to fill in,
     * a scope to retrieve the llvm values from and a builder to place
     * the packing code. */
    void pack_struct(llvm::Type *type, llvm::Value *dst, const Scope<llvm::Value *> &src, llvm::IRBuilder<> *builder);

    /** Emit code that unpacks a struct containing all the externally
     * referenced state into a symbol table. Requires you to pass it a
     * state struct type and value, a scope to fill, and a builder to place the
     * unpacking code. */
    void unpack_struct(Scope<llvm::Value *> &dst, llvm::Type *type, llvm::Value *src, llvm::IRBuilder<> *builder);

};

/** Get the llvm type equivalent to a given halide type */
llvm::Type *llvm_type_of(llvm::LLVMContext *context, Halide::Type t);

/** A routine to check if a list of extents are all constants, and if so
 * verify the total size is less than 2^31 - 1. If the result is constant,
 * but overflows, this routine asserts. The name parameter is used in the
 * assertion message. */
bool constant_allocation_size(const std::vector<Expr> &extents, const std::string &name, int32_t &size);

/** Which built-in functions require a user-context first argument? */
bool function_takes_user_context(const std::string &name);

}}

#endif
