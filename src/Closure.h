#ifndef HALIDE_CLOSURE_H
#define HALIDE_CLOSURE_H

/** \file
 *
 * Provides Closure class.
 */

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

public:
    /** Information about a buffer reference from a closure. */
    struct Buffer {
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

        Buffer()
            : dimensions(0), read(false), write(false), size(0) {
        }
    };

protected:
    void found_buffer_ref(const std::string &name, Type type,
                          bool read, bool written, Halide::Buffer<> image);

public:
    Closure() = default;

    /** Traverse a statement and find all references to external
     * symbols.
     *
     * When the closure encounters a read or write to 'foo', it
     * assumes that the host pointer is found in the symbol table as
     * 'foo.host', and any buffer_t pointer is found under
     * 'foo.buffer'. */
    Closure(Stmt s, const std::string &loop_variable = "");

    /** External variables referenced. */
    std::map<std::string, Type> vars;

    /** External allocations referenced. */
    std::map<std::string, Buffer> buffers;
};

}  // namespace Internal
}  // namespace Halide

#endif
