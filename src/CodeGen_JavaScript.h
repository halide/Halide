#ifndef HALIDE_CODEGEN_JAVASCRIPT_H
#define HALIDE_CODEGEN_JAVASCRIPT_H

/** \file
 *
 * Defines an IRPrinter that emits C++ code equivalent to a halide stmt
 */

#include <string>
#include <vector>
#include <ostream>
#include <unordered_map>
#include <unordered_set>

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

    /** A cache of generated values in scope, mapping rhs -> id */
    std::unordered_map<std::string, std::string> rhs_to_id_cache;
    std::unordered_set<std::string> valid_ids_cache;

    void clear_cache();

    /** Is SIMD.js allowed in the current scope being compiled?
     * Generally set on a per module basis. */
    bool use_simd_js;

    /** Emit an expression as an assignment, then return the id of the
     * resulting var */
    std::string print_expr(Expr);

    /** Emit a vector of exprs as an assignment of an array, then return the id of the
     * resulting var */
    std::string print_expr_array(const std::vector<Expr> &);

    /** Emit a statement */
    void print_stmt(Stmt);

    /** Emit a statement to reinterpret an expression as another type */
    virtual std::string print_reinterpret(Type, Expr);

    /** Emit a version of a string that is a valid identifier in C (. is replaced with _) */
    virtual std::string print_name(const std::string &);

    /** Emit an SSA-style assignment, and set id to the freshly generated name. Return id. */
    std::string print_assignment(Type t, const std::string &rhs);

    std::string buffer_host_as_typed_array(const Type &t, const std::string &buffer_name);

    std::string make_js_int_cast(const std::string &value, const Type &src, const Type &dst);

    /** Open a new C scope (i.e. throw in a brace, increase the indent) */
    void open_scope();

    /** Close a C scope (i.e. throw in an end brace, decrease the indent) */
    void close_scope(const std::string &comment);

    struct Allocation {
        Type type;
        std::string free_function;
    };

    /** Track the types of allocations to avoid unnecessary casts. */
    Scope<Allocation> allocations;

    /** True if there is a void * __user_context parameter in the arguments. */
    bool have_user_context;

    using IRPrinter::visit;

    void visit(const Add *) override;
    void visit(const Allocate *) override;
    void visit(const And *) override;
    void visit(const AssertStmt *) override;
    void visit(const Broadcast *) override;
    void visit(const Call *) override;
    void visit(const Cast *) override;
    void visit(const Div *) override;
    void visit(const EQ *) override;
    void visit(const Evaluate *) override;
    void visit(const FloatImm *) override;
    void visit(const For *) override;
    void visit(const Free *) override;
    void visit(const GE *) override;
    void visit(const GT *) override;
    void visit(const IfThenElse *) override;
    void visit(const IntImm *) override;
    void visit(const LE *) override;
    void visit(const Let *) override;
    void visit(const LetStmt *) override;
    void visit(const Load *) override;
    void visit(const LT *) override;
    void visit(const Max *) override;
    void visit(const Min *) override;
    void visit(const Mod *) override;
    void visit(const Mul *) override;
    void visit(const NE *) override;
    void visit(const Not *) override;
    void visit(const Or *) override;
    void visit(const ProducerConsumer *) override;
    void visit(const Provide *) override;
    void visit(const Ramp *) override;
    void visit(const Realize *) override;
    void visit(const Select *) override;
    void visit(const Shuffle *) override;
    void visit(const Store *) override;
    void visit(const StringImm *) override;
    void visit(const Sub *) override;
    void visit(const UIntImm *) override;
    void visit(const Variable *) override;
    void visit_binop(const Type &t, Expr a, Expr b, const char *op, const char *simd_js_op, const Type &op_result_type = Type());

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
