#ifndef HALIDE_EXPR_H
#define HALIDE_EXPR_H

/** \file
 * Base classes for Halide expressions (\ref Halide::Expr) and statements (\ref Halide::Internal::Stmt)
 */

#include <string>
#include <vector>

#include "IntrusivePtr.h"
#include "Type.h"

namespace Halide {

struct bfloat16_t;
struct float16_t;

namespace Internal {

class IRMutator;
class IRVisitor;

/** All our IR node types get unique IDs for the purposes of RTTI */
enum class IRNodeType {
    // Exprs, in order of strength. Code in IRMatch.h and the
    // simplifier relies on this order for canonicalization of
    // expressions, so you may need to update those modules if you
    // change this list.
    IntImm,
    UIntImm,
    FloatImm,
    StringImm,
    Broadcast,
    Cast,
    Reinterpret,
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
    VectorReduce,
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
    Atomic,
    HoistedStorage
};

constexpr IRNodeType StrongestExprNodeType = IRNodeType::VectorReduce;

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
    ~ExprNode() override = default;
};

template<typename T>
struct StmtNode : public BaseStmtNode {
    void accept(IRVisitor *v) const override;
    Stmt mutate_stmt(IRMutator *v) const override;
    StmtNode()
        : BaseStmtNode(T::_node_type) {
    }
    ~StmtNode() override = default;
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

    static const IntImm *make(Type t, int64_t value);

    static const IRNodeType _node_type = IRNodeType::IntImm;
};

/** Unsigned integer constants */
struct UIntImm : public ExprNode<UIntImm> {
    uint64_t value;

    static const UIntImm *make(Type t, uint64_t value);

    static const IRNodeType _node_type = IRNodeType::UIntImm;
};

/** Floating point constants */
struct FloatImm : public ExprNode<FloatImm> {
    double value;

    static const FloatImm *make(Type t, double value);

    static const IRNodeType _node_type = IRNodeType::FloatImm;
};

/** String constants */
struct StringImm : public ExprNode<StringImm> {
    std::string value;

    static const StringImm *make(const std::string &val);

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

    /** Allocation is stored in GPU texture memory and accessed through
     * hardware sampler */
    GPUTexture,

    /** Allocate Locked Cache Memory to act as local memory */
    LockedCache,
    /** Vector Tightly Coupled Memory. HVX (Hexagon) local memory available on
     * v65+. This memory has higher performance and lower power. Ideal for
     * intermediate buffers. Necessary for vgather-vscatter instructions
     * on Hexagon */
    VTCM,

    /** AMX Tile register for X86. Any data that would be used in an AMX matrix
     * multiplication must first be loaded into an AMX tile register. */
    AMXTile,
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
bool is_unordered_parallel(ForType for_type);

/** Returns true if for_type executes for loop iterations in parallel. */
bool is_parallel(ForType for_type);

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
