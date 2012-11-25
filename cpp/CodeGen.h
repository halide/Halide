#ifndef CODEGEN_H
#define CODEGEN_H

#include <llvm/Value.h>
#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/Support/IRBuilder.h>

#include <map>
#include <stack>

#include "IRVisitor.h"
#include "IR.h"


namespace HalideInteral {
  
    using namespace LLVM;

    class SymbolTable {
    private:
        map<string, stack<Value *> > table;
    public:
        Value *get(string name);
        void push(string name, Value *val);
        void pop(string name);        
    };

    class CodeGen : public IRVisitor {
    protected:
        // Codegen state for llvm
        // Current module, function, builder, context, and value
        Module *module;
        Function *function;
        LLVMContext context;
        BasicBlock *block;
        IRBuilder<> builder;
        typedef map<string, stack<Value *> > 
        SymbolTable symbol_table;
        Value *value;

    public:
        CodeGen(Stmt stmt, string name, string target_triple);

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
        virtual void visit(const Allocate *);
        virtual void visit(const Realize *);
        virtual void visit(const Block *);        

        static void test();
    };

}

#endif
