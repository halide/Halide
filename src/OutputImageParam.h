#ifndef HALIDE_OUTPUT_IMAGE_PARAM_H
#define HALIDE_OUTPUT_IMAGE_PARAM_H

/** \file
 *
 * Classes for declaring output image parameters to halide pipelines
 */

#include "Argument.h"
#include "runtime/HalideRuntime.h"
#include "Var.h"

namespace Halide {

/** A handle on the output buffer of a pipeline. Used to make static
 * promises about the output size and stride. */
class OutputImageParam {
protected:
    /** A reference-counted handle on the internal parameter object */
    Internal::Parameter param;

    /** Is this an input or an output? OutputImageParam is the base class for both. */
    Argument::Kind kind;

    void add_implicit_args_if_placeholder(std::vector<Expr> &args,
                                          Expr last_arg,
                                          int total_args,
                                          bool *placeholder_seen) const;
public:

    /** Construct a null image parameter handle. */
    OutputImageParam() {}

    /** Construct an OutputImageParam that wraps an Internal Parameter object. */
    EXPORT OutputImageParam(const Internal::Parameter &p, Argument::Kind k);

    /** Get the name of this Param */
    EXPORT const std::string &name() const;

    /** Get the type of the image data this Param refers to */
    EXPORT Type type() const;

    /** Is this parameter handle non-nullptr */
    EXPORT bool defined() const;

    /** Get a handle on one of the dimensions for the purposes of
     * inspecting or constraining its min, extent, or stride. */
    EXPORT Internal::Dimension dim(int i);

    /** Get a handle on one of the dimensions for the purposes of
     * inspecting its min, extent, or stride. */
    EXPORT const Internal::Dimension dim(int i) const;

    /** Get or constrain the shape of the dimensions. Soon to be
     * deprecated. Do not use. */
    // @{
    HALIDE_ATTRIBUTE_DEPRECATED("set_min() is deprecated. use dim(n).set_min() instead.") 
    OutputImageParam set_min(int i, Expr e) {dim(i).set_min(e); return *this;}
    HALIDE_ATTRIBUTE_DEPRECATED("set_extent() is deprecated. use dim(n).set_extent() instead.") 
    OutputImageParam set_extent(int i, Expr e) {dim(i).set_extent(e); return *this;}
    HALIDE_ATTRIBUTE_DEPRECATED("set_bounds() is deprecated. use dim(n).set_bounds() instead.") 
    OutputImageParam set_bounds(int i, Expr a, Expr b) {dim(i).set_bounds(a, b); return *this;}
    HALIDE_ATTRIBUTE_DEPRECATED("set_stride() is deprecated. use dim(n).set_stride() instead.") 
    OutputImageParam set_stride(int i, Expr e) {dim(i).set_stride(e); return *this;}
    HALIDE_ATTRIBUTE_DEPRECATED("min() is deprecated. use dim(n).min() instead.") 
    Expr min(int i) const {return dim(i).min();}
    HALIDE_ATTRIBUTE_DEPRECATED("extent() is deprecated. use dim(n).extent() instead.") 
    Expr extent(int i) const {return dim(i).extent();}
    HALIDE_ATTRIBUTE_DEPRECATED("stride() is deprecated. use dim(n).stride() instead.") 
    Expr stride(int i) const {return dim(i).stride();}
    // @}

    /** Get the alignment of the host pointer in bytes. Defaults to
     * the size of type. */
    EXPORT int host_alignment() const;

    /** Set the expected alignment of the host pointer in bytes. */
    EXPORT OutputImageParam &set_host_alignment(int);

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

}

#endif
