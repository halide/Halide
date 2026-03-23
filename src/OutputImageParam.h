#ifndef HALIDE_OUTPUT_IMAGE_PARAM_H
#define HALIDE_OUTPUT_IMAGE_PARAM_H

/** \file
 *
 * Classes for declaring output image parameters to halide pipelines
 */

#include "Argument.h"
#include "Dimension.h"
#include "Func.h"
#include "Var.h"
#include "runtime/HalideRuntime.h"

namespace Halide {

/** A handle on the output buffer of a pipeline. Used to make static
 * promises about the output size and stride. */
class OutputImageParam {
protected:
    friend class Func;

    /** A reference-counted handle on the internal parameter object */
    Parameter param;

    /** Is this an input or an output? OutputImageParam is the base class for both. */
    Argument::Kind kind = Argument::InputScalar;

    /** If Input: Func representation of the ImageParam.
     * If Output: Func that creates this OutputImageParam.
     */
    Func func;

    void add_implicit_args_if_placeholder(std::vector<Expr> &args,
                                          Expr last_arg,
                                          int total_args,
                                          bool *placeholder_seen) const;

    /** Construct an OutputImageParam that wraps an Internal Parameter object. */
    OutputImageParam(const Parameter &p, Argument::Kind k, Func f);

public:
    /** Construct a null image parameter handle. */
    OutputImageParam() = default;

    /** Get the name of this Param */
    const std::string &name() const;

    /** Get the type of the image data this Param refers to */
    Type type() const;

    /** Is this parameter handle non-nullptr */
    bool defined() const;

    /** Get a handle on one of the dimensions for the purposes of
     * inspecting or constraining its min, extent, or stride. */
    Internal::Dimension dim(int i);

    /** Get a handle on one of the dimensions for the purposes of
     * inspecting its min, extent, or stride. */
    Internal::Dimension dim(int i) const;

    /** Get the alignment of the host pointer in bytes. Defaults to
     * the size of type. */
    int host_alignment() const;

    /** Set the expected alignment of the host pointer in bytes. */
    OutputImageParam &set_host_alignment(int);

    /** Get the dimensionality of this image parameter */
    int dimensions() const;

    /** Get an expression giving the minimum coordinate in dimension 0, which
     * by convention is the coordinate of the left edge of the image */
    Expr left() const;

    /** Get an expression giving the maximum coordinate in dimension 0, which
     * by convention is the coordinate of the right edge of the image */
    Expr right() const;

    /** Get an expression giving the minimum coordinate in dimension 1, which
     * by convention is the top of the image */
    Expr top() const;

    /** Get an expression giving the maximum coordinate in dimension 1, which
     * by convention is the bottom of the image */
    Expr bottom() const;

    /** Get an expression giving the extent in dimension 0, which by
     * convention is the width of the image */
    Expr width() const;

    /** Get an expression giving the extent in dimension 1, which by
     * convention is the height of the image */
    Expr height() const;

    /** Get an expression giving the extent in dimension 2, which by
     * convention is the channel-count of the image */
    Expr channels() const;

    /** Get at the internal parameter object representing this ImageParam. */
    Parameter parameter() const;

    /** Construct the appropriate argument matching this parameter,
     * for the purpose of generating the right type signature when
     * statically compiling halide pipelines. */
    operator Argument() const;

    /** Using a param as the argument to an external stage treats it
     * as an Expr */
    operator ExternFuncArgument() const;

    /** Set (min, extent) estimates for all dimensions in the ImageParam
     * at once; this is equivalent to calling `dim(n).set_estimate(min, extent)`
     * repeatedly, but slightly terser. The size of the estimates vector
     * must match the dimensionality of the ImageParam. */
    OutputImageParam &set_estimates(const Region &estimates);

    /** Set the desired storage type for this parameter.  Only useful
     * for MemoryType::GPUTexture at present */
    OutputImageParam &store_in(MemoryType type);
};

}  // namespace Halide

#endif
