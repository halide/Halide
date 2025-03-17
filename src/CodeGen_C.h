#ifndef HALIDE_CODEGEN_C_H
#define HALIDE_CODEGEN_C_H

/** \file
 *
 * Defines an IRPrinter that emits C++ code equivalent to a halide stmt
 */

#include "IRPrinter.h"
#include "Scope.h"
#include "Target.h"

namespace Halide {

struct Argument;
class Module;

namespace Internal {

struct LoweredFunc;

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
        CExternDecl,
        CPlusPlusExternDecl,
        CPlusPlusFunctionInfoHeader,
    };

    /** Initialize a C code generator pointing at a particular output
     * stream (e.g. a file, or std::cout) */
    CodeGen_C(std::ostream &dest,
              const Target &target,
              OutputKind output_kind = CImplementation,
              const std::string &include_guard = "");
    ~CodeGen_C() override;

    /** Emit the declarations contained in the module as C code. */
    void compile(const Module &module);

    /** The target we're generating code for */
    const Target &get_target() const {
        return target;
    }

    static void test();

protected:
    enum class IntegerSuffixStyle {
        PlainC = 0,
        OpenCL = 1,
        HLSL = 2
    };

    /** How to emit 64-bit integer constants */
    IntegerSuffixStyle integer_suffix_style = IntegerSuffixStyle::PlainC;

    /** Emit a declaration. */
    // @{
    void compile(const LoweredFunc &func, const MetadataNameMap &metadata_name_map);
    void compile(const Buffer<> &buffer);
    // @}

    /** This is a hook that subclasses can use to transform a function body
     * just before it is emitted -- e.g., to transform the IR to code that
     * is easier to recognize and emit. The default implementation simply
     * returns the input unchanged.
     *
     * This hook will always be called after the function declaration and
     * opening brace is emitted, so in addition to (possibly) returning
     * a modified Stmt, this function may also emit C++ code to the default
     * stream if it wishes to add some prologue at the start of the function.
     */
    virtual Stmt preprocess_function_body(const Stmt &stmt);

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
    std::string print_expr(const Expr &);

    /** Like print_expr, but cast the Expr to the given Type */
    std::string print_cast_expr(const Type &, const Expr &);

    /** Emit a statement */
    void print_stmt(const Stmt &);

    void create_assertion(const std::string &id_cond, const Expr &message);
    void create_assertion(const Expr &cond, const Expr &message);

    Expr scalarize_vector_reduce(const VectorReduce *op);
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
    virtual std::string print_reinterpret(Type, const Expr &);

    /** Emit a version of a string that is a valid identifier in C (. is replaced with _) */
    virtual std::string print_name(const std::string &);

    /** Add platform specific prologue */
    virtual void add_platform_prologue();

    /** Add typedefs for vector types. Not needed for OpenCL, might
     * use different syntax for other C-like languages. */
    virtual void add_vector_typedefs(const std::set<Type> &vector_types);

    std::unordered_map<std::string, std::string> extern_function_name_map;

    /** Bottleneck to allow customization of calls to generic Extern/PureExtern calls.  */
    virtual std::string print_extern_call(const Call *op);

    /** Convert a vector Expr into a series of scalar Exprs, then reassemble into vector of original type.  */
    std::string print_scalarized_expr(const Expr &e);

    /** Emit an SSA-style assignment, and set id to the freshly generated name. Return id. */
    virtual std::string print_assignment(Type t, const std::string &rhs);

    /** Emit free for the heap allocation. **/
    void print_heap_free(const std::string &alloc_name);

    /** Return true if only generating an interface, which may be extern "C" or C++ */
    bool is_header() {
        return output_kind == CHeader ||
               output_kind == CPlusPlusHeader ||
               output_kind == CPlusPlusFunctionInfoHeader;
    }

    /** Return true if only generating an interface, which may be extern "C" or C++ */
    bool is_extern_decl() {
        return output_kind == CExternDecl ||
               output_kind == CPlusPlusExternDecl;
    }

    /** Return true if only generating an interface, which may be extern "C" or C++ */
    bool is_header_or_extern_decl() {
        return is_header() || is_extern_decl();
    }

