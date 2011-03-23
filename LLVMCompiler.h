#ifndef LLVMCOMPILER_H
#define LLVMCOMPILER_H

#include "Compiler.h"
#include <llvm/DerivedTypes.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/PassManager.h>
#include <llvm/Support/IRBuilder.h>

class LLVMCompiler : public Compiler {
public:
    LLVMCompiler();
    
    virtual void run();

protected:
    virtual void compilePrologue();
    // Compile a single definition
    virtual void preCompileDefinition(FImage *im, int definition);
    virtual void compileEpilogue();

    virtual void compileLoopHeader(size_t level);
    virtual void compileLoopTail(size_t level);

    // Generate machine code for a vector of IRNodes. Registers must
    // have already been assigned.
    virtual void compileBody(vector<IRNode::Ptr> code);
    
    // Assign registers and generates an evaluation order for a vector
    // of expressions.
    void assignRegisters();

    // Remove all assigned registers
    void regClear(IRNode::Ptr node);

    // Assign a register to a node
    void regAssign(IRNode::Ptr node,
                   uint32_t reserved,
                   vector<IRNode::Ptr> &regs, 
                   vector<vector<IRNode::Ptr> > &order);

private:
    llvm::ExecutionEngine *ee;
    llvm::IRBuilder<> *builder;
    llvm::Module *module;
    llvm::Function *mainFunc;
    llvm::LLVMContext& ctx;

    llvm::FunctionPassManager *passMgr;
    //AsmX64 a;
    //vector<AsmX64::Reg> varRegs;
    //char labels[10][20];
};

#endif
