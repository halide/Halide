#ifndef COMPILER_H
#define COMPILER_H

#include "IRNode.h"
#include "X64.h"
#include "FImage.h"

class Compiler {
public:
    
    // Compile a gather statement. e.g:
    // out(x, y, c) = 37*im(x+y, y*4, c);
    // 
    // Right now it assumes the following in order to vectorize across x for all loads. 
    //
    // 1) The bounds of X are a multiple of 4
    //
    // 2) The derivative of all load addresses w.r.t x is 1 pixel
    // (i.e. the number of channels * sizeof(float)). This means the following does not work:
    // out(x, y, c) = im(2*x, y, c);
    //
    // TODO: Remove both of these limitations and allow for more flexible vectorization
    void compileGather(AsmX64 *a, FImage *im);

    // TODO: Compile a reduction

    // TODO: Compile a scan

    // TODO: unify the above into a single compile which detects the
    // appropriate way to compile the evaluation of an FImage given
    // its definitions

protected:
    // Generate machine code for a vector of IRNodes. Registers must
    // have already been assigned.
    void compileBody(AsmX64 *a, vector<IRNode *> code);
    
    // Assign registers and generates an evaluation order for a vector
    // of expressions.
    void doRegisterAssignment(
        const vector<IRNode *> &roots, 
        const map<OpCode, int> &vars,
        uint32_t reserved,
        vector<IRNode *> order[5],
        uint32_t clobberedRegs[5], 
        uint32_t outputRegs[5]);

    // Remove all assigned registers
    void regClear(IRNode *node);

    // Assign a register to a node
    void regAssign(IRNode *node,
                   const map<OpCode, int> &vars, 
                   uint32_t reserved,
                   vector<IRNode *> &regs, 
                   vector<IRNode *> *order);

        
};

#endif
