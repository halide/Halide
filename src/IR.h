#ifndef HALIDE_IR_H
#define HALIDE_IR_H

/** \file
 * Subtypes for Halide expressions (\ref Halide::Expr) and statements (\ref Halide::Internal::Stmt)
 */

#include <string>
#include <vector>

#include "Buffer.h"
#include "Debug.h"
#include "Error.h"
#include "Expr.h"
#include "Function.h"
#include "IntrusivePtr.h"
#include "Parameter.h"
#include "Type.h"
#include "Util.h"

namespace Halide {
namespace Internal {

/** The actual IR nodes begin here. Remember that all the Expr
 * nodes also have a public "type" property */

/** Cast a node from one type to another. Can't change vector widths. */
struct Cast : public ExprNode<Cast> {
    Expr value;

    EXPORT static Expr make(Type t, Expr v);
};

/** The sum of two expressions */
struct Add : public ExprNode<Add> {
    Expr a, b;

    EXPORT static Expr make(Expr a, Expr b);
};

/** The difference of two expressions */
struct Sub : public ExprNode<Sub> {
    Expr a, b;

    EXPORT static Expr make(Expr a, Expr b);
};

/** The product of two expressions */
struct Mul : public ExprNode<Mul> {
    Expr a, b;

    EXPORT static Expr make(Expr a, Expr b);
};

/** The ratio of two expressions */
struct Div : public ExprNode<Div> {
    Expr a, b;

    EXPORT static Expr make(Expr a, Expr b);
};

/** The remainder of a / b. Mostly equivalent to '%' in C, except that
 * the result here is always positive. For floats, this is equivalent
 * to calling fmod. */
struct Mod : public ExprNode<Mod> {
    Expr a, b;

    EXPORT static Expr make(Expr a, Expr b);
};

/** The lesser of two values. */
struct Min : public ExprNode<Min> {
    Expr a, b;

    EXPORT static Expr make(Expr a, Expr b);
};

/** The greater of two values */
struct Max : public ExprNode<Max> {
    Expr a, b;

    EXPORT static Expr make(Expr a, Expr b);
};

/** Is the first expression equal to the second */
struct EQ : public ExprNode<EQ> {
    Expr a, b;

    EXPORT static Expr make(Expr a, Expr b);
};

/** Is the first expression not equal to the second */
struct NE : public ExprNode<NE> {
    Expr a, b;

    EXPORT static Expr make(Expr a, Expr b);
};

/** Is the first expression less than the second. */
struct LT : public ExprNode<LT> {
    Expr a, b;

    EXPORT static Expr make(Expr a, Expr b);
};

/** Is the first expression less than or equal to the second. */
struct LE : public ExprNode<LE> {
    Expr a, b;

    EXPORT static Expr make(Expr a, Expr b);
};

/** Is the first expression greater than the second. */
struct GT : public ExprNode<GT> {
    Expr a, b;

    EXPORT static Expr make(Expr a, Expr b);
};

/** Is the first expression greater than or equal to the second. */
struct GE : public ExprNode<GE> {
    Expr a, b;

    EXPORT static Expr make(Expr a, Expr b);
};

/** Logical and - are both expressions true */
struct And : public ExprNode<And> {
    Expr a, b;

    EXPORT static Expr make(Expr a, Expr b);
};

/** Logical or - is at least one of the expression true */
struct Or : public ExprNode<Or> {
    Expr a, b;

    EXPORT static Expr make(Expr a, Expr b);
};

/** Logical not - true if the expression false */
struct Not : public ExprNode<Not> {
    Expr a;

    EXPORT static Expr make(Expr a);
};

/** A ternary operator. Evalutes 'true_value' and 'false_value',
 * then selects between them based on 'condition'. Equivalent to
 * the ternary operator in C. */
struct Select : public ExprNode<Select> {
    Expr condition, true_value, false_value;

    EXPORT static Expr make(Expr condition, Expr true_value, Expr false_value);
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

    EXPORT static Expr make(Type type, std::string name, Expr index, Buffer image, Parameter param);
};

/** A linear ramp vector node. This is vector with 'lanes' elements,
 * where element i is 'base' + i*'stride'. This is a convenient way to
 * pass around vectors without busting them up into individual
 * elements. E.g. a dense vector load from a buffer can use a ramp
 * node with stride 1 as the index. */
struct Ramp : public ExprNode<Ramp> {
    Expr base, stride;
    int lanes;

    EXPORT static Expr make(Expr base, Expr stride, int lanes);
};

/** A vector with 'lanes' elements, in which every element is
 * 'value'. This is a special case of the ramp node above, in which
 * the stride is zero. */
struct Broadcast : public ExprNode<Broadcast> {
    Expr value;
    int lanes;

    EXPORT static Expr make(Expr value, int lanes);
};

/** A let expression, like you might find in a functional
 * language. Within the expression \ref Let::body, instances of the Var
 * node \ref Let::name refer to \ref Let::value. */
struct Let : public ExprNode<Let> {
    std::string name;
    Expr value, body;

