#ifndef HALIDE_EXPR_H
#define HALIDE_EXPR_H

/** \file
 * Base classes for Halide expressions (\ref Halide::Expr) and statements (\ref Halide::Internal::Stmt)
 */

#include <string>
#include <vector>

#include "Debug.h"
#include "Error.h"
#include "IRVisitor.h"
#include "Type.h"
#include "IntrusivePtr.h"
#include "Util.h"

namespace Halide {

#if __cplusplus > 199711L // C++11 strongly typed enum
enum class DeviceAPI {
    Parent, /// Used to denote for loops that inherit their device from where they are used, generally the default
    Host,
    Default_GPU,
    CUDA,
    OpenCL,
    GLSL
};
#else
struct DeviceAPI {
    enum Values {
        Parent, /// Used to denote for loops that inherit their device from where they are used, generally the default
        Host,
        Default_GPU,
        CUDA,
        OpenCL,
        GLSL
    };
    int val;
    DeviceAPI() : val(Parent) { }
    DeviceAPI(int val) : val(val) { }
    operator int() const { return val; }
};
#endif

inline const char *device_api_to_string(const DeviceAPI &device_api) {
    switch (device_api) {
    case DeviceAPI::Host:
        return "";
        break;
    case DeviceAPI::Parent:
        return "<Parent>";
        break;
    case DeviceAPI::Default_GPU:
        return "<Default_GPU>";
        break;
    case DeviceAPI::CUDA:
        return "<CUDA>";
        break;
    case DeviceAPI::OpenCL:
        return "<OpenCL>";
        break;
    case DeviceAPI::GLSL:
        return "<GLSL>";
        break;
    }
    return "<Unrecognized DeviceAPI>";
}

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

}  // namespace Internal

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

// The Stmt and For structs don't really belong here, but putting them here
// greatly simplifies the include paths for Function.h
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
    DeviceAPI device_api;
    Stmt body;

    EXPORT static Stmt make(std::string name, Expr min, Expr extent, ForType for_type, DeviceAPI device_api, Stmt body);
};

}  // namespace Internal
}  // namespace Halide

#endif
