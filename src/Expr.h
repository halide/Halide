#ifndef HALIDE_EXPR_H
#define HALIDE_EXPR_H

/** \file
 * Base classes for Halide expressions (\ref Halide::Expr) and statements (\ref Halide::Internal::Stmt)
 */

#include <string>
#include <vector>

#include "Debug.h"
#include "Error.h"
#include "Float16.h"
#include "Type.h"
#include "IntrusivePtr.h"
#include "Util.h"

namespace Halide {
namespace Internal {

class IRVisitor;

/** All our IR node types get unique IDs for the purposes of RTTI */
enum class IRNodeType {
    IntImm,
    UIntImm,
    FloatImm,
    StringImm,
    Cast,
    Variable,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Min,
    Max,
    EQ,
    NE,
    LT,
    LE,
    GT,
    GE,
    And,
    Or,
    Not,
    Select,
    Load,
    Ramp,
    Broadcast,
    Call,
    Let,
    LetStmt,
    AssertStmt,
    ProducerConsumer,
    For,
    Store,
    Provide,
    Allocate,
    Free,
    Realize,
    Block,
    IfThenElse,
    Evaluate,
    Shuffle,
    Prefetch,
};

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
    virtual IRNodeType type_info() const = 0;
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
    EXPORT void accept(IRVisitor *v) const;
    virtual IRNodeType type_info() const {return T::_type_info;}
    virtual ~ExprNode() {}
};

template<typename T>
struct StmtNode : public BaseStmtNode {
    EXPORT void accept(IRVisitor *v) const;
    virtual IRNodeType type_info() const {return T::_type_info;}
    virtual ~StmtNode() {}
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
     * Select). This returns nullptr if the node is not of the requested
     * type. Example usage:
     *
     * if (const Add *add = node->as<Add>()) {
     *   // This is an add node
     * }
     */
    template<typename T> const T *as() const {
        if (ptr && ptr->type_info() == T::_type_info) {
            return (const T *)ptr;
        }
        return nullptr;
    }
};


/** Integer constants */
struct IntImm : public ExprNode<IntImm> {
    int64_t value;

    static const IntImm *make(Type t, int64_t value) {
        internal_assert(t.is_int() && t.is_scalar())
            << "IntImm must be a scalar Int\n";
        internal_assert(t.bits() == 8 || t.bits() == 16 || t.bits() == 32 || t.bits() == 64)
            << "IntImm must be 8, 16, 32, or 64-bit\n";

        // Normalize the value by dropping the high bits
        value <<= (64 - t.bits());
        // Then sign-extending to get them back
        value >>= (64 - t.bits());

        IntImm *node = new IntImm;
        node->type = t;
        node->value = value;
        return node;
    }

    static const IRNodeType _type_info = IRNodeType::IntImm;
};

/** Unsigned integer constants */
struct UIntImm : public ExprNode<UIntImm> {
    uint64_t value;

    static const UIntImm *make(Type t, uint64_t value) {
        internal_assert(t.is_uint() && t.is_scalar())
            << "UIntImm must be a scalar UInt\n";
        internal_assert(t.bits() == 1 || t.bits() == 8 || t.bits() == 16 || t.bits() == 32 || t.bits() == 64)
            << "UIntImm must be 1, 8, 16, 32, or 64-bit\n";

        // Normalize the value by dropping the high bits
        value <<= (64 - t.bits());
        value >>= (64 - t.bits());

        UIntImm *node = new UIntImm;
        node->type = t;
        node->value = value;
        return node;
    }

    static const IRNodeType _type_info = IRNodeType::UIntImm;
};

/** Floating point constants */
struct FloatImm : public ExprNode<FloatImm> {
    double value;

    static const FloatImm *make(Type t, double value) {
        internal_assert(t.is_float() && t.is_scalar())
            << "FloatImm must be a scalar Float\n";
        FloatImm *node = new FloatImm;
        node->type = t;
        switch (t.bits()) {
        case 16:
            node->value = (double)((float16_t)value);
            break;
        case 32:
            node->value = (float)value;
            break;
        case 64:
            node->value = value;
            break;
        default:
            internal_error << "FloatImm must be 16, 32, or 64-bit\n";
        }

        return node;
    }

