#include "ParamMap.h"
#include "Buffer.h"
#include "ImageParam.h"
#include "Parameter.h"

namespace Halide {

namespace Internal {

namespace {

struct ParamArg {
    Internal::Parameter mapped_param;
    Buffer<void> *buf_out_param = nullptr;
};

}  // namespace

struct ParamMapContents {
    std::map<const Internal::Parameter, ParamArg> mapping;
};

}  // namespace Internal

using Internal::ParamArg;
using Internal::ParamMapContents;

ParamMap::ParamMap()
    : contents(new ParamMapContents) {
}

ParamMap::ParamMap(const std::initializer_list<ParamMapping> &init)
    : contents(new ParamMapContents) {
    for (const auto &pm : init) {
        if (pm.parameter != nullptr) {
            set_scalar(*pm.parameter, pm.parameter->type(), pm.value);
        } else if (pm.buf_in_param != nullptr) {
            set_input_buffer(*pm.image_param, *pm.buf_in_param);
        } else {
            set_output_buffer(*pm.image_param, pm.buf_out_param);
        }
    }
}

// Needed because we have unique_ptr of incomplete class in our decl
ParamMap::~ParamMap() = default;

void ParamMap::set_input_buffer(const ImageParam &p, const Buffer<> &buf_in_param) {
    Internal::Parameter v(p.type(), true, p.dimensions(), p.name());
    v.set_buffer(buf_in_param);
    contents->mapping[p.parameter()] = ParamArg{std::move(v), nullptr};
};

void ParamMap::set_output_buffer(const ImageParam &p, Buffer<> *buf_out_param) {
    internal_assert(buf_out_param != nullptr);
    Internal::Parameter v(p.type(), true, p.dimensions(), p.name());
    contents->mapping[p.parameter()] = ParamArg{std::move(v), buf_out_param};
};

void ParamMap::set_scalar(const Internal::Parameter &p, const Type &t, const halide_scalar_value_t &val) {
    Internal::Parameter v(p.type(), false, 0, p.name());
    v.set_scalar(t, val);
    contents->mapping[p] = ParamArg{std::move(v), nullptr};
};

void ParamMap::set(const ImageParam &p, const Buffer<void> &buf) {
    set_input_buffer(p, buf);
}

size_t ParamMap::size() const {
    return contents->mapping.size();
}

const ParamMap &ParamMap::empty_map() {
    static ParamMap empty_param_map;
    return empty_param_map;
}

const Internal::Parameter &ParamMap::map(const Internal::Parameter &p, Buffer<> *&buf_out_param) const {
    auto iter = contents->mapping.find(p);
    if (iter != contents->mapping.end()) {
        buf_out_param = iter->second.buf_out_param;
        return iter->second.mapped_param;
    } else {
        buf_out_param = nullptr;
        return p;
    }
}

Internal::Parameter &ParamMap::map(Internal::Parameter &p, Buffer<> *&buf_out_param) const {
    auto iter = contents->mapping.find(p);
    if (iter != contents->mapping.end()) {
        buf_out_param = iter->second.buf_out_param;
        return iter->second.mapped_param;
    } else {
        buf_out_param = nullptr;
        return p;
    }
}

}  // namespace Halide
