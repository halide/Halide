#ifndef HALIDE_CODEGEN_LLVM_H
#define HALIDE_CODEGEN_LLVM_H

/** \file
 *
 * Defines the base-class for all architecture-specific code
 * generators that use llvm.
 */

namespace llvm {
class Value;
class Module;
class Function;
template<bool> class IRBuilderDefaultInserter;
class ConstantFolder;
template<bool, typename, typename> class IRBuilder;
class LLVMContext;
class Type;
class StructType;
class Instruction;
class CallInst;
class ExecutionEngine;
class AllocaInst;
class Constant;
class Triple;
class MDNode;
class NamedMDNode;
class DataLayout;
class BasicBlock;
class GlobalVariable;
}

#include <map>
#include <string>
#include <vector>

#include "IRVisitor.h"
#include "Module.h"
#include "Scope.h"
#include "ModulusRemainder.h"
#include "Target.h"

namespace Halide {
namespace Internal {

/** A code generator abstract base class. Actual code generators
 * (e.g. CodeGen_X86) inherit from this. This class is responsible
 * for taking a Halide Stmt and producing llvm bitcode, machine
 * code in an object file, or machine code accessible through a
 * function pointer.
 */
class CodeGen_LLVM : public IRVisitor {
public:
    /** Create an instance of CodeGen_LLVM suitable for the target. */
    static CodeGen_LLVM *new_for_target(const Target &target,
                                        llvm::LLVMContext &context);

    virtual ~CodeGen_LLVM();

    /** Takes a halide Module and compiles it to an llvm Module. */
    virtual llvm::Module *compile(const Module &module);

    /** The target we're generating code for */
    const Target &get_target() const { return target; }

    /** Tell the code generator which LLVM context to use. */
    void set_context(llvm::LLVMContext &context);

protected:
    CodeGen_LLVM(Target t);

    /** Compile a specific halide declaration into the llvm Module. */
    // @{
    virtual void compile_func(const LoweredFunc &func);
    virtual void compile_buffer(const Buffer &buffer);
    // @}

    /** What should be passed as -mcpu, -mattrs, and related for
     * compilation. The architecture-specific code generator should
     * define these. */
    // @{
    virtual llvm::Triple get_target_triple() const = 0;
    virtual llvm::DataLayout get_data_layout() const = 0;
    virtual std::string mcpu() const = 0;
    virtual std::string mattrs() const = 0;
    virtual bool use_soft_float_abi() const = 0;
    // @}

    // What's the natural vector bit-width to use for loads, stores, etc.
    // @{
    virtual int native_vector_bits() const = 0;
    // @}

    /** Initialize internal llvm state for the enabled targets. */
    static void initialize_llvm();

    /** State needed by llvm for code generation, including the
     * current module, function, context, builder, and most recently
     * generated llvm value. */
    //@{
    static bool llvm_initialized;
    static bool llvm_X86_enabled;
    static bool llvm_ARM_enabled;
    static bool llvm_AArch64_enabled;
    static bool llvm_NVPTX_enabled;
    static bool llvm_Mips_enabled;

    llvm::Module *module;
    llvm::Function *function;
    llvm::LLVMContext *context;
    llvm::IRBuilder<true, llvm::ConstantFolder, llvm::IRBuilderDefaultInserter<true>> *builder;
    llvm::Value *value;
    llvm::MDNode *very_likely_branch;
    //@}

    /** The target we're generating code for */
    Halide::Target target;

    /** Grab all the context specific internal state. */
    virtual void init_context();
    /** Initialize the CodeGen_LLVM internal state to compile a fresh
     * module. This allows reuse of one CodeGen_LLVM object to compiled
     * multiple related modules (e.g. multiple device kernels). */
    virtual void init_module();

    /** Run all of llvm's optimization passes on the module. */
    void optimize_module();

    /** Add an entry to the symbol table, hiding previous entries with
     * the same name. Call this when new values come into scope. */
    void sym_push(const std::string &name, llvm::Value *value);

