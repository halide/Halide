#ifndef HALIDE_REDUCTION_H
#define HALIDE_REDUCTION_H

/** \file
 * Defines internal classes related to Reduction Domains
 */

#include "IR.h"

namespace Halide {
namespace Internal {

/** A single named dimension of a reduction domain */
struct ReductionVariable {
    std::string var;
    Expr min, extent;
};

struct ReductionDomainContents;

/** A reference-counted handle on a reduction domain, which is just a
 * vector of ReductionVariable. */
class ReductionDomain {
    IntrusivePtr<ReductionDomainContents> contents;
public:
    /** Construct a new NULL reduction domain */
    ReductionDomain() : contents(NULL) {}

    /** Construct a reduction domain that spans the outer product of
     * all values of the given ReductionVariable in scanline order,
     * with the start of the vector being innermost, and the end of
     * the vector being outermost. */
    EXPORT ReductionDomain(const std::vector<ReductionVariable> &domain);

    /** Is this handle non-NULL */
    bool defined() const {
        return contents.defined();
    }

    /** Tests for equality of reference. Only one reduction domain is
     * allowed per reduction function, and this is used to verify
     * that */
    bool same_as(const ReductionDomain &other) const {
        return contents.same_as(other.contents);
    }

    /** Immutable access to the reduction variables. */
    EXPORT const std::vector<ReductionVariable> &domain() const;
};

}
}

#endif
