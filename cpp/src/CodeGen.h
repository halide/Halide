#ifndef HALIDE_CODEGEN_H
#define HALIDE_CODEGEN_H

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
}

#include <map>
#include <stack>
#include <string>
#include <vector>

#include "IRVisitor.h"
#include "Argument.h"
#include "IR.h"
#include "Scope.h"
#include "JITCompiledModule.h"
#include "ModulusRemainder.h"

namespace Halide { 
namespace Internal {

/** A code generator abstract base class. Actual code generators
 * (e.g. CodeGen_X86) inherit from this. This class is responsible
 * for taking a Halide Stmt and producing llvm bitcode, machine
 * code in an object file, or machine code accessible through a
 * function pointer. 
 */
class CodeGen : public IRVisitor {
public:
    mutable RefCount ref_count;

    CodeGen();
    virtual ~CodeGen();

    /** Take a halide statement and compiles it to an llvm module held
     * internally. Call this before calling compile_to_bitcode or
     * compile_to_native. */
    virtual void compile(Stmt stmt, std::string name, const std::vector<Argument> &args);
    
    /** Emit a compiled halide statement as llvm bitcode. Call this
     * after calling compile. */
    void compile_to_bitcode(const std::string &filename);

    /** Emit a compiled halide statement as either an object file, or
     * as raw assembly, depending on the value of the second
     * argument. Call this after calling compile. */
    void compile_to_native(const std::string &filename, bool assembly = false);

    /** Compile to machine code stored in memory, and return some
     * functions pointers into that machine code. */
    JITCompiledModule compile_to_function_pointers();

    /** What should be passed as -mcpu and -mattrs for
     * compilation. The architecture-specific code generator should
     * define these. */
    // @{
    virtual std::string mcpu() const = 0;
    virtual std::string mattrs() const = 0;
    // @}

protected:

    class Closure;

    /** State needed by llvm for code generation, including the
     * current module, function, context, builder, and most recently
     * generated llvm value. */
    //@{
    static bool llvm_initialized;
    static bool llvm_X86_enabled;
    static bool llvm_ARM_enabled;
    static bool llvm_NVPTX_enabled;

    llvm::Module *module;
    bool owns_module;
    llvm::Function *function;
    llvm::LLVMContext &context;
    llvm::IRBuilder<true, llvm::ConstantFolder, llvm::IRBuilderDefaultInserter<true> > *builder;
    llvm::Value *value;
    //@}

    /** Run all of llvm's optimization passes on the module */
    void optimize_module();

    /** Add an entry to the symbol table, hiding previous entries with
     * the same name. Call this when new values come into scope. */
    void sym_push(const std::string &name, llvm::Value *value);

    /** Remove an entry for the symbol table, revealing any previous
     * entries with the same name. Call this when values go out of
     * scope. */
    void sym_pop(const std::string &name);

    /** Some useful llvm types */
    // @{
    llvm::Type *void_t, *i1, *i8, *i16, *i32, *i64, *f16, *f32, *f64;
    llvm::StructType *buffer_t;
    // @}

    /** The name of the function being generated */
    std::string function_name;

    /** Emit code that evaluates an expression, and return the llvm
     * representation of the result of the expression. */
    llvm::Value *codegen(Expr);
    
    /** Emit code that runs a statement */
    void codegen(Stmt);

    /** Take an llvm Value representing a pointer to a buffer_t,
     * and populate the symbol table with its constituent parts
     */
    void unpack_buffer(std::string name, llvm::Value *buffer);

    /** Add a definition of buffer_t to the module if it isn't already there */
    void define_buffer_t();

    /** Codegen an assertion. If false, it bails out and calls the error handler. */
    void create_assertion(llvm::Value *condition, const std::string &message);
       
    /** Given an llvm value representing a pointer to a buffer_t, extract various subfields */
    // @{
    llvm::Value *buffer_host(llvm::Value *);
    llvm::Value *buffer_dev(llvm::Value *);
    llvm::Value *buffer_host_dirty(llvm::Value *);
    llvm::Value *buffer_dev_dirty(llvm::Value *);
    llvm::Value *buffer_min(llvm::Value *, int);
    llvm::Value *buffer_extent(llvm::Value *, int);
    llvm::Value *buffer_stride(llvm::Value *, int);
    llvm::Value *buffer_elem_size(llvm::Value *);
    // @}

    /** Generate a pointer into a named buffer at a given index, of a
     * given type. The index counts according to the scalar type of
     * the type passed in. */
    llvm::Value *codegen_buffer_pointer(std::string buffer, Type type, llvm::Value *index);

    /** Return the llvm version of a halide type */
    llvm::Type *llvm_type_of(Type type);

    using IRVisitor::visit;

    /** Generate code for various IR nodes. These can be overridden by
     * architecture-specific code to perform peephole
     * optimizations. The result of each is stored in \ref value */
    // @{
    virtual void visit(const IntImm *);
    virtual void visit(const FloatImm *);
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
    virtual void visit(const PrintStmt *);
    virtual void visit(const AssertStmt *);
    virtual void visit(const Pipeline *);
    virtual void visit(const For *);
    virtual void visit(const Store *);
    virtual void visit(const Block *);        
    // @}

    /** Generate code for an allocate node. It has no default
     * implementation - it must be handled in an architecture-specific
     * way. */
    virtual void visit(const Allocate *) = 0; 

    /** These IR nodes should have been removed during
     * lowering. CodeGen will error out if they are present */
    // @{
    virtual void visit(const Provide *);
    virtual void visit(const Realize *);
    // @}


    /** If we have to bail out of a pipeline midway, this should
     * inject the appropriate cleanup code. */
    virtual void prepare_for_early_exit() {}

private:
    /** All the values in scope at the current code location during
     * codegen. Use sym_push and sym_pop to access. */
    Scope<llvm::Value *> symbol_table;

    /** Alignment info for Int(32) variables in scope */
    Scope<ModulusRemainder> alignment_info;
        

};

}}

#endif
