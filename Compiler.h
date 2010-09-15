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
    void compileBody(AsmX64 *a, vector<IRNode::Ptr> code);
    
    // Assign registers and generates an evaluation order for a vector
    // of expressions.
    void doRegisterAssignment(
        const vector<IRNode::Ptr> &roots, 
        uint32_t reserved,
        vector<vector<IRNode::Ptr> > &order,
        vector<uint32_t> &clobberedRegs, 
        vector<uint32_t> &outputRegs);

    // Remove all assigned registers
    void regClear(IRNode::Ptr node);

    // Assign a register to a node
    void regAssign(IRNode::Ptr node,
                   uint32_t reserved,
                   vector<IRNode::Ptr> &regs, 
                   vector<vector<IRNode::Ptr> > &order);

        
};

#endif
