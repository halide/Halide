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
#include "IntrusivePtr.h"
#include "Type.h"
#include "Util.h"

namespace Halide {
namespace Internal {

class IRMutator;
class IRVisitor;

/** All our IR node types get unique IDs for the purposes of RTTI */
enum class IRNodeType {
    // Exprs, in order of strength
    IntImm,
    UIntImm,
    FloatImm,
    StringImm,
    Broadcast,
    Cast,
    Variable,
    Add,
    Sub,
    Mod,
    Mul,
    Div,
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
    Call,
    Let,
    Shuffle,
    // Stmts
    LetStmt,
    AssertStmt,
    ProducerConsumer,
    For,
    Acquire,
    Store,
    Provide,
    Allocate,
    Free,
    Realize,
    Block,
    Fork,
    IfThenElse,
    Evaluate,
    Prefetch,
    Atomic
};

/** The abstract base classes for a node in the Halide IR. */
struct IRNode {

    /** We use the visitor pattern to traverse IR nodes throughout the
     * compiler, so we have a virtual accept method which accepts
     * visitors.
     */
    virtual void accept(IRVisitor *v) const = 0;
    IRNode(IRNodeType t)
        : node_type(t) {
    }
    virtual ~IRNode() = default;

    /** These classes are all managed with intrusive reference
     * counting, so we also track a reference count. It's mutable
     * so that we can do reference counting even through const
     * references to IR nodes.
     */
    mutable RefCount ref_count;

    /** Each IR node subclass has a unique identifier. We can compare
     * these values to do runtime type identification. We don't
     * compile with rtti because that injects run-time type
     * identification stuff everywhere (and often breaks when linking
     * external libraries compiled without it), and we only want it
     * for IR nodes. One might want to put this value in the vtable,
     * but that adds another level of indirection, and for Exprs we
     * have 32 free bits in between the ref count and the Type
     * anyway, so this doesn't increase the memory footprint of an IR node.
     */
    IRNodeType node_type;
};

template<>
inline RefCount &ref_count<IRNode>(const IRNode *t) noexcept {
    return t->ref_count;
}

template<>
inline void destroy<IRNode>(const IRNode *t) {
    delete t;
}

/** IR nodes are split into expressions and statements. These are
   similar to expressions and statements in C - expressions
   represent some value and have some type (e.g. x + 3), and
   statements are side-effecting pieces of code that do not
   represent a value (e.g. assert(x > 3)) */

/** A base class for statement nodes. They have no properties or
   methods beyond base IR nodes for now. */
struct BaseStmtNode : public IRNode {
    BaseStmtNode(IRNodeType t)
        : IRNode(t) {
    }
    virtual Stmt mutate_stmt(IRMutator *v) const = 0;
};

/** A base class for expression nodes. They all contain their types
 * (e.g. Int(32), Float(32)) */
struct BaseExprNode : public IRNode {
    BaseExprNode(IRNodeType t)
        : IRNode(t) {
    }
    virtual Expr mutate_expr(IRMutator *v) const = 0;
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
    void accept(IRVisitor *v) const override;
    Expr mutate_expr(IRMutator *v) const override;
    ExprNode()
        : BaseExprNode(T::_node_type) {
    }
    virtual ~ExprNode() = default;
};

template<typename T>
struct StmtNode : public BaseStmtNode {
    void accept(IRVisitor *v) const override;
    Stmt mutate_stmt(IRMutator *v) const override;
    StmtNode()
        : BaseStmtNode(T::_node_type) {
    }
    virtual ~StmtNode() = default;
};

/** IR nodes are passed around opaque handles to them. This is a
   base class for those handles. It manages the reference count,
   and dispatches visitors. */
struct IRHandle : public IntrusivePtr<const IRNode> {
    HALIDE_ALWAYS_INLINE
    IRHandle() = default;

