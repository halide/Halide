#ifndef HALIDE_IMAGE_PARAM_H
#define HALIDE_IMAGE_PARAM_H

/** \file
 *
 * Classes for declaring image parameters to halide pipelines
 */

#include "Var.h"
#include "OutputImageParam.h"
#include "Func.h"

namespace Halide {

/** An Image parameter to a halide pipeline. E.g., the input image. */
class ImageParam : public OutputImageParam {

    /** Func representation of the ImageParam */
    Func func;

    // Helper function to initialize the Func representation of this ImageParam
    EXPORT void init_func();

public:

    /** Construct a nullptr image parameter handle. */
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

    operator Func() const;

    EXPORT Func in(const Func &f);
    EXPORT Func in(const std::vector<Func> &fs);
    EXPORT Func in();
};

}

#endif
