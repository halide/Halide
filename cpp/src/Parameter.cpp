#include "IR.h"
#include "Parameter.h"

namespace Halide {
namespace Internal {
template<>
EXPORT RefCount &ref_count<Halide::Internal::ParameterContents>(const ParameterContents *p) {return p->ref_count;}

template<>
EXPORT void destroy<Halide::Internal::ParameterContents>(const ParameterContents *p) {delete p;}
}
}
