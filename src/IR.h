#ifndef HALIDE_IR_H
#define HALIDE_IR_H

/** \file
 * Halide expressions (\ref Halide::Expr) and statements (\ref Halide::Internal::Stmt)
 */

#include <string>
#include <vector>

#include "Debug.h"
#include "Error.h"
#include "IRVisitor.h"
#include "Buffer.h"
#include "Type.h"
#include "IntrusivePtr.h"
#include "Util.h"

namespace Halide {

namespace Internal {

/** A class representing a type of IR node (e.g. Add, or Mul, or
 * For). We use it for rtti (without having to compile with rtti). */
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

template<>
EXPORT inline RefCount &ref_count<IRNode>(const IRNode *n) {return n->ref_count;}

template<>
EXPORT inline void destroy<IRNode>(const IRNode *n) {delete n;}

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
};

/** We use the "curiously recurring template pattern" to avoid
   duplicated code in the IR Nodes. These classes live between the
   abstract base classes and the actual IR Nodes in the
   inheritance hierarchy. It provides an implementation of the
   accept function necessary for the visitor pattern to work, and
   a concrete instantiation of a unique IRNodeType per class. */
template<typename T>
struct ExprNode : public BaseExprNode {
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
        if (ptr->type_info() == &T::_type_info) {
            return (const T *)ptr;
        }
        return NULL;
    }
};

/** Integer constants */
struct IntImm : public ExprNode<IntImm> {
    int value;

    static IntImm *make(int value) {
        if (value >= -8 && value <= 8 &&
            !small_int_cache[value + 8].ref_count.is_zero()) {
            return &small_int_cache[value + 8];
        }
        IntImm *node = new IntImm;
        node->type = Int(32);
        node->value = value;
        return node;
    }

private:
    /** ints from -8 to 8 */
    static IntImm small_int_cache[17];
};

/** Floating point constants */
struct FloatImm : public ExprNode<FloatImm> {
    float value;

    static FloatImm *make(float value) {
        FloatImm *node = new FloatImm;
        node->type = Float(32);
        node->value = value;
        return node;
    }
};

/** String constants */
struct StringImm : public ExprNode<StringImm> {
    std::string value;

    static StringImm *make(const std::string &val) {
        StringImm *node = new StringImm;
        node->type = Handle();
        node->value = val;
        return node;
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
    EXPORT Expr(int x) : IRHandle(Internal::IntImm::make(x)) {
    }

    /** Make an expression representing a const 32-bit float (i.e. a FloatImm) */
    EXPORT Expr(float x) : IRHandle(Internal::FloatImm::make(x)) {
    }

    /** Make an expression representing a const 32-bit float, given a
     * double. Also emits a warning due to truncation. */
    EXPORT Expr(double x) : IRHandle(Internal::FloatImm::make((float)x)) {
        user_warning << "Halide cannot represent double constants. "
                     << "Converting " << x << " to float. "
                     << "If you wanted a double, use cast<double>(" << x
                     << (x == (int64_t)(x) ? ".0f" : "f")
                     << ")\n";
    }

    /** Make an expression representing a const string (i.e. a StringImm) */
    EXPORT Expr(const std::string &s) : IRHandle(Internal::StringImm::make(s)) {
    }

    /** Get the type of this expression node */
    Type type() const {
        return ((const Internal::BaseExprNode *)ptr)->type;
    }
};

/** This lets you use an Expr as a key in a map of the form
 * map<Expr, Foo, ExprCompare> */
struct ExprCompare {
    bool operator()(Expr a, Expr b) const {
        return a.ptr < b.ptr;
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

    /** This lets you use a Stmt as a key in a map of the form
     * map<Stmt, Foo, Stmt::Compare> */
    struct Compare {
        bool operator()(const Stmt &a, const Stmt &b) const {
            return a.ptr < b.ptr;
        }
    };
};

/** The actual IR nodes begin here. Remember that all the Expr
 * nodes also have a public "type" property */

}

namespace Internal {

/** Cast a node from one type to another */
struct Cast : public ExprNode<Cast> {
    Expr value;

