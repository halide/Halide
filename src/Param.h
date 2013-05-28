#ifndef HALIDE_PARAM_H
#define HALIDE_PARAM_H

/** \file
 * 
 * Classes for declaring scalar and image parameters to halide pipelines
 */

#include "IR.h"
#include "Var.h"
#include <sstream>
#include <vector>

namespace Halide {

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
    Param() : param(type_of<T>(), false) {}

    /** Construct a scalar parameter of type T with the given name */
    Param(const std::string &n) : param(type_of<T>(), false, n) {}

    /** Get the name of this parameter */
    const std::string &name() const {
        return param.name();
    }

    /** Get the current value of this parameter. Only meaningful when jitting. */
    T get() const {
        return param.get_scalar<T>();
    }

    /** Set the current value of this parameter. Only meaningful when jitting */
    void set(T val) {
        param.set_scalar<T>(val);
    }

    /** Get the halide type of T */
    Type type() const {
        return type_of<T>();
    }

    /** You can use this parameter as an expression in a halide
     * function definition */
    operator Expr() const {
        return Internal::Variable::make(type_of<T>(), name(), param);
    }

    /** Construct the appropriate argument matching this parameter,
     * for the purpose of generating the right type signature when
     * statically compiling halide pipelines. */
    operator Argument() const {
        return Argument(name(), false, type());
    }
};

/** An Image parameter to a halide pipeline. E.g., the input image. */
class ImageParam {
    /** A reference-counted handle on the internal parameter object */
    Internal::Parameter param;

    /** The dimensionality of this image. We need to know in order to
     * inject the right number of implicit vars when calling
     * operator() */
    int dims;
public:

    /** Construct a NULL image parameter handle. */
    ImageParam() : dims(0) {}

    /** Construct an image parameter of the given type and
     * dimensionality, with an auto-generated unique name. */
    ImageParam(Type t, int d) : param(t, true), dims(d) {}

    /** Construct an image parameter of the given type and
     * dimensionality, with the given name */
    ImageParam(Type t, int d, const std::string &n) : param(t, true, n), dims(d) {}

    /** Get the name of this ImageParam */
    const std::string &name() const {
        return param.name();
    }

    /** Get the type of the image data this ImageParam refers to */
    Type type() const {
        return param.type();
    }

    /** Bind a buffer or image to this ImageParam. Only relevant for jitting */
    void set(Buffer b) {
        if (b.defined()) assert(b.type() == type() && "Setting buffer of incorrect type");
        param.set_buffer(b);
    }
    
    /** Get the buffer bound to this ImageParam. Only relevant for jitting */
    Buffer get() const {
        return param.get_buffer();
    }

    /** Is this handle non-NULL */
    bool defined() const {
        return param.defined();
    }

    /** Get an expression representing the extent of this image
     * parameter in the given dimension */
    Expr extent(int x) const {
        std::ostringstream s;
        s << name() << ".extent." << x;
        return Internal::Variable::make(Int(32), s.str(), param);
    }

    /** Get an expression representing the stride of this image in the
     * given dimension */
    Expr stride(int x) const {
        std::ostringstream s;
        s << name() << ".stride." << x;
        return Internal::Variable::make(Int(32), s.str(), param);        
    }

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
    ImageParam &set_extent(int dim, Expr extent) {
        param.set_extent_constraint(dim, extent);
        return *this;
    }

    /** Set the min in a given dimension to equal the given
     * expression. Setting the mins to zero may simplify some
     * addressing math. */
    ImageParam &set_min(int dim, Expr min) {
        param.set_min_constraint(dim, min);
        return *this;
    }

    /** Set the stride in a given dimension to equal the given
     * value. This is particularly helpful to set when
     * vectorizing. Known strides for the vectorized dimension
     * generate better code. */
    ImageParam &set_stride(int dim, Expr stride) {
        param.set_stride_constraint(dim, stride);
        return *this;
    }

    /** Set the min and extent in one call. */
    ImageParam &set_bounds(int dim, Expr min, Expr extent) {
        return set_min(dim, min).set_extent(dim, extent);
    }

    /** Get the dimensionality of this image parameter */
    int dimensions() const {
        return dims;
    };

    /** Get an expression giving the extent in dimension 0, which by
     * convention is the width of the image */
    Expr width() const {
        assert(dims >= 0);
        return extent(0);
    }

    /** Get an expression giving the extent in dimension 1, which by
     * convention is the height of the image */
    Expr height() const {
        assert(dims >= 1);
        return extent(1);
    }
    
    /** Get an expression giving the extent in dimension 2, which by
     * convention is the channel-count of the image */
    Expr channels() const {
        assert(dims >= 2);
        return extent(2);
    }

    /** Construct an expression which loads from this image
     * parameter. The location is extended with enough implicit
     * variables to match the dimensionality of the image
     * (see \ref Var::implicit) 
     */
    // @{
    Expr operator()() const {
        assert(dimensions() >= 0);
        std::vector<Expr> args;
        for (int i = 0; args.size() < (size_t)dimensions(); i++) {
            args.push_back(Var::implicit(i));
        }
        return Internal::Call::make(param, args);
    }

    Expr operator()(Expr x) const {
        assert(dimensions() >= 1);
        std::vector<Expr> args;
        args.push_back(x);
        for (int i = 0; args.size() < (size_t)dimensions(); i++) {
            args.push_back(Var::implicit(i));
        }
        return Internal::Call::make(param, args);
    }

    Expr operator()(Expr x, Expr y) const {
        assert(dimensions() >= 2);
        std::vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        for (int i = 0; args.size() < (size_t)dimensions(); i++) {
            args.push_back(Var::implicit(i));
        }
        return Internal::Call::make(param, args);
    }

    Expr operator()(Expr x, Expr y, Expr z) const {
        assert(dimensions() >= 3);
        std::vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        args.push_back(z);
        for (int i = 0; args.size() < (size_t)dimensions(); i++) {
            args.push_back(Var::implicit(i));
        }
        return Internal::Call::make(param, args);
    }

    Expr operator()(Expr x, Expr y, Expr z, Expr w) const {
        assert(dimensions() >= 4);
        std::vector<Expr> args;
        args.push_back(x);
        args.push_back(y);
        args.push_back(z);
        args.push_back(w);
        for (int i = 0; args.size() < (size_t)dimensions(); i++) {
            args.push_back(Var::implicit(i));
        }
        return Internal::Call::make(param, args);
    }
    // @}

    /** Construct the appropriate argument matching this parameter,
     * for the purpose of generating the right type signature when
     * statically compiling halide pipelines. */
    operator Argument() const {
        return Argument(name(), true, type());
    }   

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
        return (*this)();
    }
};

}

#endif