    EXPORT static Expr make(std::string name, Expr value, Expr body);
};

/** The statement form of a let node. Within the statement 'body',
 * instances of the Var named 'name' refer to 'value' */
struct LetStmt : public StmtNode<LetStmt> {
    std::string name;
    Expr value;
    Stmt body;

    EXPORT static Stmt make(std::string name, Expr value, Stmt body);
};

/** If the 'condition' is false, then evaluate and return the message,
 * which should be a call to an error function. */
struct AssertStmt : public StmtNode<AssertStmt> {
    // if condition then val else error out with message
    Expr condition;
    Expr message;

    EXPORT static Stmt make(Expr condition, Expr message);
};

/** This node is a helpful annotation to do with permissions. The
 * three child statements happen in order. In the 'produce'
 * statement 'buffer' is write-only. In 'update' it is
 * read-write. In 'consume' it is read-only. The 'update' node is
 * often undefined. (check update.defined() to find out). None of this
 * is actually enforced, the node is purely for informative
 * purposes to help out our analysis during lowering. */
struct ProducerConsumer : public StmtNode<ProducerConsumer> {
    std::string name;
    Stmt produce, update, consume;

    EXPORT static Stmt make(std::string name, Stmt produce, Stmt update, Stmt consume);
};

/** Store a 'value' to the buffer called 'name' at a given
 * 'index'. The buffer is interpreted as an array of the same type as
 * 'value'. */
struct Store : public StmtNode<Store> {
    std::string name;
    Expr value, index;
    // If it's a store to an output buffer, then this parameter points to it.
    Parameter param;

    EXPORT static Stmt make(std::string name, Expr value, Expr index, Parameter param);
};

/** This defines the value of a function at a multi-dimensional
 * location. You should think of it as a store to a
 * multi-dimensional array. It gets lowered to a conventional
 * Store node. */
struct Provide : public StmtNode<Provide> {
    std::string name;
    std::vector<Expr> values;
    std::vector<Expr> args;

    EXPORT static Stmt make(std::string name, const std::vector<Expr> &values, const std::vector<Expr> &args);
};

/** Allocate a scratch area called with the given name, type, and
 * size. The buffer lives for at most the duration of the body
 * statement, within which it is freed. It is an error for an allocate
 * node not to contain a free node of the same buffer. Allocation only
 * occurs if the condition evaluates to true. */
struct Allocate : public StmtNode<Allocate> {
    std::string name;
    Type type;
    std::vector<Expr> extents;
    Expr condition;

    // These override the code generator dependent malloc and free
    // equivalents if provided. If the new_expr succeeds, that is it
    // returns non-nullptr, the function named be free_function is
    // guaranteed to be called. The free function signature must match
    // that of the code generator dependent free (typically
    // halide_free). If free_function is left empty, code generator
    // default will be called.
    Expr new_expr;
    std::string free_function;
    Stmt body;

    EXPORT static Stmt make(std::string name, Type type, const std::vector<Expr> &extents,
                            Expr condition, Stmt body,
                            Expr new_expr = Expr(), std::string free_function = std::string());

    /** A routine to check if the extents are all constants, and if so verify
     * the total size is less than 2^31 - 1. If the result is constant, but
     * overflows, this routine asserts. This returns 0 if the extents are
     * not all constants; otherwise, it returns the total constant allocation
     * size. */
    EXPORT static int32_t constant_allocation_size(const std::vector<Expr> &extents, const std::string &name);
    EXPORT int32_t constant_allocation_size() const;
};

/** Free the resources associated with the given buffer. */
struct Free : public StmtNode<Free> {
    std::string name;

    EXPORT static Stmt make(std::string name);
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
 * (min, extent) pairs for each dimension. Allocation only occurs if
 * the condition evaluates to true. */
struct Realize : public StmtNode<Realize> {
    std::string name;
    std::vector<Type> types;
    Region bounds;
    Expr condition;
    Stmt body;

    EXPORT static Stmt make(const std::string &name, const std::vector<Type> &types, const Region &bounds, Expr condition, Stmt body);
};

/** A sequence of statements to be executed in-order. 'rest' may be
 * undefined. Used rest.defined() to find out. */
struct Block : public StmtNode<Block> {
    Stmt first, rest;

    EXPORT static Stmt make(Stmt first, Stmt rest);
    EXPORT static Stmt make(const std::vector<Stmt> &stmts);
};

/** An if-then-else block. 'else' may be undefined. */
struct IfThenElse : public StmtNode<IfThenElse> {
    Expr condition;
    Stmt then_case, else_case;

    EXPORT static Stmt make(Expr condition, Stmt then_case, Stmt else_case = Stmt());
};

/** Evaluate and discard an expression, presumably because it has some side-effect. */
struct Evaluate : public StmtNode<Evaluate> {
    Expr value;