    static Expr make(Type t, Expr v) {
        internal_assert(v.defined()) << "Cast of undefined\n";

        Cast *node = new Cast;
        node->type = t;
        node->value = v;
        return node;
    }
};

/** The sum of two expressions */
struct Add : public ExprNode<Add> {
    Expr a, b;

    static Expr make(Expr a, Expr b) {
        internal_assert(a.defined()) << "Add of undefined\n";
        internal_assert(b.defined()) << "Add of undefined\n";
        internal_assert(a.type() == b.type()) << "Add of mismatched types\n";

        Add *node = new Add;
        node->type = a.type();
        node->a = a;
        node->b = b;
        return node;
    }
};

/** The difference of two expressions */
struct Sub : public ExprNode<Sub> {
    Expr a, b;

    static Expr make(Expr a, Expr b) {
        internal_assert(a.defined()) << "Sub of undefined\n";
        internal_assert(b.defined()) << "Sub of undefined\n";
        internal_assert(a.type() == b.type()) << "Sub of mismatched types\n";

        Sub *node = new Sub;
        node->type = a.type();
        node->a = a;
        node->b = b;
        return node;
    }
};

/** The product of two expressions */
struct Mul : public ExprNode<Mul> {
    Expr a, b;

    static Expr make(Expr a, Expr b) {
        internal_assert(a.defined()) << "Mul of undefined\n";
        internal_assert(b.defined()) << "Mul of undefined\n";
        internal_assert(a.type() == b.type()) << "Mul of mismatched types\n";

        Mul *node = new Mul;
        node->type = a.type();
        node->a = a;
        node->b = b;
        return node;
    }
};

/** The ratio of two expressions */
struct Div : public ExprNode<Div> {
    Expr a, b;

    static Expr make(Expr a, Expr b) {
        internal_assert(a.defined()) << "Div of undefined\n";
        internal_assert(b.defined()) << "Div of undefined\n";
        internal_assert(a.type() == b.type()) << "Div of mismatched types\n";

        Div *node = new Div;
        node->type = a.type();
        node->a = a;
        node->b = b;
        return node;
    }
};

/** The remainder of a / b. Mostly equivalent to '%' in C, except that
 * the result here is always positive. For floats, this is equivalent
 * to calling fmod. */
struct Mod : public ExprNode<Mod> {
    Expr a, b;

    static Expr make(Expr a, Expr b) {
        internal_assert(a.defined()) << "Mod of undefined\n";
        internal_assert(b.defined()) << "Mod of undefined\n";
        internal_assert(a.type() == b.type()) << "Mod of mismatched types\n";

        Mod *node = new Mod;
        node->type = a.type();
        node->a = a;
        node->b = b;
        return node;
    }
};

/** The lesser of two values. */
struct Min : public ExprNode<Min> {
    Expr a, b;

    static Expr make(Expr a, Expr b) {
        internal_assert(a.defined()) << "Min of undefined\n";
        internal_assert(b.defined()) << "Min of undefined\n";
        internal_assert(a.type() == b.type()) << "Min of mismatched types\n";

        Min *node = new Min;
        node->type = a.type();
        node->a = a;
        node->b = b;
        return node;
    }
};

/** The greater of two values */
struct Max : public ExprNode<Max> {
    Expr a, b;

    static Expr make(Expr a, Expr b) {
        internal_assert(a.defined()) << "Max of undefined\n";
        internal_assert(b.defined()) << "Max of undefined\n";
        internal_assert(a.type() == b.type()) << "Max of mismatched types\n";

        Max *node = new Max;
        node->type = a.type();
        node->a = a;
        node->b = b;
        return node;
    }
};

/** Is the first expression equal to the second */
struct EQ : public ExprNode<EQ> {
    Expr a, b;

