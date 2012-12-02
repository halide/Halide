#ifndef CODEGEN_H
#define CODEGEN_H

#include <llvm/Value.h>
#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/IRBuilder.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/PassManager.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/IPO.h>

#include <map>
#include <stack>
#include <string>
#include <vector>

#include "IRVisitor.h"
#include "Argument.h"
#include "IR.h"
#include "Scope.h"

namespace HalideInternal {

    using std::map;
    using std::string;
    using std::stack;
    using std::vector;

    /* A code generator abstract base class. Actual code generators
       (e.g. CodeGen_X86) inherit from this. This class is responsible
       for taking a Halide Stmt and producing llvm bitcode, machine
       code in an object file, or machine code accessible through a
       function pointer. */
    class CodeGen : public IRVisitor {
      public:
        CodeGen();

        void compile_to_bitcode(const string &filename);
        void compile_to_native(const string &filename, bool assembly = false);

        void *compile_to_function_pointer();

      protected:

        class Closure;

        void compile(Stmt stmt, string name, const vector<Argument> &args);

        // Codegen state for llvm:
        // Current module, function, builder, context, and value
        static bool llvm_initialized;
        llvm::Module *module;
        llvm::Function *function;
        static llvm::LLVMContext context;
        llvm::IRBuilder<> builder;
        llvm::Value *value;
        
        // All the values in scope
        Scope<llvm::Value *> symbol_table;
        void sym_push(const string &name, llvm::Value *value);

        // Some useful types
        llvm::Type *void_t, *i1, *i8, *i16, *i32, *i64, *f16, *f32, *f64;
        llvm::StructType *buffer_t;

        // JIT state
        string function_name;
        static llvm::ExecutionEngine *execution_engine;

        // Call these to recursively visit sub-expressions and sub-statements
        llvm::Value *codegen(Expr);
        void codegen(Stmt);

        // Take an llvm Value representing a pointer to a buffer_t,
        // and populate the symbol table with its constituent parts
        void unpack_buffer(string name, llvm::Value *buffer);

        // Add a definition of buffer_t to the module if it isn't already there
        void define_buffer_t();
       
        // Given an llvm value representing a pointer to a buffer_t, extract various subfields
        llvm::Value *buffer_host(llvm::Value *);
        llvm::Value *buffer_dev(llvm::Value *);
        llvm::Value *buffer_host_dirty(llvm::Value *);
        llvm::Value *buffer_dev_dirty(llvm::Value *);
        llvm::Value *buffer_min(llvm::Value *, int);
        llvm::Value *buffer_extent(llvm::Value *, int);
        llvm::Value *buffer_stride(llvm::Value *, int);
        llvm::Value *buffer_elem_size(llvm::Value *);

        llvm::Value *codegen_buffer_pointer(string buffer, Type t, llvm::Value *index);
        llvm::Type *llvm_type_of(Type t);

        virtual void visit(const IntImm *);
        virtual void visit(const FloatImm *);
        virtual void visit(const Cast *);
        virtual void visit(const Var *);
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
        virtual void visit(const Provide *);
        virtual void visit(const Allocate *) = 0; // Allocate is architecture-specific
        virtual void visit(const Realize *);
        virtual void visit(const Block *);        
    };

}

#endif