    /** Return true if generating C++ linkage. */
    bool is_c_plus_plus_interface() {
        return output_kind == CPlusPlusHeader ||
               output_kind == CPlusPlusImplementation ||
               output_kind == CPlusPlusExternDecl ||
               output_kind == CPlusPlusFunctionInfoHeader;
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
    Scope<> heap_allocations;

    /** True if there is a void * __user_context parameter in the arguments. */
    bool have_user_context;

    /** Track current calling convention scope. */
    bool extern_c_open = false;

    /** True if at least one gpu-based for loop is used. */
    bool uses_gpu_for_loops;

    /** Track which handle types have been forward-declared already. */
    std::set<const halide_handle_cplusplus_type *> forward_declared;

    /** If the Type is a handle type, emit a forward-declaration for it
     * if we haven't already. */
    void forward_declare_type_if_needed(const Type &t);

    void set_name_mangling_mode(NameMangling mode);

    using IRPrinter::visit;

    void visit(const Variable *) override;
    void visit(const IntImm *) override;
    void visit(const UIntImm *) override;
    void visit(const StringImm *) override;
    void visit(const FloatImm *) override;
    void visit(const Cast *) override;
    void visit(const Reinterpret *) override;
    void visit(const Add *) override;
    void visit(const Sub *) override;
    void visit(const Mul *) override;
    void visit(const Div *) override;
    void visit(const Mod *) override;
    void visit(const Max *) override;
    void visit(const Min *) override;
    void visit(const EQ *) override;
    void visit(const NE *) override;
    void visit(const LT *) override;
    void visit(const LE *) override;
    void visit(const GT *) override;
    void visit(const GE *) override;
    void visit(const And *) override;
    void visit(const Or *) override;
    void visit(const Not *) override;
    void visit(const Call *) override;
    void visit(const Select *) override;
    void visit(const Load *) override;
    void visit(const Store *) override;
    void visit(const Let *) override;
    void visit(const LetStmt *) override;
    void visit(const AssertStmt *) override;
    void visit(const ProducerConsumer *) override;
    void visit(const For *) override;
    void visit(const Ramp *) override;
    void visit(const Broadcast *) override;
    void visit(const Provide *) override;
    void visit(const Allocate *) override;
    void visit(const Free *) override;
    void visit(const Realize *) override;
    void visit(const IfThenElse *) override;
    void visit(const Evaluate *) override;
    void visit(const Shuffle *) override;
    void visit(const Prefetch *) override;
    void visit(const Fork *) override;
    void visit(const Acquire *) override;
    void visit(const Atomic *) override;
    void visit(const VectorReduce *) override;

    void visit_binop(Type t, const Expr &a, const Expr &b, const char *op);
    void visit_relop(Type t, const Expr &a, const Expr &b, const char *scalar_op, const char *vector_op);

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

    /** Are we inside an atomic node that uses mutex locks?
        This is used for detecting deadlocks from nested atomics. */
    bool inside_atomic_mutex_node = false;

    /** Emit atomic store instructions? */
    bool emit_atomic_stores = false;

    /** true if add_vector_typedefs() has been called. */
    bool using_vector_typedefs = false;

    /** Some architectures have private memory for the call stack; this
     * means a thread cannot hand pointers to stack memory to another
     * thread. Returning true here flag forces heap allocation of
     * things that might be shared, such as closures and any buffer
     * that may be used in a parallel context. */
    virtual bool is_stack_private_to_thread() const;

    void emit_argv_wrapper(const std::string &function_name,
                           const std::vector<LoweredArgument> &args);
    void emit_metadata_getter(const std::string &function_name,
                              const std::vector<LoweredArgument> &args,
                              const MetadataNameMap &metadata_name_map);
    void emit_constexpr_function_info(const std::string &function_name,
                                      const std::vector<LoweredArgument> &args,
                                      const MetadataNameMap &metadata_name_map);
    void emit_halide_free_helper(const std::string &alloc_name, const std::string &free_function);
};

}  // namespace Internal
}  // namespace Halide

#endif
