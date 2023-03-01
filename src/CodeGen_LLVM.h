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
class FunctionType;
class IRBuilderDefaultInserter;
class ConstantFolder;
template<typename, typename>
class IRBuilder;
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
}  // namespace llvm

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "IRVisitor.h"
#include "Module.h"
#include "Scope.h"
#include "Target.h"

namespace Halide {

struct ExternSignature;

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
    static std::unique_ptr<CodeGen_LLVM> new_for_target(const Target &target, llvm::LLVMContext &context);

    /** Takes a halide Module and compiles it to an llvm Module. */
    virtual std::unique_ptr<llvm::Module> compile(const Module &module);

    /** The target we're generating code for */
    const Target &get_target() const {
        return target;
    }

    /** Tell the code generator which LLVM context to use. */
    void set_context(llvm::LLVMContext &context);

    /** Initialize internal llvm state for the enabled targets. */
    static void initialize_llvm();

    static std::unique_ptr<llvm::Module> compile_trampolines(
        const Target &target,
        llvm::LLVMContext &context,
        const std::string &suffix,
        const std::vector<std::pair<std::string, ExternSignature>> &externs);

    size_t get_requested_alloca_total() const {
        return requested_alloca_total;
    }

protected:
    CodeGen_LLVM(const Target &t);

    /** Compile a specific halide declaration into the llvm Module. */
    // @{
    virtual void compile_func(const LoweredFunc &func, const std::string &simple_name, const std::string &extern_name);
    virtual void compile_buffer(const Buffer<> &buffer);
    // @}

    /** Helper functions for compiling Halide functions to llvm
     * functions. begin_func performs all the work necessary to begin
     * generating code for a function with a given argument list with
     * the IRBuilder. A call to begin_func should be a followed by a
     * call to end_func with the same arguments, to generate the
     * appropriate cleanup code. */
    // @{
    virtual void begin_func(LinkageType linkage, const std::string &simple_name,
                            const std::string &extern_name, const std::vector<LoweredArgument> &args);
    virtual void end_func(const std::vector<LoweredArgument> &args);
    // @}

    /** What should be passed as -mcpu (warning: implies attrs!), -mattrs,
     *  and related for compilation. The architecture-specific code generator
     *  should define these.
     *
     *  `mcpu_target()` - target this specific CPU, in the sense of the allowed
     *  ISA sets *and* the CPU-specific tuning/assembly instruction scheduling.
     *
     *  `mcpu_tune()` - expect that we will be running on this specific CPU,
     *  so perform CPU-specific tuning/assembly instruction scheduling, *but*
     *  DON'T sacrifice the portability, support running on other CPUs, only
     *  make use of the ISAs that are enabled by `mcpu_target()`+`mattrs()`.
     */
    // @{
    virtual std::string mcpu_target() const = 0;
    virtual std::string mcpu_tune() const = 0;
    virtual std::string mattrs() const = 0;
    virtual std::string mabi() const;
    virtual bool use_soft_float_abi() const = 0;
    virtual bool use_pic() const;
    // @}

    /** Should indexing math be promoted to 64-bit on platforms with
     * 64-bit pointers? */
    virtual bool promote_indices() const {
        return true;
    }

    /** What's the natural vector bit-width to use for loads, stores, etc. */
    virtual int native_vector_bits() const = 0;

    /** Used to decide whether to break a vector up into multiple smaller
     * operations. This is the largest size the architecture supports. */
    virtual int maximum_vector_bits() const {
        return native_vector_bits();
    }
    /** For architectures that have vscale vectors, return the constant vscale to use.
     * Default of 0 means do not use vscale vectors. Generally will depend on
     * the target flags and vector_bits settings.
     */
    virtual int target_vscale() const {
        return 0;
    }

    /** Return the type in which arithmetic should be done for the
     * given storage type. */
    virtual Type upgrade_type_for_arithmetic(const Type &) const;

    /** Return the type that a given Halide type should be
     * stored/loaded from memory as. */
    virtual Type upgrade_type_for_storage(const Type &) const;

    /** Return the type that a Halide type should be passed in and out
     * of functions as. */
    virtual Type upgrade_type_for_argument_passing(const Type &) const;

