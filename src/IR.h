#ifndef HALIDE_IR_H
#define HALIDE_IR_H

/** \file
 * Halide expressions (\ref Halide::Expr) and statements (\ref Halide::Internal::Stmt)
 */

#include <string>
#include <vector>

#include "Buffer.h"
#include "IRVisitor.h"
#include "Type.h"
#include "IntrusivePtr.h"
#include "Util.h"

namespace Halide {

namespace Internal {

/** A class representing a type of IR node (e.g. Add, or Mul, or
 * PrintStmt). We use it for rtti (without having to compile with
 * rtti). */
struct IRNodeType {};

/** The abstract base classes for a node in the Halide IR. */
struct IRNode {

    /** We use the visitor pattern to traverse IR nodes throughout the
     * compiler, so we have a virtual accept method which accepts
     * visitors.
     */
    virtual void accept(IRVisitor *v) const = 0;
    IRNode() {}
    virtual ~IRNode() {}

    /** These classes are all managed with intrusive reference
       counting, so we also track a reference count. It's mutable
       so that we can do reference counting even through const
       references to IR nodes. */
    mutable RefCount ref_count;

    /** Each IR node subclass should return some unique pointer. We
     * can compare these pointers to do runtime type
     * identification. We don't compile with rtti because that
     * injects run-time type identification stuff everywhere (and
     * often breaks when linking external libraries compiled
     * without it), and we only want it for IR nodes. */
    virtual const IRNodeType *type_info() const = 0;
};

/** IR nodes are split into expressions and statements. These are
   similar to expressions and statements in C - expressions
   represent some value and have some type (e.g. x + 3), and
   statements are side-effecting pieces of code that do not
   represent a value (e.g. assert(x > 3)) */

/** A base class for statement nodes. They have no properties or
   methods beyond base IR nodes for now */
struct BaseStmtNode : public IRNode {
};

/** A base class for expression nodes. They all contain their types
 * (e.g. Int(32), Float(32)) */
struct BaseExprNode : public IRNode {
    Type type;
    BaseExprNode(Type t) : type(t) {}
};

/** We use the "curiously recurring template pattern" to avoid
   duplicated code in the IR Nodes. These classes live between the
   abstract base classes and the actual IR Nodes in the
   inheritance hierarchy. It provides an implementation of the
   accept function necessary for the visitor pattern to work, and
   a concrete instantiation of a unique IRNodeType per class. */
template<typename T>
struct ExprNode : public BaseExprNode {
    ExprNode(Type t) : BaseExprNode(t) {}
    void accept(IRVisitor *v) const {
        v->visit((const T *)this);
    }
    virtual IRNodeType *type_info() const {return &_type_info;}
    static EXPORT IRNodeType _type_info;
};

template<typename T>
struct StmtNode : public BaseStmtNode {
    void accept(IRVisitor *v) const {
        v->visit((const T *)this);
    }
    virtual IRNodeType *type_info() const {return &_type_info;}
    static EXPORT IRNodeType _type_info;
};
    
/** IR nodes are passed around opaque handles to them. This is a
   base class for those handles. It manages the reference count,
   and dispatches visitors. */
struct IRHandle : public IntrusivePtr<const IRNode> {
    IRHandle() : IntrusivePtr<const IRNode>() {}
    IRHandle(const IRNode *p) : IntrusivePtr<const IRNode>(p) {}

    /** Dispatch to the correct visitor method for this node. E.g. if
     * this node is actually an Add node, then this will call
     * IRVisitor::visit(const Add *) */
    void accept(IRVisitor *v) const {
        ptr->accept(v);
    }

    /** Downcast this ir node to its actual type (e.g. Add, or
     * Select). This returns NULL if the node is not of the requested
     * type. Example usage:
     * 
     * if (const Add *add = node->as<Add>()) {
     *   // This is an add node
     * }
     */
    template<typename T> const T *as() const {
        if (ptr->type_info() == &T::_type_info)
            return (const T *)ptr;
        return NULL;
    }
};

}

/** A fragment of Halide syntax. It's implemented as reference-counted
 * handle to a concrete expression node, but it's immutable, so you
 * can treat it as a value type. */
struct Expr : public Internal::IRHandle {
    /** Make an undefined expression */
    Expr() : Internal::IRHandle() {}        

    /** Make an expression from a concrete expression node pointer (e.g. Add) */
    Expr(const Internal::BaseExprNode *n) : IRHandle(n) {}

