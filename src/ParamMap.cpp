#include "ParamMap.h"
#include "ImageParam.h"

namespace Halide {

void ParamMap::set(const ImageParam &p, Buffer<> &buf) {
    Internal::Parameter v(p.type(), true, p.dimensions(), p.name(), p.is_explicit_name(), false);
    v.set_buffer(buf);
    mapping[p.parameter()] = v;
};

}
