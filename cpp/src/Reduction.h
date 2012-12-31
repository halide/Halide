#ifndef HALIDE_REDUCTION_H
#define HALIDE_REDUCTION_H

#include "IntrusivePtr.h"

namespace Halide {
namespace Internal {

struct ReductionVariable {
    std::string var;
    Expr min, extent;
};

struct ReductionDomainContents {
    mutable RefCount ref_count;
    vector<ReductionVariable> domain;
};

class ReductionDomain {
    IntrusivePtr<ReductionDomainContents> contents;
public:
    ReductionDomain() : contents(NULL) {}

    ReductionDomain(const vector<ReductionVariable> &domain) : 
        contents(new ReductionDomainContents) {
        contents.ptr->domain = domain;
    }

    bool defined() const {
        return contents.defined();
    }

    bool same_as(const ReductionDomain &other) const {
        return contents.same_as(other.contents);
    }

    const vector<ReductionVariable> &domain() const {
        return contents.ptr->domain;
    }
};

}
}

#endif