    /** Make an expression representing a const 32-bit int (i.e. an IntImm) */
    EXPORT Expr(int);

    /** Make an expression representing a const 32-bit float (i.e. a FloatImm) */
    EXPORT Expr(float);

    /** Get the type of this expression node */
    Type type() const {
        return ((Internal::BaseExprNode *)ptr)->type;
    }
};    

}

// Now that we've defined an Expr, we can include Parameter.h
#include "Parameter.h"

namespace Halide {
namespace Internal {

/** A reference-counted handle to a statement node. */
struct Stmt : public IRHandle {
    Stmt() : IRHandle() {}
    Stmt(const BaseStmtNode *n) : IRHandle(n) {}
};

/** The actual IR nodes begin here. Remember that all the Expr
 * nodes also have a public "type" property */

/** Integer constants */
struct IntImm : public ExprNode<IntImm> {
    int value;

    IntImm(int v) : ExprNode<IntImm>(Int(32)), value(v) {}
};

/** Floating point constants */
struct FloatImm : public ExprNode<FloatImm> {
    float value;

    FloatImm(float v) : ExprNode<FloatImm>(Float(32)), value(v) {}
};

/** Cast a node from one type to another */
struct Cast : public ExprNode<Cast> {
    Expr value;

    Cast(Type t, Expr v) : ExprNode<Cast>(t), value(v) {
        assert(v.defined() && "Cast of undefined");
    }
};

/** The sum of two expressions */
struct Add : public ExprNode<Add> {
    Expr a, b;

    Add(Expr _a, Expr _b) : ExprNode<Add>(_a.type()), a(_a), b(_b) {
        assert(a.defined() && "Add of undefined");
        assert(b.defined() && "Add of undefined");
        assert(b.type() == type && "Add of mismatched types");
    }
};

/** The difference of two expressions */
struct Sub : public ExprNode<Sub> {
    Expr a, b;

    Sub(Expr _a, Expr _b) : ExprNode<Sub>(_a.type()), a(_a), b(_b) {
        assert(a.defined() && "Sub of undefined");
        assert(b.defined() && "Sub of undefined");
        assert(b.type() == type && "Sub of mismatched types");
    }
};

/** The product of two expressions */
struct Mul : public ExprNode<Mul> {
    Expr a, b;

    Mul(Expr _a, Expr _b) : ExprNode<Mul>(_a.type()), a(_a), b(_b) {
        assert(a.defined() && "Mul of undefined");
        assert(b.defined() && "Mul of undefined");
        assert(b.type() == type && "Mul of mismatched types");
    }        
};

/** The ratio of two expressions */
struct Div : public ExprNode<Div> {
    Expr a, b;

    Div(Expr _a, Expr _b) : ExprNode<Div>(_a.type()), a(_a), b(_b) {
        assert(a.defined() && "Div of undefined");
        assert(b.defined() && "Div of undefined");
        assert(b.type() == type && "Div of mismatched types");
    }
};

/** The remainder of a / b. Mostly equivalent to '%' in C, except that
 * the result here is always positive. For floats, this is equivalent
 * to calling fmod. */
struct Mod : public ExprNode<Mod> { 
    Expr a, b;

    Mod(Expr _a, Expr _b) : ExprNode<Mod>(_a.type()), a(_a), b(_b) {
        assert(a.defined() && "Mod of undefined");
        assert(b.defined() && "Mod of undefined");
        assert(b.type() == type && "Mod of mismatched types");
    }
};

/** The lesser of two values. */
struct Min : public ExprNode<Min> {
    Expr a, b;

    Min(Expr _a, Expr _b) : ExprNode<Min>(_a.type()), a(_a), b(_b) {
        assert(a.defined() && "Min of undefined");
        assert(b.defined() && "Min of undefined");
        assert(b.type() == type && "Min of mismatched types");
    }
};

/** The greater of two values */
struct Max : public ExprNode<Max> {
    Expr a, b;

    Max(Expr _a, Expr _b) : ExprNode<Max>(_a.type()), a(_a), b(_b) {
        assert(a.defined() && "Max of undefined");
        assert(b.defined() && "Max of undefined");
        assert(b.type() == type && "Max of mismatched types");
    }
};

/** Is the first expression equal to the second */
struct EQ : public ExprNode<EQ> {
    Expr a, b;

