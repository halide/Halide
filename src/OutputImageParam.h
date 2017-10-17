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

class DimensionedParam;

namespace Internal {


}  // namespace Internal

/** A handle on the output buffer of a pipeline. Used to make static
 * promises about the output size and stride. */
class OutputImageParam : public Internal::DimensionedParameter {
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
    OutputImageParam() : Internal::DimensionedParameter() {}

    /** Construct an OutputImageParam that wraps an Internal Parameter object. */
    OutputImageParam(const Internal::Parameter &p, Argument::Kind k) :
        Internal::DimensionedParameter(), param(p), kind(k) {
    }

    ~OutputImageParam() {}

    /** Get the name of this Param */
    EXPORT const std::string &name() const;

    /** Get the type of the image data this Param refers to */
    EXPORT Type type() const;

    /** Is this parameter handle non-nullptr */
    EXPORT bool defined() const;

    /** Construct the appropriate argument matching this parameter,
     * for the purpose of generating the right type signature when
     * statically compiling halide pipelines. */
    EXPORT operator Argument() const;

    Internal::Parameter parameter() const {
        return param;
    }
};

}

#endif
