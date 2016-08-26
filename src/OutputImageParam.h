#ifndef HALIDE_OUTPUT_IMAGE_PARAM_H
#define HALIDE_OUTPUT_IMAGE_PARAM_H

/** \file
 *
 * Classes for declaring output image parameters to halide pipelines
 */

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

    struct Dimension {
        /** Get an expression representing the minimum coordinates of this image
         * parameter in the given dimension. */
        EXPORT Expr min() const;

        /** Get an expression representing the extent of this image
         * parameter in the given dimension */
        EXPORT Expr extent() const;

        /** Get an expression representing the maximum coordinates of
         * this image parameter in the given dimension. */
        EXPORT Expr max() const;

        /** Get an expression representing the stride of this image in the
         * given dimension */
        EXPORT Expr stride() const;

        /** Set the min in a given dimension to equal the given
         * expression. Setting the mins to zero may simplify some
         * addressing math. */
        EXPORT Dimension set_min(Expr e);

        /** Set the extent in a given dimension to equal the given
         * expression. Images passed in that fail this check will generate
         * a runtime error. Returns a reference to the ImageParam so that
         * these calls may be chained.
         *
         * This may help the compiler generate better
         * code. E.g:
         \code
         im.dim(0).set_extent(100);
         \endcode
         * tells the compiler that dimension zero must be of extent 100,
         * which may result in simplification of boundary checks. The
         * value can be an arbitrary expression:
         \code
         im.dim(0).set_extent(im.dim(1).extent());
         \endcode
         * declares that im is a square image (of unknown size), whereas:
         \code
         im.dim(0).set_extent((im.dim(0).extent()/32)*32);
         \endcode
         * tells the compiler that the extent is a multiple of 32. */
        EXPORT Dimension set_extent(Expr e);

        /** Set the stride in a given dimension to equal the given
         * value. This is particularly helpful to set when
         * vectorizing. Known strides for the vectorized dimension
         * generate better code. */
        EXPORT Dimension set_stride(Expr e);

        /** Set the min and extent in one call. */
        EXPORT Dimension set_bounds(Expr min, Expr extent);

        /** Get a different dimension of the same buffer */
        // @{
        EXPORT Dimension dim(int i);
        EXPORT const Dimension dim(int i) const;
        // @}

    private:
        friend class OutputImageParam;

        /** Construct a Dimension representing dimension d of some
         * Internal::Parameter p. Only OutputImageParam may construct
         * these. */
        Dimension(const Internal::Parameter &p, int d) : param(p), d(d) {}

        /** Only OutputImageParam may copy these, too. This prevents
         * users removing constness by making a non-const copy. */
        Dimension(const Dimension &) = default;

        Internal::Parameter param;
        int d;
    };

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
    EXPORT Dimension dim(int i);

    /** Get a handle on one of the dimensions for the purposes of
     * inspecting its min, extent, or stride. */
    EXPORT const Dimension dim(int i) const;

    /** Get or constrain the shape of the dimensions. Soon to be
     * deprecated. Do not use. */
    // @{
    OutputImageParam set_min(int i, Expr e) {dim(i).set_min(e); return *this;}
    OutputImageParam set_extent(int i, Expr e) {dim(i).set_extent(e); return *this;}
    OutputImageParam set_bounds(int i, Expr a, Expr b) {dim(i).set_bounds(a, b); return *this;}
    OutputImageParam set_stride(int i, Expr e) {dim(i).set_stride(e); return *this;}
    Expr min(int i) {return dim(i).min();}
    Expr extent(int i) {return dim(i).extent();}
    Expr stride(int i) {return dim(i).stride();}
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