    EQ(Expr _a, Expr _b) : ExprNode<EQ>(Bool(_a.type().width)), a(_a), b(_b) {
        assert(a.defined() && "EQ of undefined");
        assert(b.defined() && "EQ of undefined");
    }
};

/** Is the first expression not equal to the second */
struct NE : public ExprNode<NE> {
    Expr a, b;

    NE(Expr _a, Expr _b) : ExprNode<NE>(Bool(_a.type().width)), a(_a), b(_b) {
        assert(a.defined() && "NE of undefined");
        assert(b.defined() && "NE of undefined");
    }
};

/** Is the first expression less than the second. */
struct LT : public ExprNode<LT> {
    Expr a, b;

    LT(Expr _a, Expr _b) : ExprNode<LT>(Bool(_a.type().width)), a(_a), b(_b) {
        assert(a.defined() && "LT of undefined");
        assert(b.defined() && "LT of undefined");
    }
};

/** Is the first expression less than or equal to the second. */
struct LE : public ExprNode<LE> {
    Expr a, b;

    LE(Expr _a, Expr _b) : ExprNode<LE>(Bool(_a.type().width)), a(_a), b(_b) {
        assert(a.defined() && "LE of undefined");
        assert(b.defined() && "LE of undefined");
    }
};

/** Is the first expression greater than the second. */
struct GT : public ExprNode<GT> {
    Expr a, b;

    GT(Expr _a, Expr _b) : ExprNode<GT>(Bool(_a.type().width)), a(_a), b(_b) {
        assert(a.defined() && "GT of undefined");
        assert(b.defined() && "GT of undefined");
    }
};

/** Is the first expression greater than or equal to the second. */
struct GE : public ExprNode<GE> {
    Expr a, b;

    GE(Expr _a, Expr _b) : ExprNode<GE>(Bool(_a.type().width)), a(_a), b(_b) {
        assert(a.defined() && "GE of undefined");
        assert(b.defined() && "GE of undefined");
    }
};

/** Logical and - are both expressions true */
struct And : public ExprNode<And> {
    Expr a, b;

    And(Expr _a, Expr _b) : ExprNode<And>(Bool(_a.type().width)), a(_a), b(_b) {
        assert(a.defined() && "And of undefined");
        assert(b.defined() && "And of undefined");
        assert(a.type().is_bool() && "lhs of And is not a bool");
        assert(b.type().is_bool() && "rhs of And is not a bool");
    }
};

/** Logical or - is at least one of the expression true */
struct Or : public ExprNode<Or> {
    Expr a, b;

    Or(Expr _a, Expr _b) : ExprNode<Or>(Bool(_a.type().width)), a(_a), b(_b) {
        assert(a.defined() && "Or of undefined");
        assert(b.defined() && "Or of undefined");
        assert(a.type().is_bool() && "lhs of Or is not a bool");
        assert(b.type().is_bool() && "rhs of Or is not a bool");
    }
};

/** Logical not - true if the expression false */
struct Not : public ExprNode<Not> {
    Expr a;

    Not(Expr _a) : ExprNode<Not>(Bool(_a.type().width)), a(_a) {
        assert(a.defined() && "Not of undefined");
        assert(a.type().is_bool() && "argument of Not is not a bool");
    }
};

/** A ternary operator. Evalutes 'true_value' and 'false_value',
 * then selects between them based on 'condition'. Equivalent to
 * the ternary operator in C. */
struct Select : public ExprNode<Select> {
    Expr condition, true_value, false_value;

    Select(Expr c, Expr t, Expr f) : 
        ExprNode<Select>(t.type()), 
        condition(c), true_value(t), false_value(f) {
        assert(condition.defined() && "Select of undefined");
        assert(true_value.defined() && "Select of undefined");
        assert(false_value.defined() && "Select of undefined");
        assert(condition.type().is_bool() && "First argument to Select is not a bool");
        assert(false_value.type() == type && "Select of mismatched types");
        assert((condition.type().is_scalar() ||
                condition.type().width == type.width) &&
               "In Select, vector width of condition must either be 1, or equal to vector width of arguments");
    }
};

/** Load a value from a named buffer. The buffer is treated as an
 * array of the 'type' of this Load node. That is, the buffer has
 * no inherent type. */
struct Load : public ExprNode<Load> {
    std::string name;
    Expr index;

