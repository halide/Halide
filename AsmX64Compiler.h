#ifndef ASMX64COMPILER_H
#define ASMX64COMPILER_H

#include "Compiler.h"
#include "X64.h"

class AsmX64Compiler : public Compiler {
public:
    
    virtual void run() { a.run(); }

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
    AsmX64 a;
    vector<AsmX64::Reg> varRegs;
    char labels[10][20];
};

#endif