    std::unique_ptr<llvm::Module> module;
    llvm::Function *function;
    llvm::LLVMContext *context;
    std::unique_ptr<llvm::IRBuilder<llvm::ConstantFolder, llvm::IRBuilderDefaultInserter>> builder;
    llvm::Value *value;
    llvm::MDNode *very_likely_branch;
    llvm::MDNode *default_fp_math_md;
    llvm::MDNode *strict_fp_math_md;
    std::vector<LoweredArgument> current_function_args;

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
     * returns nullptr. */
    llvm::Value *sym_get(const std::string &name,
                         bool must_succeed = true) const;

    /** Test if an item exists in the symbol table. */
    bool sym_exists(const std::string &name) const;

    /** Given a Halide ExternSignature, return the equivalent llvm::FunctionType. */
    llvm::FunctionType *signature_to_type(const ExternSignature &signature);

    /** Some useful llvm types */
    // @{
    llvm::Type *void_t, *i1_t, *i8_t, *i16_t, *i32_t, *i64_t, *f16_t, *f32_t, *f64_t;
    llvm::StructType *halide_buffer_t_type,
        *type_t_type,
        *dimension_t_type,
        *metadata_t_type,
        *argument_t_type,
        *scalar_value_t_type,
        *device_interface_t_type,
        *pseudostack_slot_t_type,
        *semaphore_t_type;

    // @}

    /** Some wildcard variables used for peephole optimizations in
     * subclasses */
    // @{
    Expr wild_u1x_, wild_i8x_, wild_u8x_, wild_i16x_, wild_u16x_;
    Expr wild_i32x_, wild_u32x_, wild_i64x_, wild_u64x_;
    Expr wild_f32x_, wild_f64x_;

    // Wildcards for scalars.
    Expr wild_u1_, wild_i8_, wild_u8_, wild_i16_, wild_u16_;
    Expr wild_i32_, wild_u32_, wild_i64_, wild_u64_;
    Expr wild_f32_, wild_f64_;
    // @}

    /** Emit code that evaluates an expression, and return the llvm
     * representation of the result of the expression. */
    llvm::Value *codegen(const Expr &);

    /** Emit code that runs a statement. */
    void codegen(const Stmt &);

    /** Codegen a vector Expr by codegenning each lane and combining. */
    void scalarize(const Expr &);

    /** Some destructors should always be called. Others should only
     * be called if the pipeline is exiting with an error code. */
    enum DestructorType { Always,
                          OnError,
                          OnSuccess };

    /* Call this at the location of object creation to register how an
     * object should be destroyed. This does three things:
     * 1) Emits code here that puts the object in a unique
     * null-initialized stack slot
     * 2) Adds an instruction to the destructor block that calls the
     * destructor on that stack slot if it's not null.
     * 3) Returns that stack slot, so you can neuter the destructor
     * (by storing null to the stack slot) or destroy the object early
     * (by calling trigger_destructor).
     */
    llvm::Value *register_destructor(llvm::Function *destructor_fn, llvm::Value *obj, DestructorType when);

    /** Call a destructor early. Pass in the value returned by register destructor. */
    void trigger_destructor(llvm::Function *destructor_fn, llvm::Value *stack_slot);

    /** Retrieves the block containing the error handling
     * code. Creates it if it doesn't already exist for this
     * function. */
    llvm::BasicBlock *get_destructor_block();

    /** Codegen an assertion. If false, returns the error code (if not
     * null), or evaluates and returns the message, which must be an
     * Int(32) expression. */
    // @{
    void create_assertion(llvm::Value *condition, const Expr &message, llvm::Value *error_code = nullptr);
    // @}

    /** Codegen a block of asserts with pure conditions */
    void codegen_asserts(const std::vector<const AssertStmt *> &asserts);

    /** Return the the pipeline with the given error code. Will run
     * the destructor block. */
    void return_with_error_code(llvm::Value *error_code);

    /** Put a string constant in the module as a global variable and return a pointer to it. */
    llvm::Constant *create_string_constant(const std::string &str);

    /** Put a binary blob in the module as a global variable and return a pointer to it. */
    llvm::Constant *create_binary_blob(const std::vector<char> &data, const std::string &name, bool constant = true);

    /** Widen an llvm scalar into an llvm vector with the given number of lanes. */
    llvm::Value *create_broadcast(llvm::Value *, int lanes);

