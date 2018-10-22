#include "ParamMap.h"
#include "ImageParam.h"

namespace Halide {

ParamMap::ParamMap(const std::initializer_list<ParamMapping> &init) {
    for (const auto &pm : init) {
        if (pm.parameter != nullptr) {
            mapping[*pm.parameter] = ParamArg(pm);
        } else if (pm.buf_out_param == nullptr) {
            // TODO: there has to be a way to do this without the const_cast.
            set(*pm.image_param, *const_cast<Buffer<> *>(&pm.buf), nullptr);
        } else {
            Buffer<> temp_undefined;
            set(*pm.image_param, temp_undefined, pm.buf_out_param);
        }
    }
}

void ParamMap::set(const ImageParam &p, Buffer<> &buf, Buffer<> *buf_out_param) {
    Internal::Parameter v(p.type(), true, p.dimensions(), p.name());
    v.set_buffer(buf);
    ParamArg pa;
    pa.mapped_param = v;
    pa.buf_out_param = buf_out_param;
    mapping[p.parameter()] = pa;
};

const Internal::Parameter &ParamMap::map(const Internal::Parameter &p, Buffer<> *&buf_out_param) const {
    auto iter = mapping.find(p);
    if (iter != mapping.end()) {
        buf_out_param = iter->second.buf_out_param;
        return iter->second.mapped_param;
    } else {
        buf_out_param = nullptr;
        return p;
    }
}

Internal::Parameter &ParamMap::map(Internal::Parameter &p, Buffer<> *&buf_out_param) const {
    auto iter = mapping.find(p);
    if (iter != mapping.end()) {
        buf_out_param = iter->second.buf_out_param;
        return iter->second.mapped_param;
    } else {
        buf_out_param = nullptr;
        return p;
    }
}

}  // namespace Halide