    /** Remove an entry for the symbol table, revealing any previous
     * entries with the same name. Call this when values go out of
     * scope. */
    void sym_pop(const std::string &name);

    /** Fetch an entry from the symbol table. If the symbol is not
     * found, it either errors out (if the second arg is true), or
     * returns NULL. */
    llvm::Value* sym_get(const std::string &name,
                         bool must_succeed = true) const;

    /** Test if an item exists in the symbol table. */
    bool sym_exists(const std::string &name) const;

    /** Some useful llvm types */
    // @{
    llvm::Type *void_t, *i1, *i8, *i16, *i32, *i64, *f16, *f32, *f64;
    llvm::StructType *buffer_t_type, *metadata_t_type, *argument_t_type, *scalar_value_t_type;
    // @}

    /** Some useful llvm types for subclasses */
    // @{
    llvm::Type *i8x8, *i8x16, *i8x32;
    llvm::Type *i16x4, *i16x8, *i16x16;
    llvm::Type *i32x2, *i32x4, *i32x8;
    llvm::Type *i64x2, *i64x4;
    llvm::Type *f32x2, *f32x4, *f32x8;
    llvm::Type *f64x2, *f64x4;
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
    Expr wild_f32x2; // 64-bit floats
    Expr wild_f32x4, wild_f64x2; // 128-bit floats
    Expr wild_f32x8, wild_f64x4; // 256-bit floats

    // Wildcards for a varying number of lanes.
    Expr wild_u1x_, wild_i8x_, wild_u8x_, wild_i16x_, wild_u16x_;
    Expr wild_i32x_, wild_u32x_, wild_i64x_, wild_u64x_;
    Expr wild_f32x_, wild_f64x_;

    Expr min_i8, max_i8, max_u8;
    Expr min_i16, max_i16, max_u16;
    Expr min_i32, max_i32, max_u32;
    Expr min_i64, max_i64, max_u64;
    Expr min_f32, max_f32, min_f64, max_f64;
    // @}

    /** Emit code that evaluates an expression, and return the llvm
     * representation of the result of the expression. */
    llvm::Value *codegen(Expr);

    /** Emit code that runs a statement. */
    void codegen(Stmt);

    /** Codegen a vector Expr by codegenning each lane and combining. */
    void scalarize(Expr);

    /** Take an llvm Value representing a pointer to a buffer_t,
     * and populate the symbol table with its constituent parts.
     */
    void push_buffer(const std::string &name, llvm::Value *buffer);
    void pop_buffer(const std::string &name);

    /* Call this at the location of object creation to register how an
     * object should be destroyed. This does three things:
     * 1) Emits code here that puts the object in a unique
     * null-initialized stack slot
     * 2) Adds an instruction to the error handling block that calls the
     * destructor on that stack slot if it's not null.
     * 3) Returns that instruction, so that you can also insert a
     * clone of it where you actually want to delete the object in the
     * non-error case. */
    llvm::Instruction *register_destructor(llvm::Function *destructor_fn, llvm::Value *obj);

    /** Retrieves the block containing the error handling
     * code. Creates it if it doesn't already exist for this
     * function. */
    llvm::BasicBlock *get_destructor_block();

    /** Codegen an assertion. If false, returns the error code (if not
     * null), or evaluates and returns the message, which must be an
     * Int(32) expression. */
    // @{
    void create_assertion(llvm::Value *condition, Expr message, llvm::Value *error_code = NULL);

    // @}

    /** Put a string constant in the module as a global variable and return a pointer to it. */
    llvm::Constant *create_string_constant(const std::string &str);

    /** Put a binary blob in the module as a global variable and return a pointer to it. */
    llvm::Constant *create_constant_binary_blob(const std::vector<char> &data, const std::string &name);

    /** Widen an llvm scalar into an llvm vector with the given number of lanes. */
    llvm::Value *create_broadcast(llvm::Value *, int width);

