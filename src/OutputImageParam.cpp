#include "OutputImageParam.h"
#include "IROperator.h"

namespace Halide {

using Internal::Dimension;

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
