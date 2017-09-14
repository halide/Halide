#ifndef HALIDE_CODEGEN_C_H
#define HALIDE_CODEGEN_C_H

/** \file
 *
 * Defines an IRPrinter that emits C++ code equivalent to a halide stmt
 */

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
class CodeGen_C : public IRPrinter {
public:
    enum OutputKind {
        CHeader,
        CPlusPlusHeader,
        CImplementation,
        CPlusPlusImplementation,
    };

    /** Initialize a C code generator pointing at a particular output
     * stream (e.g. a file, or std::cout) */
    CodeGen_C(std::ostream &dest,
              Target target,
              OutputKind output_kind = CImplementation,
              const std::string &include_guard = "");
    ~CodeGen_C();

    /** Emit the declarations contained in the module as C code. */
    void compile(const Module &module);

    /** The target we're generating code for */
    const Target &get_target() const { return target; }

    EXPORT static void test();

protected:

    /** Emit a declaration. */
    // @{
    virtual void compile(const LoweredFunc &func);
    virtual void compile(const Buffer<> &buffer);
    // @}

    /** An ID for the most recently generated ssa variable */
    std::string id;

    /** The target being generated for. */
    Target target;

    /** Controls whether this instance is generating declarations or
     * definitions and whether the interface us extern "C" or C++. */
    OutputKind output_kind;

    /** A cache of generated values in scope */
    std::map<std::string, std::string> cache;

    /** Emit an expression as an assignment, then return the id of the
     * resulting var */
    std::string print_expr(Expr);

    /** Like print_expr, but cast the Expr to the given Type */
    std::string print_cast_expr(const Type &, Expr);

    /** Emit a statement */
    void print_stmt(Stmt);

    void create_assertion(const std::string &id_cond, const std::string &id_msg);
    void create_assertion(const std::string &id_cond, Expr message);
    void create_assertion(Expr cond, Expr message);

    enum AppendSpaceIfNeeded {
        DoNotAppendSpace,
        AppendSpace,
    };

    /** Emit the C name for a halide type. If space_option is AppendSpace,
     *  and there should be a space between the type and the next token,
     *  one is appended. (This allows both "int foo" and "Foo *foo" to be
     *  formatted correctly. Otherwise the latter is "Foo * foo".)
     */
    virtual std::string print_type(Type, AppendSpaceIfNeeded space_option = DoNotAppendSpace);

    /** Emit a statement to reinterpret an expression as another type */
    virtual std::string print_reinterpret(Type, Expr);

    /** Emit a version of a string that is a valid identifier in C (. is replaced with _) */
    virtual std::string print_name(const std::string &);

    /** Add typedefs for vector types. Not needed for OpenCL, might
     * use different syntax for other C-like languages. */
    virtual void add_vector_typedefs(const std::set<Type> &vector_types);

    /** Bottleneck to allow customization of calls to generic Extern/PureExtern calls.  */
    virtual std::string print_extern_call(const Call *op);

    /** Convert a vector Expr into a series of scalar Exprs, then reassemble into vector of original type.  */
    std::string print_scalarized_expr(Expr e);

    /** Emit an SSA-style assignment, and set id to the freshly generated name. Return id. */
    std::string print_assignment(Type t, const std::string &rhs);

    /** Return true if only generating an interface, which may be extern "C" or C++ */
    bool is_header() {
        return output_kind == CHeader ||
               output_kind == CPlusPlusHeader;
    }

    /** Return true if generating C++ linkage. */
    bool is_c_plus_plus_interface() {
        return output_kind == CPlusPlusHeader ||
               output_kind == CPlusPlusImplementation;
    }

    /** Open a new C scope (i.e. throw in a brace, increase the indent) */
    void open_scope();

    /** Close a C scope (i.e. throw in an end brace, decrease the indent) */
    void close_scope(const std::string &comment);

    struct Allocation {
        Type type;
    };

    /** Track the types of allocations to avoid unnecessary casts. */
    Scope<Allocation> allocations;

    /** Track which allocations actually went on the heap. */
    Scope<int> heap_allocations;

    /** True if there is a void * __user_context parameter in the arguments. */
    bool have_user_context;

    /** Track current calling convention scope. */
    bool extern_c_open;

    /** True if at least one gpu-based for loop is used. */
    bool uses_gpu_for_loops;

    /** Track which handle types have been forward-declared already. */
    std::set<const halide_handle_cplusplus_type *> forward_declared;

    /** If the Type is a handle type, emit a forward-declaration for it
     * if we haven't already. */
    void forward_declare_type_if_needed(const Type &t);

    void set_name_mangling_mode(NameMangling mode);

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
    void visit(const Store *);
    void visit(const Let *);
    void visit(const LetStmt *);
    void visit(const AssertStmt *);
    void visit(const ProducerConsumer *);
    void visit(const For *);
    void visit(const Ramp *);
    void visit(const Broadcast *);
    void visit(const Provide *);
    void visit(const Allocate *);
    void visit(const Free *);
    void visit(const Realize *);
    void visit(const IfThenElse *);
    void visit(const Evaluate *);
    void visit(const Shuffle *);
    void visit(const Prefetch *);

    void visit_binop(Type t, Expr a, Expr b, const char *op);

    template<typename T>
    static std::string with_sep(const std::vector<T> &v, const std::string &sep) {
        std::ostringstream o;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i > 0) {
                o << sep;
            }
            o << v[i];
        }
        return o.str();
    }

    template<typename T>
    static std::string with_commas(const std::vector<T> &v) {
        return with_sep<T>(v, ", ");
    }
};

}
}

#endif
