#ifndef HALIDE_OUTPUT_IMAGE_PARAM_H
#define HALIDE_OUTPUT_IMAGE_PARAM_H

/** \file
 *
 * Classes for declaring output image parameters to halide pipelines
 */

#include "Argument.h"
#include "Dimension.h"
#include "runtime/HalideRuntime.h"
#include "Var.h"
#include "Dimension.h"
#include "Func.h"

namespace Halide {

/** A handle on the output buffer of a pipeline. Used to make static
 * promises about the output size and stride. */
class OutputImageParam {
protected:
    friend class Func;

    /** A reference-counted handle on the internal parameter object */
    Internal::Parameter param;

    /** Is this an input or an output? OutputImageParam is the base class for both. */
    Argument::Kind kind;

    /** If Input: Func representation of the ImageParam.
     * If Output: Func that creates this OutputImageParam.
     */
    Func func;

    void add_implicit_args_if_placeholder(std::vector<Expr> &args,
                                          Expr last_arg,
                                          int total_args,
                                          bool *placeholder_seen) const;

    /** Construct an OutputImageParam that wraps an Internal Parameter object. */
    EXPORT OutputImageParam(const Internal::Parameter &p, Argument::Kind k, Func f);

public:

    /** Construct a null image parameter handle. */
    OutputImageParam() {}

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