    // If it's a load from an image argument or compiled-in constant
    // image, this will point to that
    Buffer image;

    // If it's a load from an image parameter, this points to that
    Parameter param;

    Load(Type t, std::string b, Expr i, Buffer m, Parameter p) : 
        ExprNode<Load>(t), name(b), index(i), image(m), param(p) {
        assert(index.defined() && "Load of undefined");
        assert(type.width == i.type().width && "Vector width of Load must match vector width of index");
    }
};

/** A linear ramp vector node. This is vector with 'width' elements,
 * where element i is 'base' + i*'stride'. This is a convenient way to
 * pass around vectors without busting them up into individual
 * elements. E.g. a dense vector load from a buffer can use a ramp
 * node with stride 1 as the index. */
struct Ramp : public ExprNode<Ramp> {
    Expr base, stride;
    int width;

    Ramp(Expr base, Expr stride, int width) : 
        ExprNode<Ramp>(base.type().vector_of(width)),
        base(base), stride(stride), width(width) {
        assert(base.defined() && "Ramp of undefined");
        assert(stride.defined() && "Ramp of undefined");
        assert(base.type().is_scalar() && "Ramp with vector base");
        assert(stride.type().is_scalar() && "Ramp with vector stride");
        assert(width > 1 && "Ramp of width <= 1");
        assert(stride.type() == base.type() && "Ramp of mismatched types");
    }
};

/** A vector with 'width' elements, in which every element is
 * 'value'. This is a special case of the ramp node above, in which
 * the stride is zero. */
struct Broadcast : public ExprNode<Broadcast> {
    Expr value;
    int width;
        
    Broadcast(Expr value, int width) :
        ExprNode<Broadcast>(value.type().vector_of(width)), 
        value(value), width(width) {
        assert(value.defined() && "Broadcast of undefined");
        assert(value.type().is_scalar() && "Broadcast of vector");
        assert(width > 1 && "Broadcast of width <= 1");            
    }
};

/** A let expression, like you might find in a functional
 * language. Within the expression \ref body, instances of the Var
 * node \ref name refer to \ref value. */
struct Let : public ExprNode<Let> {
    std::string name;
    Expr value, body;

    Let(std::string n, Expr v, Expr b) : 
        ExprNode<Let>(b.type()), name(n), value(v), body(b) {
        assert(value.defined() && "Let of undefined");
        assert(body.defined() && "Let of undefined");
    }
};

/** The statement form of a let node. Within the statement 'body',
 * instances of the Var named 'name' refer to 'value' */
struct LetStmt : public StmtNode<LetStmt> {
    std::string name;
    Expr value;
    Stmt body;

    LetStmt(std::string n, Expr v, Stmt b) : 
        name(n), value(v), body(b) {
        assert(value.defined() && "LetStmt of undefined");
        assert(body.defined() && "LetStmt of undefined");
    }
};

/** Used largely for debugging and tracing. Dumps the 'prefix'
 * string and the args to stdout. */
struct PrintStmt : public StmtNode<PrintStmt> {
    std::string prefix;
    std::vector<Expr> args;

    PrintStmt(std::string p, const std::vector<Expr> &a) :
        prefix(p), args(a) {
        for (size_t i = 0; i < args.size(); i++) {
            assert(args[i].defined() && "PrintStmt of undefined");
        }
    }
};

/** If the 'condition' is false, then bail out printing the
 * 'message' to stderr */
struct AssertStmt : public StmtNode<AssertStmt> {
    // if condition then val else error out with message
    Expr condition;
    std::string message;

    AssertStmt(Expr c, std::string m) :
        condition(c), message(m) {
        assert(condition.defined() && "AssertStmt of undefined");
        assert(condition.type().is_scalar() && "AssertStmt of vector");
    }
};

/** This node is a helpful annotation to do with permissions. The
 * three child statements happen in order. In the 'produce'
 * statement 'buffer' is write-only. In 'update' it is
 * read-write. In 'consume' it is read-only. The 'update' node is
 * often NULL. (check update.defined() to find out). None of this
 * is actually enforced, the node is purely for informative
 * purposes to help out our analysis during lowering. */ 
struct Pipeline : public StmtNode<Pipeline> {
    std::string name;
    Stmt produce, update, consume;