    EXPORT static Stmt make(Expr v);
};

/** A function call. This can represent a call to some extern function
 * (like sin), but it's also our multi-dimensional version of a Load,
 * so it can be a load from an input image, or a call to another
 * halide function. These two types of call nodes don't survive all
 * the way down to code generation - the lowering process converts
 * them to Load nodes. */
struct Call : public ExprNode<Call> {
    std::string name;
    std::vector<Expr> args;
    typedef enum {Image,        //< A load from an input image
                  Extern,       //< A call to an external C-ABI function, possibly with side-effects
                  ExternCPlusPlus, //< A call to an external C-ABI function, possibly with side-effects
                  PureExtern,   //< A call to a guaranteed-side-effect-free external function
                  Halide,       //< A call to a Func
                  Intrinsic,    //< A possibly-side-effecty compiler intrinsic, which has special handling during codegen
                  PureIntrinsic //< A side-effect-free version of the above.
    } CallType;
    CallType call_type;

    // Halide uses calls internally to represent certain operations
    // (instead of IR nodes). These are matched by name. Note that
    // these are deliberately char* (rather than std::string) so that
    // they can be referenced at static-initialization time without
    // risking ambiguous initalization order; we use a typedef to simplify
    // declaration.
    typedef const char* const ConstString;
    EXPORT static ConstString debug_to_file,
        shuffle_vector,
        interleave_vectors,
        concat_vectors,
        reinterpret,
        bitwise_and,
        bitwise_not,
        bitwise_xor,
        bitwise_or,
        shift_left,
        shift_right,
        abs,
        absd,
        rewrite_buffer,
        random,
        lerp,
        create_buffer_t,
        copy_buffer_t,
        extract_buffer_min,
        extract_buffer_max,
        extract_buffer_host,
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
        glsl_varying,
        image_load,
        image_store,
        make_struct,
        stringify,
        memoize_expr,
        copy_memory,
        likely,
        likely_if_innermost,
        register_destructor,
        div_round_to_zero,
        mod_round_to_zero,
        slice_vector,
        call_cached_indirect_function,
        signed_integer_overflow;

    // If it's a call to another halide function, this call node holds
    // onto a pointer to that function for the purposes of reference
    // counting only. Self-references in update definitions do not
    // have this set, to avoid cycles.
    IntrusivePtr<FunctionContents> func;

    // If that function has multiple values, which value does this
    // call node refer to?
    int value_index;

    // If it's a call to an image, this call nodes hold a
    // pointer to that image's buffer
    Buffer image;

    // If it's a call to an image parameter, this call node holds a
    // pointer to that
    Parameter param;

    EXPORT static Expr make(Type type, std::string name, const std::vector<Expr> &args, CallType call_type,
                            IntrusivePtr<FunctionContents> func = nullptr, int value_index = 0,
                            Buffer image = Buffer(), Parameter param = Parameter());

    /** Convenience constructor for calls to other halide functions */
    static Expr make(Function func, const std::vector<Expr> &args, int idx = 0) {
        internal_assert(idx >= 0 &&
                        idx < func.outputs())
            << "Value index out of range in call to halide function\n";
        internal_assert(func.has_pure_definition() || func.has_extern_definition())
            << "Call to undefined halide function\n";
        return make(func.output_types()[(size_t)idx], func.name(), args, Halide, func.get_contents(), idx, Buffer(), Parameter());
    }

    /** Convenience constructor for loads from concrete images */
    static Expr make(Buffer image, const std::vector<Expr> &args) {
        return make(image.type(), image.name(), args, Image, nullptr, 0, image, Parameter());
    }

    /** Convenience constructor for loads from images parameters */
    static Expr make(Parameter param, const std::vector<Expr> &args) {
        return make(param.type(), param.name(), args, Image, nullptr, 0, Buffer(), param);
    }

    /** Check if a call node is pure within a pipeline, meaning that
     * the same args always give the same result, and the calls can be
     * reordered, duplicated, unified, etc without changing the
     * meaning of anything. Not transitive - doesn't guarantee the
     * args themselves are pure. An example of a pure Call node is
     * sqrt. If in doubt, don't mark a Call node as pure. */
    bool is_pure() const {
        return (call_type == PureExtern ||
                call_type == Image ||
                call_type == PureIntrinsic);
    }

    bool is_intrinsic(ConstString intrin_name) const {
        return
            ((call_type == Intrinsic ||
              call_type == PureIntrinsic) &&
             name == intrin_name);
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

    EXPORT static Expr make(Type type, std::string name, Buffer image, Parameter param, ReductionDomain reduction_domain);
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
    ForType for_type;
    DeviceAPI device_api;
    Stmt body;

    EXPORT static Stmt make(std::string name, Expr min, Expr extent, ForType for_type, DeviceAPI device_api, Stmt body);
};

}
}

#endif
