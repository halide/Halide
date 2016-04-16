#include "IR.h"
#include "Reduction.h"

namespace Halide {
namespace Internal {

struct ReductionDomainContents {
    mutable RefCount ref_count;
    std::vector<ReductionVariable> domain;
    std::vector<Expr> predicates;
};

template<>
EXPORT RefCount &ref_count<Halide::Internal::ReductionDomainContents>(const ReductionDomainContents *p) {return p->ref_count;}

template<>
EXPORT void destroy<Halide::Internal::ReductionDomainContents>(const ReductionDomainContents *p) {delete p;}

ReductionDomain::ReductionDomain(const std::vector<ReductionVariable> &domain) :
    contents(new ReductionDomainContents) {
    contents.ptr->domain = domain;
}

const std::vector<ReductionVariable> &ReductionDomain::domain() const {
    return contents.ptr->domain;
}

void ReductionDomain::bound(const Expr &predicate) {
    contents.ptr->predicates.push_back(predicate);
}

const std::vector<Expr> &ReductionDomain::predicates() const {
    return contents.ptr->predicates;
}

std::vector<Expr> &ReductionDomain::predicates() {
    return contents.ptr->predicates;
}

}
}