    /** Generate a pointer into a named buffer at a given index, of a
     * given type. The index counts according to the scalar type of
     * the type passed in. */
    // @{
    llvm::Value *codegen_buffer_pointer(const std::string &buffer, Type type, llvm::Value *index);
    llvm::Value *codegen_buffer_pointer(const std::string &buffer, Type type, Expr index);
    llvm::Value *codegen_buffer_pointer(llvm::Value *base_address, Type type, Expr index);
    llvm::Value *codegen_buffer_pointer(llvm::Value *base_address, Type type, llvm::Value *index);
    // @}

    /** Return type string for LLVM type using LLVM IR intrinsic type mangling.
     * E.g. ".i32 or ".f32" for scalars, ".p0" for pointers,
     * ".nxv4i32" for a scalable vector of four 32-bit integers,
     * or ".v4f32" for a fixed vector of four 32-bit floats.
     * The dot is included in the result.
     */
    std::string mangle_llvm_type(llvm::Type *type);

    /** Turn a Halide Type into an llvm::Value representing a constant halide_type_t */
    llvm::Value *make_halide_type_t(const Type &);

    /** Mark a load or store with type-based-alias-analysis metadata
     * so that llvm knows it can reorder loads and stores across
     * different buffers */
    void add_tbaa_metadata(llvm::Instruction *inst, std::string buffer, const Expr &index);

    /** Get a unique name for the actual block of memory that an
     * allocate node uses. Used so that alias analysis understands
     * when multiple Allocate nodes shared the same memory. */
    virtual std::string get_allocation_name(const std::string &n) {
        return n;
    }

    /** Add the appropriate function attribute to tell LLVM that the function
     * doesn't access memory. */
    void function_does_not_access_memory(llvm::Function *fn);

    using IRVisitor::visit;

    /** Generate code for various IR nodes. These can be overridden by
     * architecture-specific code to perform peephole
     * optimizations. The result of each is stored in \ref value */
    // @{
    void visit(const IntImm *) override;
    void visit(const UIntImm *) override;
    void visit(const FloatImm *) override;
    void visit(const StringImm *) override;
    void visit(const Cast *) override;
    void visit(const Reinterpret *) override;
    void visit(const Variable *) override;
    void visit(const Add *) override;
    void visit(const Sub *) override;
    void visit(const Mul *) override;
    void visit(const Div *) override;
    void visit(const Mod *) override;
    void visit(const Min *) override;
    void visit(const Max *) override;
    void visit(const EQ *) override;
    void visit(const NE *) override;
    void visit(const LT *) override;
    void visit(const LE *) override;
    void visit(const GT *) override;
    void visit(const GE *) override;
    void visit(const And *) override;
    void visit(const Or *) override;
    void visit(const Not *) override;
    void visit(const Select *) override;
    void visit(const Load *) override;
    void visit(const Ramp *) override;
    void visit(const Broadcast *) override;
    void visit(const Call *) override;
    void visit(const Let *) override;
    void visit(const LetStmt *) override;
    void visit(const AssertStmt *) override;
    void visit(const ProducerConsumer *) override;
    void visit(const For *) override;
    void visit(const Store *) override;
    void visit(const Block *) override;
    void visit(const IfThenElse *) override;
    void visit(const Evaluate *) override;
    void visit(const Shuffle *) override;
    void visit(const VectorReduce *) override;
    void visit(const Prefetch *) override;
    void visit(const Atomic *) override;
    // @}

    /** Generate code for an allocate node. It has no default
     * implementation - it must be handled in an architecture-specific
     * way. */
    void visit(const Allocate *) override = 0;

    /** Generate code for a free node. It has no default
     * implementation and must be handled in an architecture-specific
     * way. */
    void visit(const Free *) override = 0;

    /** These IR nodes should have been removed during
     * lowering. CodeGen_LLVM will error out if they are present */
    // @{
    void visit(const Provide *) override;
    void visit(const Realize *) override;
    // @}

    /** If we have to bail out of a pipeline midway, this should
     * inject the appropriate target-specific cleanup code. */
    virtual void prepare_for_early_exit() {
    }

    /** Get the llvm type equivalent to the given halide type in the
     * current context. */
    virtual llvm::Type *llvm_type_of(const Type &) const;

    /** Get the llvm type equivalent to a given halide type. If
     * effective_vscale is nonzero and the type is a vector type with lanes
     * a multiple of effective_vscale, a scalable vector type is generated
     * with total lanes divided by effective_vscale. That is a scalable
     * vector intended to be used with a fixed vscale of effective_vscale.
     */
    llvm::Type *llvm_type_of(llvm::LLVMContext *context, Halide::Type t,
                             int effective_vscale) const;