    static Expr make(Expr a, Expr b) {
        internal_assert(a.defined()) << "EQ of undefined\n";
        internal_assert(b.defined()) << "EQ of undefined\n";
        internal_assert(a.type() == b.type()) << "EQ of mismatched types\n";

        EQ *node = new EQ;
        node->type = Bool(a.type().width);
        node->a = a;
        node->b = b;
        return node;
    }
};

/** Is the first expression not equal to the second */
struct NE : public ExprNode<NE> {
    Expr a, b;

    static Expr make(Expr a, Expr b) {
        internal_assert(a.defined()) << "NE of undefined\n";
        internal_assert(b.defined()) << "NE of undefined\n";
        internal_assert(a.type() == b.type()) << "NE of mismatched types\n";

        NE *node = new NE;
        node->type = Bool(a.type().width);
        node->a = a;
        node->b = b;
        return node;
    }
};

/** Is the first expression less than the second. */
struct LT : public ExprNode<LT> {
    Expr a, b;

    static Expr make(Expr a, Expr b) {
        internal_assert(a.defined()) << "LT of undefined\n";
        internal_assert(b.defined()) << "LT of undefined\n";
        internal_assert(a.type() == b.type()) << "LT of mismatched types\n";

        LT *node = new LT;
        node->type = Bool(a.type().width);
        node->a = a;
        node->b = b;
        return node;
    }
};

/** Is the first expression less than or equal to the second. */
struct LE : public ExprNode<LE> {
    Expr a, b;

    static Expr make(Expr a, Expr b) {
        internal_assert(a.defined()) << "LE of undefined\n";
        internal_assert(b.defined()) << "LE of undefined\n";
        internal_assert(a.type() == b.type()) << "LE of mismatched types\n";

        LE *node = new LE;
        node->type = Bool(a.type().width);
        node->a = a;
        node->b = b;
        return node;
    }
};

/** Is the first expression greater than the second. */
struct GT : public ExprNode<GT> {
    Expr a, b;

    static Expr make(Expr a, Expr b) {
        internal_assert(a.defined()) << "GT of undefined\n";
        internal_assert(b.defined()) << "GT of undefined\n";
        internal_assert(a.type() == b.type()) << "GT of mismatched types\n";

        GT *node = new GT;
        node->type = Bool(a.type().width);
        node->a = a;
        node->b = b;
        return node;
    }
};

/** Is the first expression greater than or equal to the second. */
struct GE : public ExprNode<GE> {
    Expr a, b;

    static Expr make(Expr a, Expr b) {
        internal_assert(a.defined()) << "GE of undefined\n";
        internal_assert(b.defined()) << "GE of undefined\n";
        internal_assert(a.type() == b.type()) << "GE of mismatched types\n";

        GE *node = new GE;
        node->type = Bool(a.type().width);
        node->a = a;
        node->b = b;
        return node;
    }
};

/** Logical and - are both expressions true */
struct And : public ExprNode<And> {
    Expr a, b;

    static Expr make(Expr a, Expr b) {
        internal_assert(a.defined()) << "And of undefined\n";
        internal_assert(b.defined()) << "And of undefined\n";
        internal_assert(a.type().is_bool()) << "lhs of And is not a bool\n";
        internal_assert(b.type().is_bool()) << "rhs of And is not a bool\n";

        And *node = new And;
        node->type = Bool(a.type().width);
        node->a = a;
        node->b = b;
        return node;
    }
};

/** Logical or - is at least one of the expression true */
struct Or : public ExprNode<Or> {
    Expr a, b;

    static Expr make(Expr a, Expr b) {
        internal_assert(a.defined()) << "Or of undefined\n";
        internal_assert(b.defined()) << "Or of undefined\n";
        internal_assert(a.type().is_bool()) << "lhs of Or is not a bool\n";
        internal_assert(b.type().is_bool()) << "rhs of Or is not a bool\n";

        Or *node = new Or;
        node->type = Bool(a.type().width);
        node->a = a;
        node->b = b;
        return node;
    }
};

/** Logical not - true if the expression false */
struct Not : public ExprNode<Not> {
    Expr a;