    HALIDE_ALWAYS_INLINE
    IRHandle(const IRNode *p)
        : IntrusivePtr<const IRNode>(p) {
    }

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
    template<typename T>
    const T *as() const {
        if (ptr && ptr->node_type == T::_node_type) {
            return (const T *)ptr;
        }
        return nullptr;
    }

    IRNodeType node_type() const {
        return ptr->node_type;
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

        // Normalize the value by dropping the high bits.
        // Since left-shift of negative value is UB in C++, cast to uint64 first;
        // it's unlikely any compilers we care about will misbehave, but UBSan will complain.
        value = (int64_t)(((uint64_t)value) << (64 - t.bits()));

        // Then sign-extending to get them back
        value >>= (64 - t.bits());

        IntImm *node = new IntImm;
        node->type = t;
        node->value = value;
        return node;
    }

    static const IRNodeType _node_type = IRNodeType::IntImm;
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

    static const IRNodeType _node_type = IRNodeType::UIntImm;
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
            if (t.is_bfloat()) {
                node->value = (double)((bfloat16_t)value);
            } else {
                node->value = (double)((float16_t)value);
            }
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

    static const IRNodeType _node_type = IRNodeType::FloatImm;
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

    static const IRNodeType _node_type = IRNodeType::StringImm;
};

}  // namespace Internal

/** A fragment of Halide syntax. It's implemented as reference-counted
 * handle to a concrete expression node, but it's immutable, so you
 * can treat it as a value type. */
struct Expr : public Internal::IRHandle {
    /** Make an undefined expression */
    HALIDE_ALWAYS_INLINE
    Expr() = default;

    /** Make an expression from a concrete expression node pointer (e.g. Add) */
    HALIDE_ALWAYS_INLINE
    Expr(const Internal::BaseExprNode *n)
        : IRHandle(n) {
    }

    /** Make an expression representing numeric constants of various types. */
    // @{
    explicit Expr(int8_t x)
        : IRHandle(Internal::IntImm::make(Int(8), x)) {
    }
    explicit Expr(int16_t x)
        : IRHandle(Internal::IntImm::make(Int(16), x)) {
    }
    Expr(int32_t x)
        : IRHandle(Internal::IntImm::make(Int(32), x)) {
    }
    explicit Expr(int64_t x)
        : IRHandle(Internal::IntImm::make(Int(64), x)) {
    }
    explicit Expr(uint8_t x)
        : IRHandle(Internal::UIntImm::make(UInt(8), x)) {
    }
    explicit Expr(uint16_t x)
        : IRHandle(Internal::UIntImm::make(UInt(16), x)) {
    }
    explicit Expr(uint32_t x)
        : IRHandle(Internal::UIntImm::make(UInt(32), x)) {
    }
    explicit Expr(uint64_t x)
        : IRHandle(Internal::UIntImm::make(UInt(64), x)) {
    }
    Expr(float16_t x)
        : IRHandle(Internal::FloatImm::make(Float(16), (double)x)) {
    }
    Expr(bfloat16_t x)
        : IRHandle(Internal::FloatImm::make(BFloat(16), (double)x)) {
    }
    Expr(float x)
        : IRHandle(Internal::FloatImm::make(Float(32), x)) {
    }
    explicit Expr(double x)
        : IRHandle(Internal::FloatImm::make(Float(64), x)) {
    }
    // @}

    /** Make an expression representing a const string (i.e. a StringImm) */
    Expr(const std::string &s)
        : IRHandle(Internal::StringImm::make(s)) {
    }

    /** Override get() to return a BaseExprNode * instead of an IRNode * */
    HALIDE_ALWAYS_INLINE
    const Internal::BaseExprNode *get() const {
        return (const Internal::BaseExprNode *)ptr;
    }