    /** Perform an alloca at the function entrypoint. Will be cleaned
     * on function exit. */
    llvm::Value *create_alloca_at_entry(llvm::Type *type, int n,
                                        bool zero_initialize = false,
                                        const std::string &name = "");

    /** A (very) conservative guess at the size of all alloca() storage requested
     * (including alignment padding). It's currently meant only to be used as
     * a very coarse way to ensure there is enough stack space when testing
     * on the WebAssembly backend.
     *
     * It is *not* meant to be a useful proxy for "stack space needed", for a
     * number of reasons:
     * - allocas with non-overlapping lifetimes will share space
     * - on some backends, LLVM may promote register-sized allocas into registers
     * - while this accounts for alloca() calls we know about, it doesn't attempt
     *   to account for stack spills, function call overhead, etc.
     */
    size_t requested_alloca_total = 0;

    /** The user_context argument. May be a constant null if the
     * function is being compiled without a user context. */
    llvm::Value *get_user_context() const;

    /** Implementation of the intrinsic call to
     * interleave_vectors. This implementation allows for interleaving
     * an arbitrary number of vectors.*/
    virtual llvm::Value *interleave_vectors(const std::vector<llvm::Value *> &);

    /** Description of an intrinsic function overload. Overloads are resolved
     * using both argument and return types. The scalar types of the arguments
     * and return type must match exactly for an overload resolution to succeed. */
    struct Intrinsic {
        Type result_type;
        std::vector<Type> arg_types;
        llvm::Function *impl;

        Intrinsic(Type result_type, std::vector<Type> arg_types, llvm::Function *impl)
            : result_type(result_type), arg_types(std::move(arg_types)), impl(impl) {
        }
    };
    /** Mapping of intrinsic functions to the various overloads implementing it. */
    std::map<std::string, std::vector<Intrinsic>> intrinsics;

    /** Get an LLVM intrinsic declaration. If it doesn't exist, it will be created. */
    llvm::Function *get_llvm_intrin(const Type &ret_type, const std::string &name, const std::vector<Type> &arg_types, bool scalars_are_vectors = false);
    llvm::Function *get_llvm_intrin(llvm::Type *ret_type, const std::string &name, const std::vector<llvm::Type *> &arg_types);
    /** Declare an intrinsic function that participates in overload resolution. */
    llvm::Function *declare_intrin_overload(const std::string &name, const Type &ret_type, const std::string &impl_name, std::vector<Type> arg_types, bool scalars_are_vectors = false);
    void declare_intrin_overload(const std::string &name, const Type &ret_type, llvm::Function *impl, std::vector<Type> arg_types);
    /** Call an overloaded intrinsic function. Returns nullptr if no suitable overload is found. */
    llvm::Value *call_overloaded_intrin(const Type &result_type, const std::string &name, const std::vector<Expr> &args);

    /** Generate a call to a vector intrinsic or runtime inlined
     * function. The arguments are sliced up into vectors of the width
     * given by 'intrin_lanes', the intrinsic is called on each
     * piece, then the results (if any) are concatenated back together
     * into the original type 't'. For the version that takes an
     * llvm::Type *, the type may be void, so the vector width of the
     * arguments must be specified explicitly as
     * 'called_lanes'. */
    // @{
    llvm::Value *call_intrin(const Type &t, int intrin_lanes,
                             const std::string &name, std::vector<Expr>);
    llvm::Value *call_intrin(const Type &t, int intrin_lanes,
                             llvm::Function *intrin, std::vector<Expr>);
    llvm::Value *call_intrin(const llvm::Type *t, int intrin_lanes,
                             const std::string &name, std::vector<llvm::Value *>,
                             bool scalable_vector_result = false, bool is_reduction = false);
    llvm::Value *call_intrin(const llvm::Type *t, int intrin_lanes,
                             llvm::Function *intrin, std::vector<llvm::Value *>,
                             bool is_reduction = false);
    // @}

    /** Take a slice of lanes out of an llvm vector. Pads with undefs
     * if you ask for more lanes than the vector has. */
    virtual llvm::Value *slice_vector(llvm::Value *vec, int start, int extent);

    /** Concatenate a bunch of llvm vectors. Must be of the same type. */
    virtual llvm::Value *concat_vectors(const std::vector<llvm::Value *> &);

