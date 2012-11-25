#include "CodeGen.h"

namespace HalideInternal {

    Value *SymbolTable::get(string name) {
        map<string, stack<Value *> >::iterator iter = table.find(name);
        assert(iter != table.end() && "Symbol not found");
        return iter->second;
    }

    void SymbolTable::push(string name, Value *value) {
        table[name].push(value);
    }

    void SymbolTable::pop(string name) {
        assert(!table[name].empty() && "Name not in symbol table");
        table[name].pop();
    }

    CodeGen::CodeGen(Stmt stmt, string name, string target_triple) : builder(context) {
        module = new Module(name.c_str(), context);

        Type void_t = Type::getVoidTy(context);
        FunctionType *func_t = FunctionType::get(void_t, void_t, false);
        function = Function::Create(func_t, Function::ExternalLinkage, name, module);

        block = BasicBlock::Create(context, "entry", function);
        builder.SetInsertPoint(block);

        // Ok, we have a module, function, context, and a builder
        // pointing at a brand new basic block. We're good to go.
        stmt.accept(this);

        // Now verify the function is ok
        verifyFunction(function);
    }

    Value *codegen(Expr e) {
        value = NULL;
        e.accept(this);
        assert(value && "Codegen of an expr did not produce an llvm value");
        return value;
    }

    virtual void visit(const IntImm *op) {
        IntegerType *i32_t = Type::getInt32Ty(context);
        value = ConstantInt::getSigned(i32_t, op->value);
    }

    virtual void visit(const FloatImm *op) {
        value = ConstantFP::get(context, APFloat(op->value));
    }

    virtual void visit(const Cast *op) {
        // do nothing for now
        value = codegen(op->value);
    }

    virtual void visit(const Var *op) {
        // look in the symbol table
        value = symbol_table.get(op->name);
    }

    virtual void visit(const Add *op) {
        
    }

    virtual void visit(const Sub *op) {
    }

    virtual void visit(const Mul *op) {
    }

    virtual void visit(const Div *op) {
    }

    virtual void visit(const Mod *op) {
    }

    virtual void visit(const Min *op) {
    }

    virtual void visit(const Max *op) {
    }

    virtual void visit(const EQ *op) {
    }

    virtual void visit(const NE *op) {
    }

    virtual void visit(const LT *op) {
    }

    virtual void visit(const LE *op) {
    }

    virtual void visit(const GT *op) {
    }

    virtual void visit(const GE *op) {
    }

    virtual void visit(const And *op) {
    }

    virtual void visit(const Or *op) {
    }

    virtual void visit(const Not *op) {
    }

    virtual void visit(const Select *op) {
    }

    virtual void visit(const Load *op) {
    }

    virtual void visit(const Ramp *op) {
    }

    virtual void visit(const Call *op) {
    }

    virtual void visit(const Let *op) {
    }

    virtual void visit(const LetStmt *op) {
    }

    virtual void visit(const PrintStmt *op) {
    }

    virtual void visit(const AssertStmt *op) {
    }

    virtual void visit(const Pipeline *op) {
    }

    virtual void visit(const For *op) {
    }

    virtual void visit(const Store *op) {
    }

    virtual void visit(const Allocate *op) {
    }

    virtual void visit(const Block *op) {
    }        

    virtual void visit(const Realize *op) {
        assert(false && "Realize encountered during codegen");
    }

    virtual void visit(const Provide *op) {
        assert(false && "Provide encountered during codegen");
    }

    void CodeGen::test() {
        
    }
}
