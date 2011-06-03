#ifndef IR_NODE_H
#define IR_NODE_H

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <map>
#include <vector>
using namespace std;

#ifndef _MSC_VER
#include <tr1/unordered_set>
#include <tr1/memory>
using namespace std::tr1;
#else
#include <memory>
#include <unordered_set>
#endif

#include "Interval.h"


enum OpCode {Const = 0, NoOp, 
             Variable, Plus, Minus, Times, Divide, Power,
             Sin, Cos, Tan, ASin, ACos, ATan, ATan2, 
             Abs, Floor, Ceil, Round,
             Exp, Log, Mod, 
             LT, GT, LTE, GTE, EQ, NEQ,
             And, Or, Nand,
             Load, Store,
             IntToFloat, FloatToInt, 
             PlusImm, TimesImm, 
             Vector, LoadVector, StoreVector, ExtractVector, ExtractScalar};

enum Order {Increasing, Decreasing, Parallel};

const char *opname(OpCode op);

class AbstractNodeData {
public:
    virtual ~AbstractNodeData() {};
};

template<OpCode op>
class NodeData : public AbstractNodeData {
public:
    ~NodeData() {};
};

template<>
class NodeData<Variable> : public AbstractNodeData {
public:
    ~NodeData() {};
    int vectorize, unroll, parallelize;
    Order order;   
    bool fuseLoops;
    int loopNesting;
};

// Here's how you hash a shared_ptr
namespace std {
    namespace tr1 {
        template<class T>
        class hash<shared_ptr<T> > {
        public:
            size_t operator()(const shared_ptr<T>& key) const {
                return (size_t)key.get();
            }
        };
    }
}

// jrk 2011-02-23: why only tag on OpCode? Why not subclass IRNode? (e.g. var ref, load, store, assign, binop, unaryop, ...)
// One node in the intermediate representation
class IRNode {
public:

    typedef shared_ptr<IRNode> Ptr;
    typedef weak_ptr<IRNode> WeakPtr;
    typedef unordered_set<Ptr> PtrSet;
    #define NULL_IRNODE_PTR (IRNode::Ptr((IRNode*)NULL))

    enum Type {Unknown = 0, Float, Bool, Int};   

    // Opcode
    OpCode op;
        
    // Any immediate data. Mostly useful for the Const op.
    float fval;
    int64_t ival;

    // Vector width. Should be one or four on X64.
    int width;

    // Static info for integer nodes. Necessary for vars. Useful for
    // reasoning about overflow, alignment, and signedness,
    // etc. Includes min, max, modulus, and remainder.
    SteppedInterval interval;

    // Inputs - whose values do I depend on?
    vector<Ptr> inputs;    
        
    // Who uses my value?
    vector<WeakPtr> outputs;

            
    // Does this op depend on any vars or memory?
    bool constant;

    // What register will this node be computed in? -1 indicates no
    // register has been allocated. 0-15 indicates a GPR, 16-31
    // indicates an SSE register. It will be -1 until register
    // allocation takes place.
    signed char reg;
               
    // What level of the for loop will this node be computed at?
    // Right now 0 is outermost, representing consts, and 4 is
    // deepest, representing iteration over channels usually.
    signed char level;

    // What is the type of this expression?
    Type type;

    // A tag used by recursive algorithms that need to add marks to different nodes
    int tag;

    // Destructor. Don't call delete - use Ptrs and WeakPtrs instead.
    ~IRNode();

    // Make a float constant
    static Ptr make(float v);

    // Make an int constant 
    static Ptr make(int64_t v);
    
    // Make an IRNode with the given opcode and the given inputs and constant values
    static Ptr make(OpCode opcode, 
                    Ptr input1 = NULL_IRNODE_PTR, 
                    Ptr input2 = NULL_IRNODE_PTR, 
                    Ptr input3 = NULL_IRNODE_PTR,
                    Ptr input4 = NULL_IRNODE_PTR,
                    int64_t ival = 0,
                    float fval = 0.0f);


    // Alternative versions for common cases
    static Ptr make(OpCode opcode, 
                    Ptr input1, 
                    int64_t ival) {
        return make(opcode, input1, NULL_IRNODE_PTR, NULL_IRNODE_PTR, NULL_IRNODE_PTR, ival, 0.0f);
    }

    static Ptr make(OpCode opcode, 
                    Ptr input1, 
                    Ptr input2,
                    int64_t ival) {
        return make(opcode, input1, input2, NULL_IRNODE_PTR, NULL_IRNODE_PTR, ival, 0.0f);
    }


    // debugging output
    void printExp();
    void print();

    // Return an optimized version of this node. Most optimizations
    // are done by make, but there may be some that can only
    // effectively run after the entire DAG is generated. They go
    // here.
    Ptr optimize();

    // Return a new version of this node with one IRNode replaced with
    // another. Rebuilds and reoptimizes the graph.
    Ptr substitute(Ptr oldNode, Ptr newNode);

    // Assign a loop level to a var. The outputs will be recursively updated too.
    void assignLevel(unsigned char);

    // Cast an IRNode to a different type
    Ptr as(Type t);

    // Save out a .dot file showing all nodes in existence and how they connect
    static void saveDot(const char *filename);

    // Make another copy of the sole shared pointer to this object
    Ptr ptr() {return self.lock();}

    // All nodes in existence
    static vector<WeakPtr> allNodes;

    // Do (or redo any static analysis)
    void analyze();

    // If you know statically that a node n is of type foo, then you
    // can ask for n.data<foo>() to get at its supplemental data.
    template<OpCode o>
    NodeData<o> *data() {
        if (_data == NULL) _data = new NodeData<o>(); 
        return (NodeData<o> *)_data;
    }

protected:
    // All the const float nodes
    static map<float, WeakPtr> floatInstances;

    // All the int nodes
    static map<int64_t, WeakPtr> intInstances;

    // The correct way for IRNode methods to create new nodes.
    static Ptr makeNew(float);
    static Ptr makeNew(int64_t);
    static Ptr makeNew(Type, int, OpCode, const vector<Ptr> &input, int64_t, float);

    // The actual constructor. Only used by the makeNew methods, which
    // are only used by the make methods.
    IRNode(Type t, int w, OpCode opcode, 
           const vector<Ptr> &input,
           int64_t iv, float fv);
    
    // A slightly more generate make function that takes a vector of children for inputs
    static Ptr make(OpCode opcode,
                    vector<Ptr> inputs,
                    int64_t ival = 0, float fval = 0.0f);
    

    // A weak reference to myself. 
    WeakPtr self;

    // Reorder summations from low to high level. (x+(y+1)) is cheaper
    // to compute than ((x+y)+1) because more can be moved into an
    // outer loop. This is done by the optimize call and also by make.
    Ptr rebalanceSum();
    void collectSum(vector<pair<Ptr, bool> > &terms, bool positive = true);


    // Extra data for this IRNode. Cast it to NodeData<op>
    AbstractNodeData *_data;
    
};



#endif