    static Expr make(Expr a) {
        internal_assert(a.defined()) << "Not of undefined\n";
        internal_assert(a.type().is_bool()) << "argument of Not is not a bool\n";

        Not *node = new Not;
        node->type = Bool(a.type().width);
        node->a = a;
        return node;
    }
};

/** A ternary operator. Evalutes 'true_value' and 'false_value',
 * then selects between them based on 'condition'. Equivalent to
 * the ternary operator in C. */
struct Select : public ExprNode<Select> {
    Expr condition, true_value, false_value;

    static Expr make(Expr condition, Expr true_value, Expr false_value) {
        internal_assert(condition.defined()) << "Select of undefined\n";
        internal_assert(true_value.defined()) << "Select of undefined\n";
        internal_assert(false_value.defined()) << "Select of undefined\n";
        internal_assert(condition.type().is_bool()) << "First argument to Select is not a bool\n";
        internal_assert(false_value.type() == true_value.type()) << "Select of mismatched types\n";
        internal_assert(condition.type().is_scalar() ||
                        condition.type().width == true_value.type().width)
            << "In Select, vector width of condition must either be 1, or equal to vector width of arguments\n";

        Select *node = new Select;
        node->type = true_value.type();
        node->condition = condition;
        node->true_value = true_value;
        node->false_value = false_value;
        return node;
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

    static Expr make(Type type, std::string name, Expr index, Buffer image, Parameter param) {
        internal_assert(index.defined()) << "Load of undefined\n";
        internal_assert(type.width == index.type().width) << "Vector width of Load must match vector width of index\n";

        Load *node = new Load;
        node->type = type;
        node->name = name;
        node->index = index;
        node->image = image;
        node->param = param;
        return node;
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

    static Expr make(Expr base, Expr stride, int width) {
        internal_assert(base.defined()) << "Ramp of undefined\n";
        internal_assert(stride.defined()) << "Ramp of undefined\n";
        internal_assert(base.type().is_scalar()) << "Ramp with vector base\n";
        internal_assert(stride.type().is_scalar()) << "Ramp with vector stride\n";
        internal_assert(width > 1) << "Ramp of width <= 1\n";
        internal_assert(stride.type() == base.type()) << "Ramp of mismatched types\n";

        Ramp *node = new Ramp;
        node->type = base.type().vector_of(width);
        node->base = base;
        node->stride = stride;
        node->width = width;
        return node;
    }
};

/** A vector with 'width' elements, in which every element is
 * 'value'. This is a special case of the ramp node above, in which
 * the stride is zero. */
struct Broadcast : public ExprNode<Broadcast> {
    Expr value;
    int width;

    static Expr make(Expr value, int width) {
        internal_assert(value.defined()) << "Broadcast of undefined\n";
        internal_assert(value.type().is_scalar()) << "Broadcast of vector\n";
        internal_assert(width > 1) << "Broadcast of width <= 1\n";

        Broadcast *node = new Broadcast;
        node->type = value.type().vector_of(width);
        node->value = value;
        node->width = width;
        return node;
    }
};

/** A let expression, like you might find in a functional
 * language. Within the expression \ref Let::body, instances of the Var
 * node \ref Let::name refer to \ref Let::value. */
struct Let : public ExprNode<Let> {
    std::string name;
    Expr value, body;

    static Expr make(std::string name, Expr value, Expr body) {
        internal_assert(value.defined()) << "Let of undefined\n";
        internal_assert(body.defined()) << "Let of undefined\n";

        Let *node = new Let;
        node->type = body.type();
        node->name = name;
        node->value = value;
        node->body = body;
        return node;
    }
};

/** The statement form of a let node. Within the statement 'body',
 * instances of the Var named 'name' refer to 'value' */
struct LetStmt : public StmtNode<LetStmt> {
    std::string name;
    Expr value;
    Stmt body;

    static Stmt make(std::string name, Expr value, Stmt body) {
        internal_assert(value.defined()) << "Let of undefined\n";
        internal_assert(body.defined()) << "Let of undefined\n";

        LetStmt *node = new LetStmt;
        node->name = name;
        node->value = value;
        node->body = body;
        return node;
    }
};

/** If the 'condition' is false, then bail out printing the
 * 'message' to stderr */
struct AssertStmt : public StmtNode<AssertStmt> {
    // if condition then val else error out with message
    Expr condition;
    std::string message;
    std::vector<Expr> args;

    static Stmt make(Expr condition, std::string message, const std::vector<Expr> &args) {
        internal_assert(condition.defined()) << "AssertStmt of undefined\n";

        AssertStmt *node = new AssertStmt;
        node->condition = condition;
        node->message = message;
        node->args = args;
        return node;
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

    static Stmt make(std::string name, Stmt produce, Stmt update, Stmt consume) {
        internal_assert(produce.defined()) << "Pipeline of undefined\n";
        // update is allowed to be null
        internal_assert(consume.defined()) << "Pipeline of undefined\n";

        Pipeline *node = new Pipeline;
        node->name = name;
        node->produce = produce;
        node->update = update;
        node->consume = consume;
        return node;
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

    static Stmt make(std::string name, Expr min, Expr extent, ForType for_type, Stmt body) {
        internal_assert(min.defined()) << "For of undefined\n";
        internal_assert(extent.defined()) << "For of undefined\n";
        internal_assert(min.type().is_scalar()) << "For with vector min\n";
        internal_assert(extent.type().is_scalar()) << "For with vector extent\n";
        internal_assert(body.defined()) << "For of undefined\n";

        For *node = new For;
        node->name = name;
        node->min = min;
        node->extent = extent;
        node->for_type = for_type;
        node->body = body;
        return node;
    }
};

/** Store a 'value' to the buffer called 'name' at a given
 * 'index'. The buffer is interpreted as an array of the same type as
 * 'value'. */
struct Store : public StmtNode<Store> {
    std::string name;
    Expr value, index;

    static Stmt make(std::string name, Expr value, Expr index) {
        internal_assert(value.defined()) << "Store of undefined\n";
        internal_assert(index.defined()) << "Store of undefined\n";

        Store *node = new Store;
        node->name = name;
        node->value = value;
        node->index = index;
        return node;
    }
};

/** This defines the value of a function at a multi-dimensional
 * location. You should think of it as a store to a
 * multi-dimensional array. It gets lowered to a conventional
 * Store node. */
struct Provide : public StmtNode<Provide> {
    std::string name;
    std::vector<Expr> values;
    std::vector<Expr> args;

    static Stmt make(std::string name, const std::vector<Expr> &values, const std::vector<Expr> &args) {
        internal_assert(!values.empty()) << "Provide of no values\n";
        for (size_t i = 0; i < values.size(); i++) {
            internal_assert(values[i].defined()) << "Provide of undefined value\n";
        }
        for (size_t i = 0; i < args.size(); i++) {
            internal_assert(args[i].defined()) << "Provide to undefined location\n";
        }

        Provide *node = new Provide;
        node->name = name;
        node->values = values;
        node->args = args;
        return node;
    }
};

/** Allocate a scratch area called with the given name, type, and
 * size. The buffer lives for at most the duration of the body
 * statement, within which it is freed. It is an error for an allocate
 * node not to contain a free node of the same buffer. */
struct Allocate : public StmtNode<Allocate> {
    std::string name;
    Type type;
    std::vector<Expr> extents;
    Stmt body;

    static Stmt make(std::string name, Type type, const std::vector<Expr> &extents, Stmt body) {
        for (size_t i = 0; i < extents.size(); i++) {
            internal_assert(extents[i].defined()) << "Allocate of undefined extent\n";
            internal_assert(extents[i].type().is_scalar() == 1) << "Allocate of vector extent\n";
        }
        internal_assert(body.defined()) << "Allocate of undefined\n";

        Allocate *node = new Allocate;
        node->name = name;
        node->type = type;
        node->extents = extents;

        node->body = body;
        return node;
    }
};

/** Free the resources associated with the given buffer. */
struct Free : public StmtNode<Free> {
    std::string name;

    static Stmt make(std::string name) {
        Free *node = new Free;
        node->name = name;
        return node;
    }
};

/** A single-dimensional span. Includes all numbers between min and
 * (min + extent - 1) */
struct Range {
    Expr min, extent;
    Range() {}
    Range(Expr min, Expr extent) : min(min), extent(extent) {
        internal_assert(min.type() == extent.type()) << "Region min and extent must have same type\n";
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
    std::vector<Type> types;
    Region bounds;
    Stmt body;

    static Stmt make(const std::string &name, const std::vector<Type> &types, const Region &bounds, Stmt body) {
        for (size_t i = 0; i < bounds.size(); i++) {
            internal_assert(bounds[i].min.defined()) << "Realize of undefined\n";
            internal_assert(bounds[i].extent.defined()) << "Realize of undefined\n";
            internal_assert(bounds[i].min.type().is_scalar()) << "Realize of vector size\n";
            internal_assert(bounds[i].extent.type().is_scalar()) << "Realize of vector size\n";
        }
        internal_assert(body.defined()) << "Realize of undefined\n";
        internal_assert(!types.empty()) << "Realize has empty type\n";

        Realize *node = new Realize;
        node->name = name;
        node->types = types;
        node->bounds = bounds;
        node->body = body;
        return node;
    }
};

/** A sequence of statements to be executed in-order. 'rest' may be
 * NULL. Used rest.defined() to find out. */
struct Block : public StmtNode<Block> {
    Stmt first, rest;

    static Stmt make(Stmt first, Stmt rest) {
        internal_assert(first.defined()) << "Block of undefined\n";
        // rest is allowed to be null

        Block *node = new Block;
        node->first = first;
        node->rest = rest;
        return node;
    }
};

/** An if-then-else block. 'else' may be NULL. */
struct IfThenElse : public StmtNode<IfThenElse> {
    Expr condition;
    Stmt then_case, else_case;

    static Stmt make(Expr condition, Stmt then_case, Stmt else_case = Stmt()) {
        internal_assert(condition.defined() && then_case.defined()) << "IfThenElse of undefined\n";
        // else_case may be null.

        IfThenElse *node = new IfThenElse;
        node->condition = condition;
        node->then_case = then_case;
        node->else_case = else_case;
        return node;
    }
};

/** Evaluate and discard an expression, presumably because it has some side-effect. */
struct Evaluate : public StmtNode<Evaluate> {
    Expr value;

    static Stmt make(Expr v) {
        internal_assert(v.defined()) << "Evaluate of undefined\n";

        Evaluate *node = new Evaluate;
        node->value = v;
        return node;
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
    typedef enum {Image, Extern, Halide, Intrinsic} CallType;
    CallType call_type;

    // Halide uses calls internally to represent certain operations
    // (instead of IR nodes). These are matched by name.
    EXPORT static const std::string debug_to_file,
        shuffle_vector,
        interleave_vectors,
        reinterpret,
        bitwise_and,
        bitwise_not,
        bitwise_xor,
        bitwise_or,
        shift_left,
        shift_right,
        abs,
        rewrite_buffer,
        profiling_timer,
        random,
        lerp,
        create_buffer_t,
        extract_buffer_min,
        extract_buffer_max,
        set_host_dirty,
        set_dev_dirty,
        popcount,
        count_leading_zeros,
        count_trailing_zeros,
        undef,
        null_handle,
        address_of,
        return_second,
        if_then_else,
        trace,
        trace_expr,
        glsl_texture_load, 
        glsl_texture_store,
        cache_expr,
        copy_memory;

    // If it's a call to another halide function, this call node
    // holds onto a pointer to that function.
    Function func;

    // If that function has multiple values, which value does this
    // call node refer to?
    int value_index;

    // If it's a call to an image, this call nodes hold a
    // pointer to that image's buffer
    Buffer image;

    // If it's a call to an image parameter, this call node holds a
    // pointer to that
    Parameter param;

    static Expr make(Type type, std::string name, const std::vector<Expr> &args, CallType call_type,
                     Function func = Function(), int value_index = 0,
                     Buffer image = Buffer(), Parameter param = Parameter()) {
        for (size_t i = 0; i < args.size(); i++) {
            internal_assert(args[i].defined()) << "Call of undefined\n";
        }
        if (call_type == Halide) {
            internal_assert(value_index >= 0 &&
                            value_index < func.outputs())
                << "Value index out of range in call to halide function\n";
            internal_assert((func.has_pure_definition() || func.has_extern_definition())) << "Call to undefined halide function\n";
            internal_assert((int)args.size() <= func.dimensions()) << "Call node with too many arguments.\n";
            for (size_t i = 0; i < args.size(); i++) {
                internal_assert(args[i].type() == Int(32)) << "Args to call to halide function must be type Int(32)\n";
            }
        } else if (call_type == Image) {
            internal_assert((param.defined() || image.defined())) << "Call node to undefined image\n";
            for (size_t i = 0; i < args.size(); i++) {
                internal_assert(args[i].type() == Int(32)) << "Args to load from image must be type Int(32)\n";
            }
        }

        Call *node = new Call;
        node->type = type;
        node->name = name;
        node->args = args;
        node->call_type = call_type;
        node->func = func;
        node->value_index = value_index;
        node->image = image;
        node->param = param;
        return node;
    }

    /** Convenience constructor for calls to other halide functions */
    static Expr make(Function func, const std::vector<Expr> &args, int idx = 0) {
        internal_assert(idx >= 0 &&
                        idx < func.outputs())
            << "Value index out of range in call to halide function\n";
        internal_assert(func.has_pure_definition() || func.has_extern_definition())
            << "Call to undefined halide function\n";
        return make(func.output_types()[idx], func.name(), args, Halide, func, idx, Buffer(), Parameter());
    }

    /** Convenience constructor for loads from concrete images */
    static Expr make(Buffer image, const std::vector<Expr> &args) {
        return make(image.type(), image.name(), args, Image, Function(), 0, image, Parameter());
    }

    /** Convenience constructor for loads from images parameters */
    static Expr make(Parameter param, const std::vector<Expr> &args) {
        return make(param.type(), param.name(), args, Image, Function(), 0, Buffer(), param);
    }

};

/** A named variable. Might be a loop variable, function argument,
 * parameter, reduction variable, or something defined by a Let or
 * LetStmt node. */
struct Variable : public ExprNode<Variable> {
    std::string name;

    /** References to scalar parameters, or to the dimensions of buffer
     * parameters hang onto those expressions. */
    Parameter param;

    /** References to properties of literal image parameters. */
    Buffer image;

    /** Reduction variables hang onto their domains */
    ReductionDomain reduction_domain;

    static Expr make(Type type, std::string name) {
        return make(type, name, Buffer(), Parameter(), ReductionDomain());
    }

    static Expr make(Type type, std::string name, Parameter param) {
        return make(type, name, Buffer(), param, ReductionDomain());
    }

    static Expr make(Type type, std::string name, Buffer image) {
        return make(type, name, image, Parameter(), ReductionDomain());
    }

    static Expr make(Type type, std::string name, ReductionDomain reduction_domain) {
        return make(type, name, Buffer(), Parameter(), reduction_domain);
    }

    static Expr make(Type type, std::string name, Buffer image, Parameter param, ReductionDomain reduction_domain) {
        Variable *node = new Variable;
        node->type = type;
        node->name = name;
        node->image = image;
        node->param = param;
        node->reduction_domain = reduction_domain;
        return node;
    }

};

}
}

#endif
