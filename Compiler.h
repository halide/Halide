#ifndef COMPILER_H
#define COMPILER_H

#include "IRNode.h"
#include "X64.h"
#include "FImage.h"

#include <hash_set>

class Compiler {
public:
    
    // Compile a gather statement. e.g:
    // out(x, y, c) = 37*im(x+y, y*4, c);
    // 
    // Right now it assumes the following in order to vectorize across x for all loads. 
    //
    // 1) The bounds of X are a multiple of 4
    //
    void compile(AsmX64 *a, FImage *im);

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

    // Gather all descendents of a node with a particular op
    void collectInputs(IRNode::Ptr node, OpCode op, IRNode::PtrSet &nodes);

    // Remove all assigned registers
    void regClear(IRNode::Ptr node);

    // Gather all descendents of a node in a depth-first post-order
    // manner. Used to start off the instruction scheduler.
    void gatherDescendents(IRNode::Ptr node, 
                           vector<vector<IRNode::Ptr> > &output, int depth);

    // Find and order all the IRNodes that go into computing the given
    // vector of root nodes
    void doInstructionScheduling(
        const vector<IRNode::Ptr > &roots, 
        vector<vector<IRNode::Ptr > > &order);

    // Assign a register to a node
    void regAssign(IRNode::Ptr node,
                   uint32_t reserved,
                   vector<IRNode::Ptr> &regs, 
                   vector<vector<IRNode::Ptr> > &order);

        
};

#endif
