#ifndef HALIDE_REDUCTION_H
#define HALIDE_REDUCTION_H

#include "IntrusivePtr.h"
#include <string>
#include <vector>

namespace Halide {
namespace Internal {

struct ReductionVariable {
    std::string var;
    Expr min, extent;
};

struct ReductionDomainContents {
    mutable RefCount ref_count;
    std::vector<ReductionVariable> domain;
};

class ReductionDomain {
    IntrusivePtr<ReductionDomainContents> contents;
public:
    ReductionDomain() : contents(NULL) {}

    ReductionDomain(const std::vector<ReductionVariable> &domain) : 
        contents(new ReductionDomainContents) {
        contents.ptr->domain = domain;
    }

    bool defined() const {
        return contents.defined();
    }

    bool same_as(const ReductionDomain &other) const {
        return contents.same_as(other.contents);
    }

    const std::vector<ReductionVariable> &domain() const {
        return contents.ptr->domain;
    }
};

}
}

#endif