    static const IRNodeType _type_info = IRNodeType::FloatImm;
};

/** String constants */
struct StringImm : public ExprNode<StringImm> {
    std::string value;

    static const StringImm *make(const std::string &val) {
        StringImm *node = new StringImm;
        node->type = type_of<const char *>();
        node->value = val;
        return node;
    }

    static const IRNodeType _type_info = IRNodeType::StringImm;
};

}  // namespace Internal

/** A fragment of Halide syntax. It's implemented as reference-counted
 * handle to a concrete expression node, but it's immutable, so you
 * can treat it as a value type. */
struct Expr : public Internal::IRHandle {
    /** Make an undefined expression */
    Expr() : Internal::IRHandle() {}

    /** Make an expression from a concrete expression node pointer (e.g. Add) */
    Expr(const Internal::BaseExprNode *n) : IRHandle(n) {}


    /** Make an expression representing numeric constants of various types. */
    // @{
    EXPORT explicit Expr(int8_t x)    : IRHandle(Internal::IntImm::make(Int(8), x)) {}
    EXPORT explicit Expr(int16_t x)   : IRHandle(Internal::IntImm::make(Int(16), x)) {}
    EXPORT          Expr(int32_t x)   : IRHandle(Internal::IntImm::make(Int(32), x)) {}
    EXPORT explicit Expr(int64_t x)   : IRHandle(Internal::IntImm::make(Int(64), x)) {}
    EXPORT explicit Expr(uint8_t x)   : IRHandle(Internal::UIntImm::make(UInt(8), x)) {}
    EXPORT explicit Expr(uint16_t x)  : IRHandle(Internal::UIntImm::make(UInt(16), x)) {}
    EXPORT explicit Expr(uint32_t x)  : IRHandle(Internal::UIntImm::make(UInt(32), x)) {}
    EXPORT explicit Expr(uint64_t x)  : IRHandle(Internal::UIntImm::make(UInt(64), x)) {}
    EXPORT          Expr(float16_t x) : IRHandle(Internal::FloatImm::make(Float(16), (double)x)) {}
    EXPORT          Expr(float x)     : IRHandle(Internal::FloatImm::make(Float(32), x)) {}
    EXPORT explicit Expr(double x)    : IRHandle(Internal::FloatImm::make(Float(64), x)) {}
    // @}

    /** Make an expression representing a const string (i.e. a StringImm) */
    EXPORT          Expr(const std::string &s) : IRHandle(Internal::StringImm::make(s)) {}

    /** Get the type of this expression node */
    Type type() const {
        return ((const Internal::BaseExprNode *)ptr)->type;
    }
};

/** This lets you use an Expr as a key in a map of the form
 * map<Expr, Foo, ExprCompare> */
struct ExprCompare {
    bool operator()(const Expr &a, const Expr &b) const {
        return a.get() < b.get();
    }
};

/** An enum describing a type of device API. Used by schedules, and in
 * the For loop IR node. */
enum class DeviceAPI {
    None, /// Used to denote for loops that run on the same device as the containing code.
    Host,
    Default_GPU,
    CUDA,
    OpenCL,
    GLSL,
    OpenGLCompute,
    Metal,
    Hexagon
};

/** An array containing all the device apis. Useful for iterating
 * through them. */
const DeviceAPI all_device_apis[] = {DeviceAPI::None,
                                     DeviceAPI::Host,
                                     DeviceAPI::Default_GPU,
                                     DeviceAPI::CUDA,
                                     DeviceAPI::OpenCL,
                                     DeviceAPI::GLSL,
                                     DeviceAPI::OpenGLCompute,
                                     DeviceAPI::Metal,
                                     DeviceAPI::Hexagon};

namespace Internal {

/** An enum describing a type of loop traversal. Used in schedules, and in
 * the For loop IR node. GPUBlock and GPUThread are implicitly parallel */
enum class ForType {
    Serial,
    Parallel,
    Vectorized,
    Unrolled,
    GPUBlock,
    GPUThread
};


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


}  // namespace Internal
}  // namespace Halide

#endif
