#ifndef HALIDE_DIMENSION_H
#define HALIDE_DIMENSION_H

/** \file
 * Defines the Dimension utility class for Halide pipelines
 */

#include <utility>

#include "Func.h"
#include "Parameter.h"

namespace Halide {
namespace Internal {

class Dimension {
public:
    /** Get an expression representing the minimum coordinates of this image
     * parameter in the given dimension. */
    Expr min() const;

    /** Get an expression representing the extent of this image
     * parameter in the given dimension */
    Expr extent() const;

    /** Get an expression representing the maximum coordinates of
     * this image parameter in the given dimension. */
    Expr max() const;

    /** Get an expression representing the stride of this image in the
     * given dimension */
    Expr stride() const;

    /** Set the min in a given dimension to equal the given
     * expression. Setting the mins to zero may simplify some
     * addressing math. */
    Dimension set_min(const Expr &min);

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
    Dimension set_extent(const Expr &extent);

    /** Set the stride in a given dimension to equal the given
     * value. This is particularly helpful to set when
     * vectorizing. Known strides for the vectorized dimension
     * generate better code. */
    Dimension set_stride(const Expr &stride);

    /** Set the min and extent in one call. */
    Dimension set_bounds(const Expr &min, const Expr &extent);

    /** Set the min and extent estimates in one call. These values are only
     * used by the auto-scheduler and/or the RunGen tool/ */
    Dimension set_estimate(const Expr &min, const Expr &extent);

    Expr min_estimate() const;
    Expr extent_estimate() const;

    /** Get a different dimension of the same buffer */
    // @{
    Dimension dim(int i) const;
    // @}

private:
    friend class ::Halide::OutputImageParam;

    /** Construct a Dimension representing dimension d of some
     * Internal::Parameter p. Only friends may construct
     * these. */
    Dimension(const Internal::Parameter &p, int d, Func f);

    Parameter param;
    int d;
    Func f;
};

}  // namespace Internal
}  // namespace Halide

#endif
