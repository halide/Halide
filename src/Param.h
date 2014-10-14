#ifndef HALIDE_PARAM_H
#define HALIDE_PARAM_H

/** \file
 *
 * Classes for declaring scalar and image parameters to halide pipelines
 */

#include <sstream>
#include <vector>

#include "IR.h"
#include "Var.h"
#include "Util.h"

namespace Halide {

/** A struct used to detect if a type is a pointer. If it's not a
 * pointer, then not_a_pointer<T>::type is T.  If it is a pointer,
 * then not_a_pointer<T>::type is some internal hidden type that no
 * overload should trigger on. TODO: with C++11 this can be written 
 * more cleanly. */
namespace Internal {
template<typename T> struct not_a_pointer {typedef T type;};
template<typename T> struct not_a_pointer<T *> { struct type {}; };
}

/** A scalar parameter to a halide pipeline. If you're jitting, this
 * should be bound to an actual value of type T using the set method
 * before you realize the function uses this. If you're statically
 * compiling, this param should appear in the argument list. */
template<typename T>
class Param {
    /** A reference-counted handle on the internal parameter object */
    Internal::Parameter param;
    
public:
    /** Construct a scalar parameter of type T with a unique
     * auto-generated name */
    Param() : param(type_of<T>(), false, Internal::make_entity_name(this, "Halide::Param<?", 'p')) {}
    
    /** Construct a scalar parameter of type T with the given name. */
    explicit Param(const std::string &n) : param(type_of<T>(), false, n) {}

    /** Construct a scalar parameter of type T an initial value of
     * 'val'. Only triggers for scalar types. */
    explicit Param(typename Internal::not_a_pointer<T>::type val) : param(type_of<T>(), false, Internal::make_entity_name(this, "Halide::Param<?", 'p')) {
        set(val);
    }

    /** Construct a scalar parameter of type T with the given name
     * and an initial value of 'val'. */
    Param(T val, const std::string &n) : param(type_of<T>(), false, n) {
        set(val);
    }

    /** Construct a scalar parameter of type T with an initial value of 'val'
    * and a given min and max. */
    Param(T val, Expr min, Expr max) : param(type_of<T>(), false, Internal::make_entity_name(this, "Halide::Param<?", 'p')) {
        set_range(min, max);
        set(val);
    }

    /** Construct a scalar parameter of type T with the given name
     * and an initial value of 'val' and a given min and max. */
    Param(T val, Expr min, Expr max, const std::string &n) : param(type_of<T>(), false, n) {
        set_range(min, max);
        set(val);
    }

    /** Get the name of this parameter */
    const std::string &name() const {
        return param.name();
    }

    /** Get the current value of this parameter. Only meaningful when jitting. */
    NO_INLINE T get() const {
        return param.get_scalar<T>();
    }

    /** Set the current value of this parameter. Only meaningful when jitting */
    NO_INLINE void set(T val) {
        param.set_scalar<T>(val);
    }

    /** Get a pointer to the location that stores the current value of
     * this parameter. Only meaningful for jitting. */
    NO_INLINE T *get_address() const {
        return (T *)(param.get_scalar_address());
    }

    /** Get the halide type of T */
    Type type() const {
        return type_of<T>();
    }

    /** Get or set the possible range of this parameter. Use undefined
     * Exprs to mean unbounded. */
    // @{
    void set_range(Expr min, Expr max) {
        set_min_value(min);
        set_max_value(max);
    }

    void set_min_value(Expr min) {
        if (min.type() != type_of<T>()) {
            min = Internal::Cast::make(type_of<T>(), min);
        }
        param.set_min_value(min);
    }

    void set_max_value(Expr max) {
        if (max.type() != type_of<T>()) {
            max = Internal::Cast::make(type_of<T>(), max);
        }
        param.set_max_value(max);
    }

    Expr get_min_value() {
        return param.get_min_value();
    }

    Expr get_max_value() {
        return param.get_max_value();
    }
    // @}

    /** You can use this parameter as an expression in a halide
     * function definition */
    operator Expr() const {
        return Internal::Variable::make(type_of<T>(), name(), param);
    }

    /** Using a param as the argument to an external stage treats it
     * as an Expr */
    operator ExternFuncArgument() const {
        return Expr(*this);
    }

    /** Construct the appropriate argument matching this parameter,
     * for the purpose of generating the right type signature when
     * statically compiling halide pipelines. */
    operator Argument() const {
        return Argument(name(), false, type());
    }
};

/** Returns a Param corresponding to a pointer to a user context
 * structure; when the Halide function that takes such a parameter
 * calls a function from the Halide runtime (e.g. halide_printf()), it
 * passes the value of this pointer as the first argument to the
 * runtime function.  */
inline Param<void *> user_context_param() {
  return Param<void *>("__user_context");
}

/** A handle on the output buffer of a pipeline. Used to make static
 * promises about the output size and stride. */
class OutputImageParam {
protected:
    /** A reference-counted handle on the internal parameter object */
    Internal::Parameter param;

    /** The dimensionality of this image. */
    int dims;

    void add_implicit_args_if_placeholder(std::vector<Expr> &args,
                                          Expr last_arg,
                                          int total_args,
                                          bool *placeholder_seen) const;
public:

    /** Construct a NULL image parameter handle. */
    OutputImageParam() : dims(0) {}