    /** Given an llvm value representing a pointer to a buffer_t, extract various subfields.
     * The *_ptr variants return a pointer to the struct element, while the basic variants
     * load the actual value. */
    // @{
    llvm::Value *buffer_host(llvm::Value *);
    llvm::Value *buffer_dev(llvm::Value *);
    llvm::Value *buffer_host_dirty(llvm::Value *);
    llvm::Value *buffer_dev_dirty(llvm::Value *);
    llvm::Value *buffer_min(llvm::Value *, int);
    llvm::Value *buffer_extent(llvm::Value *, int);
    llvm::Value *buffer_stride(llvm::Value *, int);
    llvm::Value *buffer_elem_size(llvm::Value *);
    llvm::Value *buffer_host_ptr(llvm::Value *);
    llvm::Value *buffer_dev_ptr(llvm::Value *);
    llvm::Value *buffer_host_dirty_ptr(llvm::Value *);
    llvm::Value *buffer_dev_dirty_ptr(llvm::Value *);
    llvm::Value *buffer_min_ptr(llvm::Value *, int);
    llvm::Value *buffer_extent_ptr(llvm::Value *, int);
    llvm::Value *buffer_stride_ptr(llvm::Value *, int);
    llvm::Value *buffer_elem_size_ptr(llvm::Value *);
    // @}

    /** Generate a pointer into a named buffer at a given index, of a
     * given type. The index counts according to the scalar type of
     * the type passed in. */
    // @{
    llvm::Value *codegen_buffer_pointer(std::string buffer, Type type, llvm::Value *index);
    llvm::Value *codegen_buffer_pointer(std::string buffer, Type type, Expr index);
    // @}

    /** Mark a load or store with type-based-alias-analysis metadata
     * so that llvm knows it can reorder loads and stores across
     * different buffers */
    void add_tbaa_metadata(llvm::Instruction *inst, std::string buffer, Expr index);

    using IRVisitor::visit;

    /** Generate code for various IR nodes. These can be overridden by
     * architecture-specific code to perform peephole
     * optimizations. The result of each is stored in \ref value */
    // @{
    virtual void visit(const IntImm *);
    virtual void visit(const FloatImm *);
    virtual void visit(const StringImm *);
    virtual void visit(const Cast *);
    virtual void visit(const Variable *);
    virtual void visit(const Add *);
    virtual void visit(const Sub *);
    virtual void visit(const Mul *);
    virtual void visit(const Div *);
    virtual void visit(const Mod *);
    virtual void visit(const Min *);
    virtual void visit(const Max *);
    virtual void visit(const EQ *);
    virtual void visit(const NE *);
    virtual void visit(const LT *);
    virtual void visit(const LE *);
    virtual void visit(const GT *);
    virtual void visit(const GE *);
    virtual void visit(const And *);
    virtual void visit(const Or *);
    virtual void visit(const Not *);
    virtual void visit(const Select *);
    virtual void visit(const Load *);
    virtual void visit(const Ramp *);
    virtual void visit(const Broadcast *);
    virtual void visit(const Call *);
    virtual void visit(const Let *);
    virtual void visit(const LetStmt *);
    virtual void visit(const AssertStmt *);
    virtual void visit(const Pipeline *);
    virtual void visit(const For *);
    virtual void visit(const Store *);
    virtual void visit(const Block *);
    virtual void visit(const IfThenElse *);
    virtual void visit(const Evaluate *);
    // @}

    /** Generate code for an allocate node. It has no default
     * implementation - it must be handled in an architecture-specific
     * way. */
    virtual void visit(const Allocate *) = 0;

    /** Generate code for a free node. It has no default
     * implementation and must be handled in an architecture-specific
     * way. */
    virtual void visit(const Free *) = 0;

    /** These IR nodes should have been removed during
     * lowering. CodeGen_LLVM will error out if they are present */
    // @{
    virtual void visit(const Provide *);
    virtual void visit(const Realize *);
    // @}

