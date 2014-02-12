#include "IR.h"
#include "Reduction.h"

namespace Halide {
namespace Internal {
template<>
EXPORT RefCount &ref_count<Halide::Internal::ReductionDomainContents>(const ReductionDomainContents *p) {return p->ref_count;}

template<>
EXPORT void destroy<Halide::Internal::ReductionDomainContents>(const ReductionDomainContents *p) {delete p;}
}
}
