#ifndef CODEGEN_H
#define CODEGEN_H

#include <llvm/Value.h>
#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/IRBuilder.h>

#include <map>
#include <stack>
#include <string>
#include <vector>

#include "IRVisitor.h"
#include "Argument.h"
#include "IR.h"

namespace HalideInternal {

    using std::map;
    using std::string;
    using std::stack;
    using std::vector;

    class SymbolTable {
    private:
        map<string, stack<llvm::Value *> > table;
    public:
        llvm::Value *get(string name);
        void push(string name, llvm::Value *val);
        void pop(string name);        
    };

    class CodeGen : public IRVisitor {
    public:
        CodeGen();

        void compile_to_file(string name);
        void *compile_to_function_pointer();

    protected:

        void compile(Stmt stmt, string name, const vector<Argument> &args, string target_triple);

        // Codegen state for llvm
        // Current module, function, builder, context, and value
        llvm::Module *module;
        llvm::Function *function;
        llvm::LLVMContext context;
        llvm::BasicBlock *block;
        llvm::IRBuilder<> builder;
        SymbolTable symbol_table;
        llvm::Value *value;

        llvm::Value *codegen(Expr);
        void codegen(Stmt);

        llvm::Type *void_t, *i1, *i8, *i16, *i32, *i64, *f16, *f32, *f64;
        llvm::StructType *buffer_t;

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