    /** If we have to bail out of a pipeline midway, this should
     * inject the appropriate target-specific cleanup code. */
    virtual void prepare_for_early_exit() {}

    /** Get the llvm type equivalent to the given halide type in the
     * current context. */
    llvm::Type *llvm_type_of(Type);

    /** Perform an alloca at the function entrypoint. Will be cleaned
     * on function exit. */
    llvm::Value *create_alloca_at_entry(llvm::Type *type, int n,
                                        bool zero_initialize = false,
                                        const std::string &name = "");

    /** Which buffers came in from the outside world (and so we can't
     * guarantee their alignment) */
    std::set<std::string> might_be_misaligned;

    /** The user_context argument. May be a constant null if the
     * function is being compiled without a user context. */
    llvm::Value *get_user_context() const;

    /** Implementation of the intrinsic call to
     * interleave_vectors. This implementation allows for interleaving
     * an arbitrary number of vectors.*/
    llvm::Value *interleave_vectors(Type, const std::vector<Expr> &);

    /** Generate a call to a vector intrinsic or runtime inlined
     * function. The arguments are sliced up into vectors of the width
     * given by 'intrin_vector_width', the intrinsic is called on each
     * piece, then the results (if any) are concatenated back together
     * into the original type 't'. For the version that takes an
     * llvm::Type *, the type may be void, so the vector width of the
     * arguments must be specified explicitly as
     * 'called_vector_width'. */
    // @{
    llvm::Value *call_intrin(Type t, int intrin_vector_width,
                             const std::string &name, std::vector<Expr>);
    llvm::Value *call_intrin(llvm::Type *t, int intrin_vector_width,
                             const std::string &name, std::vector<llvm::Value *>);
    // @}

    /** Take a slice of lanes out of an llvm vector. Pads with undefs
     * if you ask for more lanes than the vector has. */
    llvm::Value *slice_vector(llvm::Value *vec, int start, int extent);

    /** Concatenate a bunch of llvm vectors. Must be of the same type. */
    llvm::Value *concat_vectors(const std::vector<llvm::Value *> &);

    /** Go looking for a vector version of a runtime function. Will
     * return the best match. Matches in the following order:
     *
     * 1) The requested vector width.
     *
     * 2) The width which is the smallest power of two
     * greater than or equal to the vector width.
     *
     * 3) All the factors of 2) greater than one, in decreasing order.
     *
     * 4) The smallest power of two not yet tried.
     *
     * So for a 5-wide vector, it tries: 5, 8, 4, 2, 16.
     *
     * If there's no match, returns (NULL, 0).
     */
    std::pair<llvm::Function *, int> find_vector_runtime_function(const std::string &name, int width);

private:

    /** All the values in scope at the current code location during
     * codegen. Use sym_push and sym_pop to access. */
    Scope<llvm::Value *> symbol_table;

    /** Alignment info for Int(32) variables in scope. */
    Scope<ModulusRemainder> alignment_info;

    /** String constants already emitted to the module. Tracked to
     * prevent emitting the same string many times. */
    std::map<std::string, llvm::Constant *> string_constants;

    /** A basic block to branch to on error that triggers all
     * destructors. As destructors are registered, code gets added
     * to this block. */
    llvm::BasicBlock *destructor_block;

    /** Embed an instance of halide_filter_metadata_t in the code, using
     * the given name (by convention, this should be ${FUNCTIONNAME}_metadata)
     * as extern "C" linkage.
     */
    llvm::Constant* embed_metadata(const std::string &metadata_name,
        const std::string &function_name, const std::vector<Argument> &args);

    /** Embed a constant expression as a global variable. */
    llvm::Constant *embed_constant_expr(Expr e);

    void register_metadata(const std::string &name, llvm::Constant *metadata, llvm::Function *argv_wrapper);
};

}

/** Given a Halide module, generate an llvm::Module. */
EXPORT llvm::Module *codegen_llvm(const Module &module, llvm::LLVMContext &context);

}

#endif