    Pipeline(std::string b, Stmt p, Stmt u, Stmt c) : 
        name(b), produce(p), update(u), consume(c) {
        assert(produce.defined() && "Pipeline of undefined");
        // update is allowed to be null
        assert(consume.defined() && "Pipeline of undefined");
    }
};
    
/** A for loop. Execute the 'body' statement for all values of the
 * variable 'name' from 'min' to 'min + extent'. There are four
 * types of For nodes. A 'Serial' for loop is a conventional
 * one. In a 'Parallel' for loop, each iteration of the loop
 * happens in parallel or in some unspecified order. In a
 * 'Vectorized' for loop, each iteration maps to one SIMD lane,
 * and the whole loop is executed in one shot. For this case,
 * 'extent' must be some small integer constant (probably 4, 8, or
 * 16). An 'Unrolled' for loop compiles to a completely unrolled
 * version of the loop. Each iteration becomes its own
 * statement. Again in this case, 'extent' should be a small
 * integer constant. */
struct For : public StmtNode<For> {
    std::string name;
    Expr min, extent;
    typedef enum {Serial, Parallel, Vectorized, Unrolled} ForType;
    ForType for_type;
    Stmt body;

    For(std::string n, Expr m, Expr e, ForType f, Stmt b) :
        name(n), min(m), extent(e), for_type(f), body(b) {
        assert(min.defined() && "For of undefined");
        assert(extent.defined() && "For of undefined");
        assert(min.type().is_scalar() && "For with vector min");
        assert(extent.type().is_scalar() && "For with vector extent");
        assert(body.defined() && "For of undefined");
    }
};

/** Store a 'value' to a 'buffer' at a given 'index'. The buffer is
 * interpreted as an array of the same type as 'value'. */
struct Store : public StmtNode<Store> {
    std::string name;
    Expr value, index;

    Store(std::string b, Expr v, Expr i) :
        name(b), value(v), index(i) {
        assert(value.defined() && "Store of undefined");
        assert(index.defined() && "Store of undefined");
    }
};

/** This defines the value of a function at a multi-dimensional
 * location. You should think of it as a store to a
 * multi-dimensional array. It gets lowered to a conventional
 * Store node. */
struct Provide : public StmtNode<Provide> {
    std::string name;
    Expr value;
    std::vector<Expr> args;

    Provide(std::string b, Expr v, const std::vector<Expr> &a) : 
        name(b), value(v), args(a) {
        assert(value.defined() && "Provide of undefined");
        for (size_t i = 0; i < args.size(); i++) {
            assert(args[i].defined() && "Provide of undefined");
        }
    }
};

/** Allocate a scratch area called with the given name, type, and
 * size. The buffer lives for at most the duration of the body
 * statement, within which it is freed. It is an error for an allocate
 * node not to contain a free node of the same buffer. */
struct Allocate : public StmtNode<Allocate> {
    std::string name;
    Type type;
    Expr size;
    Stmt body;

    Allocate(std::string buf, Type t, Expr s, Stmt bod) : 
        name(buf), type(t), size(s), body(bod) {
        assert(size.defined() && "Allocate of undefined");
        assert(body.defined() && "Allocate of undefined");
        assert(size.type().is_scalar() == 1 && "Allocate of vector size");
    }
};

/** Free the resources associated with the given buffer. */
struct Free : public StmtNode<Free> {
    std::string name;
    Free(std::string buf) : name(buf) {}
};

/** A single-dimensional span. Includes all numbers between min and
 * (min + extent - 1) */
struct Range {
    Expr min, extent;
    Range() {}
    Range(Expr min, Expr extent) : min(min), extent(extent) {
        assert(min.type() == extent.type() && "Region min and extent must have same type");
    }
};

/** A multi-dimensional box. The outer product of the elements */
typedef std::vector<Range> Region;   

/** Allocate a multi-dimensional buffer of the given type and
 * size. Create some scratch memory that will back the function 'name'
 * over the range specified in 'bounds'. The bounds are a vector of
 * (min, extent) pairs for each dimension. */
struct Realize : public StmtNode<Realize> {
    std::string name;
    Type type;
    Region bounds;
    Stmt body;

