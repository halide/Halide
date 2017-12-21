#include "ParamMap.h"
#include "ImageParam.h"

namespace Halide {

void ParamMap::set(const ImageParam &p, Buffer<> &buf, Buffer<> *buf_out_param) {
    Internal::Parameter v(p.type(), true, p.dimensions(), p.name(), p.is_explicit_name(), false);
    v.set_buffer(buf);
    ParamArg pa;
    pa.mapped_param = v;
    pa.buf_out_param = buf_out_param;
    mapping[p.parameter()] = pa;
};

}