    /** Create an LLVM shuffle vectors instruction. */
    virtual llvm::Value *shuffle_vectors(llvm::Value *a, llvm::Value *b,
                                         const std::vector<int> &indices);
    /** Shorthand for shuffling a single vector. */
    llvm::Value *shuffle_vectors(llvm::Value *v, const std::vector<int> &indices);

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
     * If there's no match, returns (nullptr, 0).
     */
    std::pair<llvm::Function *, int> find_vector_runtime_function(const std::string &name, int lanes);

    virtual bool supports_atomic_add(const Type &t) const;

    /** Compile a horizontal reduction that starts with an explicit
     * initial value. There are lots of complex ways to peephole
     * optimize this pattern, especially with the proliferation of
     * dot-product instructions, and they can usefully share logic
     * across backends. */
    virtual void codegen_vector_reduce(const VectorReduce *op, const Expr &init);

    /** Are we inside an atomic node that uses mutex locks?
        This is used for detecting deadlocks from nested atomics & illegal vectorization. */
    bool inside_atomic_mutex_node;

    /** Emit atomic store instructions? */
    bool emit_atomic_stores;

    /** Can we call this operation with float16 type?
        This is used to avoid "emulated" equivalent code-gen in case target has FP16 feature **/
    virtual bool supports_call_as_float16(const Call *op) const;

    /** Ensure that a vector value is either fixed or vscale depending to match desired_type.
     */
    llvm::Value *normalize_fixed_scalable_vector_type(llvm::Type *desired_type, llvm::Value *result);

    /** Convert an LLVM fixed vector value to the corresponding vscale vector value. */
    llvm::Value *fixed_to_scalable_vector_type(llvm::Value *fixed);

    /** Convert an LLVM vscale vector value to the corresponding fixed vector value. */
    llvm::Value *scalable_to_fixed_vector_type(llvm::Value *scalable);

    /** Get number of vector elements, taking into account scalable vectors. Returns 1 for scalars. */
    int get_vector_num_elements(const llvm::Type *t);

    /** Interface to abstract vector code generation as LLVM is now
     * providing multiple options to express even simple vector
     * operations. Specifically traditional fixed length vectors, vscale
     * based variable length vectors, and the vector predicate based approach
     * where an explict length is passed with each instruction.
     */
    // @{
    enum class VectorTypeConstraint {
        None,    /// Use default for current target.
        Fixed,   /// Force use of fixed size vectors.
        VScale,  /// For use of scalable vectors.
    };
    llvm::Type *get_vector_type(llvm::Type *, int n,
                                VectorTypeConstraint type_constraint = VectorTypeConstraint::None) const;
    // @}

    llvm::Constant *get_splat(int lanes, llvm::Constant *value,
                              VectorTypeConstraint type_constraint = VectorTypeConstraint::None) const;

    /** Support for generating LLVM vector predication intrinsics
     * ("@llvm.vp.*" and "@llvm.experimental.vp.*")
     */
    // @{
    /** Struct to hold descriptor for an argument to a vector
     *  predicated intrinsic. This includes the value, whether the
     *  type of the argument should be mangled into the intrisic name
     *  and if so, where, and the alignment for pointer arguments. */
    struct VPArg {
        llvm::Value *value;
        // If provided, put argument's type into the intrinsic name via LLVM IR type mangling.
        std::optional<size_t> mangle_index;
        int alignment;
        VPArg(llvm::Value *value, std::optional<size_t> mangle_index = std::nullopt, int32_t alignment = 0)
            : value(value), mangle_index(mangle_index), alignment(alignment) {
        }
    };

    /** Type indicating an intrinsic does not take a mask. */
    struct NoMask {
    };

    /** Type indicating mask to use is all true -- all lanes enabled. */
    struct AllEnabledMask {
    };

    /** Predication mask using the above two types for special cases
     *   and an llvm::Value for the general one. */
    using MaskVariant = std::variant<NoMask, AllEnabledMask, llvm::Value *>;

    /** Generate a vector predicated comparison intrinsic call if
     * use_llvm_vp_intrinsics is true and result_type is a vector
     * type. If generated, assigns result of vp intrinsic to value and
     * returns true if it an instuction is generated, otherwise
     * returns false. */
    bool try_vector_predication_comparison(const std::string &name, const Type &result_type,
                                           MaskVariant mask, llvm::Value *a, llvm::Value *b,
                                           const char *cmp_op);