    /** Get the type of this expression node */
    HALIDE_ALWAYS_INLINE
    Type type() const {
        return get()->type;
    }
};

/** This lets you use an Expr as a key in a map of the form
 * map<Expr, Foo, ExprCompare> */
struct ExprCompare {
    bool operator()(const Expr &a, const Expr &b) const {
        return a.get() < b.get();
    }
};

/** A single-dimensional span. Includes all numbers between min and
 * (min + extent - 1). */
struct Range {
    Expr min, extent;

    Range() = default;
    Range(const Expr &min_in, const Expr &extent_in);
};

/** A multi-dimensional box. The outer product of the elements */
typedef std::vector<Range> Region;

/** An enum describing a type of device API. Used by schedules, and in
 * the For loop IR node. */
enum class DeviceAPI {
    None,  /// Used to denote for loops that run on the same device as the containing code.
    Host,
    Default_GPU,
    CUDA,
    OpenCL,
    GLSL,
    OpenGLCompute,
    Metal,
    Hexagon,
    HexagonDma,
    D3D12Compute,
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
                                     DeviceAPI::Hexagon,
                                     DeviceAPI::HexagonDma,
                                     DeviceAPI::D3D12Compute};

/** An enum describing different address spaces to be used with Func::store_in. */
enum class MemoryType {
    /** Let Halide select a storage type automatically */
    Auto,

    /** Heap/global memory. Allocated using halide_malloc, or
     * halide_device_malloc */
    Heap,

    /** Stack memory. Allocated using alloca. Requires a constant
     * size. Corresponds to per-thread local memory on the GPU. If all
     * accesses are at constant coordinates, may be promoted into the
     * register file at the discretion of the register allocator. */
    Stack,

    /** Register memory. The allocation should be promoted into the
     * register file. All stores must be at constant coordinates. May
     * be spilled to the stack at the discretion of the register
     * allocator. */
    Register,

    /** Allocation is stored in GPU shared memory. Also known as
     * "local" in OpenCL, and "threadgroup" in metal. Can be shared
     * across GPU threads within the same block. */
    GPUShared,

    /** Allocate Locked Cache Memory to act as local memory */
    LockedCache,
    /** Vector Tightly Coupled Memory. HVX (Hexagon) local memory available on
     * v65+. This memory has higher performance and lower power. Ideal for
     * intermediate buffers. Necessary for vgather-vscatter instructions
     * on Hexagon */
    VTCM,
};

namespace Internal {

/** An enum describing a type of loop traversal. Used in schedules,
 * and in the For loop IR node. Serial is a conventional ordered for
 * loop. Iterations occur in increasing order, and each iteration must
 * appear to have finished before the next begins. Parallel, GPUBlock,
 * and GPUThread are parallel and unordered: iterations may occur in
 * any order, and multiple iterations may occur
 * simultaneously. Vectorized and GPULane are parallel and
 * synchronous: they act as if all iterations occur at the same time
 * in lockstep. */
enum class ForType {
    Serial,
    Parallel,
    Vectorized,
    Unrolled,
    Extern,
    GPUBlock,
    GPUThread,
    GPULane,
};

/** Check if for_type executes for loop iterations in parallel and unordered. */
inline bool is_unordered_parallel(ForType for_type) {
    return (for_type == ForType::Parallel ||
            for_type == ForType::GPUBlock ||
            for_type == ForType::GPUThread);
}

/** Returns true if for_type executes for loop iterations in parallel. */
inline bool is_parallel(ForType for_type) {
    return (is_unordered_parallel(for_type) ||
            for_type == ForType::Vectorized ||
            for_type == ForType::GPULane);
}

/** A reference-counted handle to a statement node. */
struct Stmt : public IRHandle {
    Stmt() = default;
    Stmt(const BaseStmtNode *n)
        : IRHandle(n) {
    }

    /** Override get() to return a BaseStmtNode * instead of an IRNode * */
    HALIDE_ALWAYS_INLINE
    const BaseStmtNode *get() const {
        return (const Internal::BaseStmtNode *)ptr;
    }

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
