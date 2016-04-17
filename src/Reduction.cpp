#include "IR.h"
#include "IROperator.h"
#include "Reduction.h"

namespace Halide {
namespace Internal {

struct ReductionDomainContents {
    mutable RefCount ref_count;
    std::vector<ReductionVariable> domain;
    std::vector<Expr> predicates;
    bool frozen;

    ReductionDomainContents() : frozen(false) {}
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

void ReductionDomain::where(const Expr &predicate) {
    contents.ptr->predicates.push_back(predicate);
}

const std::vector<Expr> &ReductionDomain::predicates() const {
    return contents.ptr->predicates;
}

std::vector<Expr> &ReductionDomain::predicates() {
    return contents.ptr->predicates;
}

Expr ReductionDomain::and_predicates() const {
	if (contents.ptr->predicates.empty()) {
		return Expr();
	}
	Expr and_pred = contents.ptr->predicates[0];
	for (size_t i = 1; i < contents.ptr->predicates.size(); ++i) {
		and_pred = (and_pred && contents.ptr->predicates[i]);
	}
	return and_pred;
}

void ReductionDomain::freeze() {
    contents.ptr->frozen = true;
}

bool ReductionDomain::frozen() const {
    return contents.ptr->frozen;
}

}
}