    struct VPResultType {
        llvm::Type *type;
        std::optional<size_t> mangle_index;
        VPResultType(llvm::Type *type, std::optional<size_t> mangle_index = std::nullopt)
            : type(type), mangle_index(mangle_index) {
        }
    };

    /** Generate an intrisic call if use_llvm_vp_intrinsics is true
     * and length is greater than 1. If generated, assigns result
     * of vp intrinsic to value and returns true if it an instuction
     * is generated, otherwise returns false. */
    bool try_vector_predication_intrinsic(const std::string &name, VPResultType result_type,
                                          int32_t length, MaskVariant mask, std::vector<VPArg> args);

    /** Controls use of vector predicated intrinsics for vector operations.
     * Will be set by certain backends (e.g. RISC V) to control codegen. */
    bool use_llvm_vp_intrinsics;
    // @}

    /** Generate a basic dense vector load, with an optional predicate and
     * control over whether or not we should slice the load into native
     * vectors. Used by CodeGen_ARM to help with vld2/3/4 emission. */
    llvm::Value *codegen_dense_vector_load(const Load *load, llvm::Value *vpred = nullptr, bool slice_to_native = true);

    /** Warning messages which we want to avoid displaying number of times */
    enum class WarningKind {
        EmulatedFloat16,
    };
    std::map<WarningKind, std::string> onetime_warnings;

private:
    /** All the values in scope at the current code location during
     * codegen. Use sym_push and sym_pop to access. */
    Scope<llvm::Value *> symbol_table;

    /** String constants already emitted to the module. Tracked to
     * prevent emitting the same string many times. */
    std::map<std::string, llvm::Constant *> string_constants;

    /** A basic block to branch to on error that triggers all
     * destructors. As destructors are registered, code gets added
     * to this block. */
    llvm::BasicBlock *destructor_block;

    /** Turn off all unsafe math flags in scopes while this is set. */
    bool strict_float;

    /** Use the LLVM large code model when this is set. */
    bool llvm_large_code_model;

    /** Cache the result of target_vscale from architecture specific implementation
     * as this is used on every Halide to LLVM type conversion.
     */
    int effective_vscale;

    /** Embed an instance of halide_filter_metadata_t in the code, using
     * the given name (by convention, this should be ${FUNCTIONNAME}_metadata)
     * as extern "C" linkage. Note that the return value is a function-returning-
     * pointer-to-constant-data.
     */
    llvm::Function *embed_metadata_getter(const std::string &metadata_getter_name,
                                          const std::string &function_name, const std::vector<LoweredArgument> &args,
                                          const MetadataNameMap &metadata_name_map);

    /** Embed a constant expression as a global variable. */
    llvm::Constant *embed_constant_expr(Expr e, llvm::Type *t);
    llvm::Constant *embed_constant_scalar_value_t(const Expr &e);

    llvm::Function *add_argv_wrapper(llvm::Function *fn, const std::string &name,
                                     bool result_in_argv, std::vector<bool> &arg_is_buffer);

    llvm::Value *codegen_vector_load(const Type &type, const std::string &name, const Expr &base,
                                     const Buffer<> &image, const Parameter &param, const ModulusRemainder &alignment,
                                     llvm::Value *vpred = nullptr, bool slice_to_native = true, llvm::Value *stride = nullptr);

    virtual void codegen_predicated_load(const Load *op);
    virtual void codegen_predicated_store(const Store *op);

    void codegen_atomic_rmw(const Store *op);

    void init_codegen(const std::string &name, bool any_strict_float = false);
    std::unique_ptr<llvm::Module> finish_codegen();

    /** A helper routine for generating folded vector reductions. */
    template<typename Op>
    bool try_to_fold_vector_reduce(const Expr &a, Expr b);

    /** Records the StructType for pointer values returned from
     * make_struct intrinsic. Required for opaque pointer support.
     * This map should never grow without bound as each entry
     * represents a unique struct type created by a closure or similar.
     */
    std::map<llvm::Value *, llvm::Type *> struct_type_recovery;
};

}  // namespace Internal

/** Given a Halide module, generate an llvm::Module. */
std::unique_ptr<llvm::Module> codegen_llvm(const Module &module,
                                           llvm::LLVMContext &context);

}  // namespace Halide

#endif