    /** Construct an OutputImageParam that wraps an Internal Parameter object. */
    EXPORT OutputImageParam(const Internal::Parameter &p, int d);

    /** Get the name of this Param */
    EXPORT const std::string &name() const;

    /** Get the type of the image data this Param refers to */
    EXPORT Type type() const;

    /** Is this parameter handle non-NULL */
    EXPORT bool defined();

    /** Get an expression representing the minimum coordinates of this image
     * parameter in the given dimension. */
    EXPORT Expr min(int x) const;

    /** Get an expression representing the extent of this image
     * parameter in the given dimension */
    EXPORT Expr extent(int x) const;

    /** Get an expression representing the stride of this image in the
     * given dimension */
    EXPORT Expr stride(int x) const;

    /** Set the extent in a given dimension to equal the given
     * expression. Images passed in that fail this check will generate
     * a runtime error. Returns a reference to the ImageParam so that
     * these calls may be chained.
     *
     * This may help the compiler generate better
     * code. E.g:
     \code
     im.set_extent(0, 100);
     \endcode
     * tells the compiler that dimension zero must be of extent 100,
     * which may result in simplification of boundary checks. The
     * value can be an arbitrary expression:
     \code
     im.set_extent(0, im.extent(1));
     \endcode
     * declares that im is a square image (of unknown size), whereas:
     \code
     im.set_extent(0, (im.extent(0)/32)*32);
     \endcode
     * tells the compiler that the extent is a multiple of 32. */
    EXPORT OutputImageParam &set_extent(int dim, Expr extent);

    /** Set the min in a given dimension to equal the given
     * expression. Setting the mins to zero may simplify some
     * addressing math. */
    EXPORT OutputImageParam &set_min(int dim, Expr min);

    /** Set the stride in a given dimension to equal the given
     * value. This is particularly helpful to set when
     * vectorizing. Known strides for the vectorized dimension
     * generate better code. */
    EXPORT OutputImageParam &set_stride(int dim, Expr stride);

    /** Set the min and extent in one call. */
    EXPORT OutputImageParam &set_bounds(int dim, Expr min, Expr extent);

    /** Get the dimensionality of this image parameter */
    EXPORT int dimensions() const;

    /** Get an expression giving the minimum coordinate in dimension 0, which
     * by convention is the coordinate of the left edge of the image */
    EXPORT Expr left() const;

    /** Get an expression giving the maximum coordinate in dimension 0, which
     * by convention is the coordinate of the right edge of the image */
    EXPORT Expr right() const;

    /** Get an expression giving the minimum coordinate in dimension 1, which
     * by convention is the top of the image */
    EXPORT Expr top() const;

    /** Get an expression giving the maximum coordinate in dimension 1, which
     * by convention is the bottom of the image */
    EXPORT Expr bottom() const;

    /** Get an expression giving the extent in dimension 0, which by
     * convention is the width of the image */
    EXPORT Expr width() const;

    /** Get an expression giving the extent in dimension 1, which by
     * convention is the height of the image */
    EXPORT Expr height() const;

    /** Get an expression giving the extent in dimension 2, which by
     * convention is the channel-count of the image */
    EXPORT Expr channels() const;

    /** Get at the internal parameter object representing this ImageParam. */
    EXPORT Internal::Parameter parameter() const;

    /** Construct the appropriate argument matching this parameter,
     * for the purpose of generating the right type signature when
     * statically compiling halide pipelines. */
    EXPORT operator Argument() const;

    /** Using a param as the argument to an external stage treats it
     * as an Expr */
    EXPORT operator ExternFuncArgument() const;
};

/** An Image parameter to a halide pipeline. E.g., the input image. */
class ImageParam : public OutputImageParam {

public:

    /** Construct a NULL image parameter handle. */
    ImageParam() : OutputImageParam() {}

    /** Construct an image parameter of the given type and
     * dimensionality, with an auto-generated unique name. */
    EXPORT ImageParam(Type t, int d);

    /** Construct an image parameter of the given type and
     * dimensionality, with the given name */
    EXPORT ImageParam(Type t, int d, const std::string &n);

    /** Bind a buffer or image to this ImageParam. Only relevant for jitting */
    EXPORT void set(Buffer b);

    /** Get the buffer bound to this ImageParam. Only relevant for jitting */
    EXPORT Buffer get() const;

    /** Construct an expression which loads from this image
     * parameter. The location is extended with enough implicit
     * variables to match the dimensionality of the image
     * (see \ref Var::implicit)
     */
    // @{
    EXPORT Expr operator()() const;
    EXPORT Expr operator()(Expr x) const;
    EXPORT Expr operator()(Expr x, Expr y) const;
    EXPORT Expr operator()(Expr x, Expr y, Expr z) const;
    EXPORT Expr operator()(Expr x, Expr y, Expr z, Expr w) const;
    EXPORT Expr operator()(std::vector<Expr>) const;
    EXPORT Expr operator()(std::vector<Var>) const;
    // @}

    /** Treating the image parameter as an Expr is equivalent to call
     * it with no arguments. For example, you can say:
     *
     \code
     ImageParam im(UInt(8), 2);
     Func f;
     f = im*2;
     \endcode
     *
     * This will define f as a two-dimensional function with value at
     * position (x, y) equal to twice the value of the image parameter
     * at the same location.
     */
    operator Expr() const {
        return (*this)(_);
    }

};

}

#endif