    Realize(std::string buf, Type t, const Region &bou, Stmt bod) : 
        name(buf), type(t), bounds(bou), body(bod) {
        for (size_t i = 0; i < bounds.size(); i++) {
            assert(bounds[i].min.defined() && "Realize of undefined");
            assert(bounds[i].extent.defined() && "Realize of undefined");
            assert(bounds[i].min.type().is_scalar() && "Realize of vector size");
            assert(bounds[i].extent.type().is_scalar() && "Realize of vector size");
        }
        assert(body.defined() && "Realize of undefined");
    }
};

/** A sequence of statements to be executed in-order. 'rest' may be
 * NULL. Used rest.defined() to find out. */
struct Block : public StmtNode<Block> {
    Stmt first, rest;
        
    Block(Stmt f, Stmt r) : 
        first(f), rest(r) {
        assert(first.defined() && "Block of undefined");
        // rest is allowed to be null
    }
};

}
}
// Now that we've defined an Expr and ForType, we can include the definition of a function
#include "Function.h"

// And the definition of a reduction domain
#include "Reduction.h"

namespace Halide {
namespace Internal {

/** A function call. This can represent a call to some extern
 * function (like sin), but it's also our multi-dimensional
 * version of a Load, so it can be a load from an input image, or
 * a call to another halide function. The latter two types of call
 * nodes don't survive all the way down to code generation - the
 * lowering process converts them to Load nodes. */
struct Call : public ExprNode<Call> {
    std::string name;
    std::vector<Expr> args;
    typedef enum {Image, Extern, Halide} CallType;
    CallType call_type;

    // If it's a call to another halide function, this call node
    // holds onto a pointer to that function
    Function func;

    // If it's a call to an image, this call nodes hold a
    // pointer to that image's buffer
    Buffer image;

    // If it's a call to an image parameter, this call nodes holds a
    // pointer to that
    Parameter param;

    Call(Type t, std::string n, const std::vector<Expr> &a, CallType ct, 
         Function f, Buffer m, Parameter p) : 
        ExprNode<Call>(t), name(n), args(a), call_type(ct), func(f), image(m), param(p) {
        for (size_t i = 0; i < args.size(); i++) {
            assert(args[i].defined() && "Call of undefined");
        }
        if (call_type == Halide) {
            assert(func.value().defined() && "Call nodes to undefined halide function");
            assert(args.size() <= func.args().size() && "Call node with too many arguments.");
        } else if (call_type == Image) {
            assert((param.defined() || image.defined()) && "Call node to undefined image");
        }
    }

    /** Convenience constructor for calls to externally defined functions */
    Call(Type t, std::string n, const std::vector<Expr> &a) : 
        ExprNode<Call>(t), name(n), args(a), call_type(Extern), 
        func(Function()), image(Buffer()), param(Parameter()) {
        for (size_t i = 0; i < args.size(); i++) {
            assert(args[i].defined() && "Call of undefined");
        }
    }

    /** Convenience constructor for loads from concrete images */
    Call(Buffer b, const std::vector<Expr> &a) :
        ExprNode<Call>(b.type()), name(b.name()), args(a), call_type(Image), 
        func(Function()), image(b), param(Parameter()) {
    }

    /** Convenience constructor for loads from images parameters */
    Call(Parameter p, const std::vector<Expr> &a) :
        ExprNode<Call>(p.type()), name(p.name()), args(a), call_type(Image), 
        func(Function()), image(Buffer()), param(p) {
    }

    /** Convenience constructor for calls to other halide functions */
    Call(Function f, const std::vector<Expr> &a) :
        ExprNode<Call>(f.value().type()), name(f.name()), args(a), call_type(Halide), 
        func(f), image(Buffer()), param(Parameter()) {
    }
};

/** A named variable. Might be a loop variable, function argument,
 * parameter, reduction variable, or something defined by a Let or
 * LetStmt node. */
struct Variable : public ExprNode<Variable> {
    std::string name;

    /** References to scalar parameters, or to the dimensions of buffer
     * parameters hang onto those expressions */
    Parameter param;

    /** Reduction variables hang onto their domains */
    ReductionDomain reduction_domain;

    Variable(Type t, std::string n, Parameter p) : 
        ExprNode<Variable>(t), name(n), param(p) {}
    Variable(Type t, std::string n, ReductionDomain d) : 
        ExprNode<Variable>(t), name(n), reduction_domain(d) {}
    Variable(Type t, std::string n, Parameter p, ReductionDomain d) : 
        ExprNode<Variable>(t), name(n), param(p), reduction_domain(d) {}

    Variable(Type t, std::string n) : ExprNode<Variable>(t), name(n) {}
};

}
}

#endif
