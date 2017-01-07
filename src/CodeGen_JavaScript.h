#ifndef HALIDE_CODEGEN_JAVASCRIPT_H
#define HALIDE_CODEGEN_JAVASCRIPT_H

/** \file
 *
 * Defines an IRPrinter that emits C++ code equivalent to a halide stmt
 */

#include <string>
#include <vector>
#include <ostream>
#include <map>

#include "IRPrinter.h"
#include "Module.h"
#include "Scope.h"

namespace Halide {

struct Argument;

namespace Internal {

/** This class emits C++ code equivalent to a halide Stmt. It's
 * mostly the same as an IRPrinter, but it's wrapped in a function
 * definition, and some things are handled differently to be valid
 * C++.
 */
class CodeGen_JavaScript : public IRPrinter {
public:
    /** Initialize a C code generator pointing at a particular output
     * stream (e.g. a file, or std::cout) */
    CodeGen_JavaScript(std::ostream &);
    ~CodeGen_JavaScript();

    /** Emit source code equivalent to the given statement, wrapped in
     * a function with the given type signature */
    void compile(const Module &module);

    static void test();

protected:
    /** Emit a declaration. */
    // @{
    virtual void compile(const LoweredFunc &func);
    virtual void compile(const Buffer<> &buffer);
    // @}

    /** An ID for the most recently generated ssa variable */
    std::string id;

    /** A cache of generated values in scope */
    std::map<std::string, std::string> cache;

    /** Is SIMD.js allowed in the current scope being compiled?
     * Generally set on a per module basis. */
    bool use_simd_js;

    /** Emit an expression as an assignment, then return the id of the
     * resulting var */
    std::string print_expr(Expr);

    /** Emit a statement */
    void print_stmt(Stmt);

    /** Emit a statement to reinterpret an expression as another type */
    virtual std::string print_reinterpret(Type, Expr);

    /** Emit a version of a string that is a valid identifier in C (. is replaced with _) */
    virtual std::string print_name(const std::string &);

    /** Emit an SSA-style assignment, and set id to the freshly generated name. Return id. */
    std::string print_assignment(Type t, const std::string &rhs);

    std::string make_js_int_cast(std::string value, bool src_unsigned, int src_bits, bool dst_unsigned, int dst_bits);

    /** Open a new C scope (i.e. throw in a brace, increase the indent) */
    void open_scope();

    /** Close a C scope (i.e. throw in an end brace, decrease the indent) */
    void close_scope(const std::string &comment);

    /** Unpack a buffer into its constituent parts */
    void unpack_buffer(Type t, const std::string &buffer_name);

    struct Allocation {
        Type type;
        std::string free_function;
    };

    /** Track the types of allocations to avoid unnecessary casts. */
    Scope<Allocation> allocations;

    /** True if there is a void * __user_context parameter in the arguments. */
    bool have_user_context;

    using IRPrinter::visit;

    void visit(const Variable *);
    void visit(const IntImm *);
    void visit(const UIntImm *);
    void visit(const StringImm *);
    void visit(const FloatImm *);
    void visit(const Cast *);
    void visit(const Add *);
    void visit(const Sub *);
    void visit(const Mul *);
    void visit(const Div *);
    void visit(const Mod *);
    void visit(const Max *);
    void visit(const Min *);
    void visit(const EQ *);
    void visit(const NE *);
    void visit(const LT *);
    void visit(const LE *);
    void visit(const GT *);
    void visit(const GE *);
    void visit(const And *);
    void visit(const Or *);
    void visit(const Not *);
    void visit(const Call *);
    void visit(const Select *);
    void visit(const Load *);
    void visit(const Ramp *);
    void visit(const Broadcast *);
    void visit(const Store *);
    void visit(const Let *);
    void visit(const LetStmt *);
    void visit(const AssertStmt *);
    void visit(const ProducerConsumer *);
    void visit(const For *);
    void visit(const Provide *);
    void visit(const Allocate *);
    void visit(const Free *);
    void visit(const Realize *);
    void visit(const IfThenElse *);
    void visit(const Evaluate *);

    void visit_binop(Type t, Expr a, Expr b, const char *op, const char *simd_js_op);

    std::string fround_start_if_needed(const Type &t) const;
    std::string fround_end_if_needed(const Type &t) const;

    bool simd_js_type_for_type(Type t, std::string &result, bool include_prefix = true);
    std::string literal_may_be_vector_start(Type t);
    std::string literal_may_be_vector_end(Type t);

    void lane_by_lane_load(std::ostringstream &rhs, const Load *op,
        const std::string &typed_name, const std::string &open, const std::string &close, bool type_cast_needed);
    void lane_by_lane_store(const Store *op, const std::string &typed_name, bool type_cast_needed);
    void call_scalar_function(std::ostream &rhs, Type type, const std::string &name, bool is_operator, const std::vector<Expr> &args);
};

}
}

#endif
