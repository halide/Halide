#include "OutputImageParam.h"
#include "IROperator.h"

namespace Halide {

using Internal::Dimension;

OutputImageParam::OutputImageParam(const OutputImageParam& that) : 
    Internal::DimensionedParameter(), param(that.param), kind(that.kind) {}

OutputImageParam::~OutputImageParam() {}

OutputImageParam::OutputImageParam(const Internal::Parameter &p, Argument::Kind k) :
    Internal::DimensionedParameter(), param(p), kind(k) {
}

Internal::Parameter OutputImageParam::parameter() const {
    return param;
}

const std::string &OutputImageParam::name() const {
    return param.name();
}

Type OutputImageParam::type() const {
    return param.type();
}

bool OutputImageParam::defined() const {
    return param.defined();
}

OutputImageParam::operator Argument() const {
    return Argument(name(), kind, type(), dimensions());
}

namespace Internal {

}  // namespace Internal
}  // namespace Halide
