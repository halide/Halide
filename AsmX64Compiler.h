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
    virtual void compileDefinition(FImage *im, int definition);
    virtual void compileEpilogue();

    // Generate machine code for a vector of IRNodes. Registers must
    // have already been assigned.
    virtual void compileBody(vector<IRNode::Ptr> code);
    
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

private:
    AsmX64 a;
};

#endif
