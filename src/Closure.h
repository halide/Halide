#ifndef HALIDE_CLOSURE_H
#define HALIDE_CLOSURE_H

/** \file
 *
 * Provides Closure class.
 */
#include <map>
#include <string>

#include "Buffer.h"
#include "IR.h"
#include "IRVisitor.h"
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
    Scope<> ignore;

    using IRVisitor::visit;

    void visit(const Let *op) override;
    void visit(const LetStmt *op) override;
    void visit(const For *op) override;
    void visit(const Load *op) override;
    void visit(const Store *op) override;
    void visit(const Allocate *op) override;
    void visit(const Variable *op) override;
    void visit(const Atomic *op) override;

public:
    /** Information about a buffer reference from a closure. */
    struct Buffer {
        /** The type of the buffer referenced. */
        Type type;

        /** The dimensionality of the buffer. */
        uint8_t dimensions = 0;

        /** The buffer is read from. */
        bool read = false;

        /** The buffer is written to. */
        bool write = false;

        /** The buffer is a texture */
        MemoryType memory_type = MemoryType::Auto;

        /** The size of the buffer if known, otherwise zero. */
        size_t size = 0;

        Buffer() = default;
    };

protected:
    void found_buffer_ref(const std::string &name, Type type,
                          bool read, bool written, const Halide::Buffer<> &image);

    /** One field of the closure's struct layout: a captured buffer's host
     * pointer or a captured variable, in the same order pack_into_struct()
     * and unpack_from_struct() lay them out. */
    struct ClosureField {
        std::string name;
        Type type;
    };

    /** The fields of the closure's struct, sorted by decreasing size so the
     * struct is densely packed. Shared by pack_into_struct() and
     * unpack_from_struct() so they always agree on layout. */
    std::vector<ClosureField> sorted_fields() const;

public:
    Closure() = default;

    // Movable but not copyable.
    Closure(const Closure &) = delete;
    Closure &operator=(const Closure &) = delete;
    Closure(Closure &&) = default;
    Closure &operator=(Closure &&) = default;

    /** Traverse a statement and find all references to external
     * symbols.
     *
     * When the closure encounters a read or write to 'foo', it
     * assumes that the host pointer is found in the symbol table as
     * 'foo.host', and any halide_buffer_t pointer is found under
     * 'foo.buffer'.
     *
     * Calling this multiple times (on multiple statements) is legal
     * (and will produce a unified closure).
     **/
    void include(const Stmt &s, const std::string &loop_variable = "");

    /** External variables referenced. There's code that assumes iterating over
     * this repeatedly gives a consistent order, so don't swap out the data type
     * for something non-deterministic. */
    std::map<std::string, Type> vars;

    /** External allocations referenced. */
    std::map<std::string, Buffer> buffers;

    /** The struct type describing this closure's packed layout: one field
     * per captured buffer pointer and variable, in the order used by
     * pack_into_struct()/unpack_from_struct(). */
    Type struct_type() const;

    /** Allocate a struct named `alloc_name` holding the current values of
     * this closure's captured symbols, and wrap `body` so the allocation
     * lives for its duration. Within `body` (and after this call returns),
     * the packed struct's address is `Variable::make(Handle(), alloc_name)`. */
    Stmt pack_into_struct(const std::string &alloc_name, const Stmt &body) const;

    /** Unpack a closure around a Stmt, putting all the names in scope. `e`
     * must be the Variable naming the raw pointer to the packed struct, as
     * bound by pack_into_struct() (or an equivalent incoming argument of
     * type Handle() pointing at a struct with the same layout). */
    Stmt unpack_from_struct(const Expr &e, const Stmt &s) const;
};

}  // namespace Internal
}  // namespace Halide

#endif
