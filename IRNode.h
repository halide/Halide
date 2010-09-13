#ifndef IR_NODE_H
#define IR_NODE_H

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <map>
#include <vector>

using namespace std;

void panic(const char *fmt, ...);
void assert(bool condition, const char *fmt, ...);

static const char *opname[] = {"Const", "NoOp",
                               "VarX", "VarY", "VarT", "VarC", "UnboundVar",
                               "Plus", "Minus", "Times", "Divide", "Power",
                               "Sin", "Cos", "Tan", "ASin", "ACos", "ATan", "ATan2", 
                               "Abs", "Floor", "Ceil", "Round",
                               "Exp", "Log", "Mod", 
                               "LT", "GT", "LTE", "GTE", "EQ", "NEQ",
                               "And", "Or", "Nand", "Load",
                               "IntToFloat", "FloatToInt", 
                               "LoadImm", "PlusImm", "TimesImm"};


enum OpCode {Const = 0, NoOp, 
             VarX, VarY, VarT, VarC, UnboundVar,
             Plus, Minus, Times, Divide, Power,
             Sin, Cos, Tan, ASin, ACos, ATan, ATan2, 
             Abs, Floor, Ceil, Round,
             Exp, Log, Mod, 
             LT, GT, LTE, GTE, EQ, NEQ,
             And, Or, Nand,
             Load,
             IntToFloat, FloatToInt, 
             LoadImm, PlusImm, TimesImm};

// One node in the intermediate representation
class IRNode {
public:

    enum Type {Unknown = 0, Float, Bool, Int};   

    enum {DepT = 1, DepY = 2, DepX = 4, DepC = 8, DepMem = 16, DepUnbound = 32};

    // Opcode
    OpCode op;
        
    // Any immediate data. Mostly useful for the Const op.
    float fval;
    int ival;

    // Vector width. Should be one or four on X64.
    //
    // TODO: this is currently always set to 1 even though we're
    // forcibly vectorizing across X
    int width;

    // Inputs - whose values do I depend on?
    vector<IRNode *> inputs;    
        
    // Who uses my value?
    vector<IRNode *> outputs;
        
    // Which loop variables does this node depend on?
    uint32_t deps;

    // What register will this node be computed in? -1 indicates no
    // register has been allocated. 0-15 indicates a GPR, 16-31
    // indicates an SSE register. It will be -1 until register
    // allocation takes place.
    signed char reg;
               
    // What level of the for loop will this node be computed at?
    // Right now 0 is outermost, representing consts, and 4 is
    // deepest, representing iteration over channels.
    signed char level;

    // What is the type of this expression?
    Type type;

    // Make a float constant
    static IRNode *IRNode::make(float v);

    // Make an int constant 
    static IRNode *IRNode::make(int v);

    // Make an IRNode with the given opcode and the given inputs and constant values
    static IRNode *IRNode::make(OpCode opcode, 
                                IRNode *input1 = NULL, 
                                IRNode *input2 = NULL, 
                                IRNode *input3 = NULL,
                                IRNode *input4 = NULL,
                                int ival = 0,
                                float fval = 0.0f);


    // Return an optimized version of this node. Most optimizations
    // are done by make, but there may be some that can only
    // effectively run after the entire DAG is generated. They go
    // here.
    IRNode *optimize();

    // Return a new version of this node with a variable replaced by a constant int value
    IRNode *substitute(OpCode var, int val);

    // Return a new version of this node with the given unbound
    // variables replaced with VarX, VarY, VarT, and VarC.
    IRNode *bind(IRNode *x, IRNode *y, IRNode *t, IRNode *c);

    // Cast an IRNode to a different type
    IRNode *IRNode::as(Type t);

    // Recursively print out the complete expression this IRNode
    // computes (e.g. x+y*17). This can get long.
    void IRNode::printExp();

    // Print out which operation occurs on what registers (e.g. xmm0 =
    // xmm1 + xmm2). Must be called after registers are assigned.
    void IRNode::print();

protected:
    // All the const float nodes
    static map<float, IRNode *> floatInstances;

    // All the int nodes
    static map<int, IRNode *> intInstances;

    // All the Var nodes
    static map<OpCode, IRNode *> varInstances;

    // All nodes, including those above
    static vector<IRNode *> allNodes;

    // Delete all IRNodes except for those in this list and all nodes
    // necessary for the computation of those in this list.  Currently
    // IRNodes are garbage collected (if they are ever deleted at
    // all), using a mark and sweep algorithm.
    void collectGarbage(vector<IRNode *> saved);
    
    // Delete all IRNodes.
    void IRNode::clearAll();

    // Is this node marked for death by the garbage collector?
    bool marked;
    
    // Mark a node and its inputs, (and it's inputs' inputs...)
    void markDescendents(bool newMark);

    // You can only make IRNodes using the static make functions. The actual constructors are private.
    IRNode::IRNode(float v);
    IRNode::IRNode(int v);
    IRNode::IRNode(Type t, OpCode opcode, 
                   vector<IRNode *> input,
                   int iv, float fv);
    
    // A slightly more generate make function that takes a vector of children for inputs
    static IRNode *IRNode::make(OpCode opcode,
                                vector<IRNode *> inputs,
                                int ival = 0, float fval = 0.0f);


    // Reorder summations from low to high level. (x+(y+1)) is cheaper
    // to compute than ((x+y)+1) because more can be moved into an
    // outer loop. This is done by the optimize call and also by make.
    IRNode *rebalanceSum();
    void collectSum(vector<pair<IRNode *, bool> > &terms, bool positive = true);

};


#endif







