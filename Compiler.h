#ifndef COMPILER_H
#define COMPILER_H

#include "IRNode.h"
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
    virtual void compile(FImage *im);
    virtual void run() = 0;

    // TODO: Compile a reduction

    // TODO: Compile a scan

    // TODO: unify the above into a single compile which detects the
    // appropriate way to compile the evaluation of an FImage given
    // its definitions
    
protected:
    NodeData<Variable>* varData(size_t i) { return vars[i]->data<Variable>(); }
    
    virtual void compilePrologue() = 0;

    // Compile a single definition
    // Gather all descendents of a node with a particular op
    virtual void collectInputs(IRNode::Ptr node, OpCode op, IRNode::PtrSet &nodes);
    
    virtual void compileDefinition(FImage *im, int definition);
    virtual void preCompileDefinition(FImage *im, int definition);
    virtual void compileLoopHeader(size_t level) = 0;
    virtual void compileLoopTail(size_t level) = 0;
    virtual void compileBody(vector<IRNode::Ptr> code) = 0;
    virtual void compileEpilogue() = 0;

    // Gather all descendents of a node in a depth-first post-order
    // manner. Used to start off the instruction scheduler.
    virtual void gatherDescendents(IRNode::Ptr node, 
                                   vector<vector<IRNode::Ptr> > &output,
                                   int depth);
    
    // Find and order all the IRNodes that go into computing the given
    // vector of root nodes
    virtual void scheduleInstructions();

    vector<IRNode::Ptr> roots;
    vector<IRNode::Ptr> vars;
    
    vector<vector<IRNode::Ptr > > order;

    vector<int> vectorWidth;
    vector<int> unroll;
};

#endif
