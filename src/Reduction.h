#ifndef HALIDE_REDUCTION_H
#define HALIDE_REDUCTION_H

/** \file
 * Defines internal classes related to Reduction Domains
 */

#include "Expr.h"

namespace Halide {
namespace Internal {

class IRMutator;

/** A single named dimension of a reduction domain */
struct ReductionVariable {
    std::string var;
    Expr min, extent;

    /** This lets you use a ReductionVariable as a key in a map of the form
     * map<ReductionVariable, Foo, ReductionVariable::Compare> */
    struct Compare {
        bool operator()(const ReductionVariable &a, const ReductionVariable &b) const {
            return a.var < b.var;
        }
    };
};

struct ReductionDomainContents;

/** A reference-counted handle on a reduction domain, which is just a
 * vector of ReductionVariable. */
class ReductionDomain {
    IntrusivePtr<ReductionDomainContents> contents;

public:
    /** This lets you use a ReductionDomain as a key in a map of the form
     * map<ReductionDomain, Foo, ReductionDomain::Compare> */
    struct Compare {
        bool operator()(const ReductionDomain &a, const ReductionDomain &b) const {
            internal_assert(a.contents.defined() && b.contents.defined());
            return a.contents < b.contents;
        }
    };

    /** Construct a new nullptr reduction domain */
    ReductionDomain()
        : contents(nullptr) {
    }

    /** Construct a reduction domain that spans the outer product of
     * all values of the given ReductionVariable in scanline order,
     * with the start of the vector being innermost, and the end of
     * the vector being outermost. */
    ReductionDomain(const std::vector<ReductionVariable> &domain);

    /** Return a deep copy of this ReductionDomain. */
    ReductionDomain deep_copy() const;

    /** Is this handle non-nullptr */
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
    const std::vector<ReductionVariable> &domain() const;

    /** Add predicate to the reduction domain. See \ref RDom::where
     * for more details. */
    void where(Expr predicate);

    /** Return the predicate defined on this reducation demain. */
    Expr predicate() const;

    /** Set the predicate, replacing any previously set predicate. */
    void set_predicate(Expr);

    /** Split predicate into vector of ANDs. If there is no predicate (i.e. all
     * iteration domain in this reduction domain is valid), this returns an
     * empty vector. */
    std::vector<Expr> split_predicate() const;

    /** Mark RDom as frozen, which means it cannot accept new predicates. An
     * RDom is frozen once it is used in a Func's update definition. */
    void freeze();

    /** Check if a RDom has been frozen. If so, it is an error to add new
     * predicates. */
    bool frozen() const;

    /** Pass an IRVisitor through to all Exprs referenced in the
     * ReductionDomain. */
    void accept(IRVisitor *) const;

    /** Pass an IRMutator through to all Exprs referenced in the
     * ReductionDomain. */
    void mutate(IRMutator *);
};

void split_predicate_test();

}  // namespace Internal
}  // namespace Halide

#endif
