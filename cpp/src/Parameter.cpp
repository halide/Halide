#include "IR.h"
#include "Parameter.h"

namespace Halide {
namespace Internal {
template<>
RefCount &ref_count<Halide::Internal::ParameterContents>(const ParameterContents *p) {return p->ref_count;}

template<>
void destroy<Halide::Internal::ParameterContents>(const ParameterContents *p) {delete p;}
}
}
