#ifndef HALIDE_CODEGEN_INTERNAL_H
#define HALIDE_CODEGEN_INTERNAL_H

/** \file
 * 
 * Defines functionality that's useful to multiple target-specific
 * CodeGen paths, but shouldn't live in CodeGen.h (because that's the
 * front-end-facing interface to CodeGen).
 */

#include "IRVisitor.h"
#include "LLVM_Headers.h"
#include "Scope.h"
#include "IR.h"

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

    bool track_buffers;
    llvm::StructType *buffer_t;

public:

    /** Traverse a statement and find all references to external
     * symbols. 
     * 
     * Simple backends just create a pointer for internal
     * allocations. If the backend creates a whole buffer_t (e.g. in
     * order to track dirty bits), then the third argument should be
     * set to true. When the closure encounters a read or write to
     * 'foo', it assumes that the host pointer is found in the symbol
     * table as 'foo.host', and any buffer_t pointer is found under
     * 'foo.buffer'. */
    static Closure make(Stmt s, const std::string &loop_variable, bool track_buffers, llvm::StructType *buffer_t);

    /** External variables referenced. */
    std::map<std::string, Type> vars;
    
    /** External allocations read from. */
    std::map<std::string, Type> reads;

    /** External allocations written to. */
    std::map<std::string, Type> writes;

    /** The llvm types of the external symbols. */
    std::vector<llvm::Type *> llvm_types(llvm::LLVMContext *context);

    /** The Halide names of the external symbols (in the same order as llvm_types). */
    std::vector<std::string> names();

    /** The llvm type of a struct containing all of the externally referenced state. */
    llvm::StructType *build_type(llvm::LLVMContext *context);

    /** Emit code that builds a struct containing all the externally
     * referenced state. Requires you to pass it a struct to fill in,
     * a scope to retrieve the llvm values from and a builder to place
     * the packing code. */
    void pack_struct(llvm::Value *dst, const Scope<llvm::Value *> &src, llvm::IRBuilder<> *builder);

    /** Emit code that unpacks a struct containing all the externally
     * referenced state into a symbol table. Requires you to pass it a
     * state struct value, a scope to fill, and a builder to place the
     * unpacking code. */
    void unpack_struct(Scope<llvm::Value *> &dst, llvm::Value *src, llvm::IRBuilder<> *builder);

};

/** Get the llvm type equivalent to a given halide type */
llvm::Type *llvm_type_of(llvm::LLVMContext *context, Halide::Type t);

}}

#endif
