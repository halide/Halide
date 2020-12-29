#include "Argument.h"

namespace Halide {

bool ArgumentEstimates::operator==(const ArgumentEstimates &rhs) const {
    if (buffer_estimates.size() != rhs.buffer_estimates.size()) {
        return false;
    }
    for (size_t i = 0; i < buffer_estimates.size(); ++i) {
        if (!buffer_estimates[i].min.same_as(rhs.buffer_estimates[i].min) ||
            !buffer_estimates[i].extent.same_as(rhs.buffer_estimates[i].extent)) {
            return false;
        }
    }
    return scalar_def.same_as(rhs.scalar_def) &&
           scalar_min.same_as(rhs.scalar_min) &&
           scalar_max.same_as(rhs.scalar_max) &&
           scalar_estimate.same_as(rhs.scalar_estimate);
}

Argument::Argument(const std::string &_name, Kind _kind, const Type &_type, int _dimensions, const ArgumentEstimates &argument_estimates)
    : name(_name), kind(_kind), dimensions((uint8_t)_dimensions), type(_type), argument_estimates(argument_estimates) {
    internal_assert(dimensions >= 0 && dimensions <= 255);
    user_assert(!(is_scalar() && dimensions != 0)) << "Scalar Arguments must specify dimensions of 0";
    user_assert(argument_estimates.buffer_estimates.empty() ||
                argument_estimates.buffer_estimates.size() == dimensions)
        << "buffer_estimates must match dimensionality for Argument " << name;
}

}  // namespace Halide
