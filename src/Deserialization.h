#ifndef HALIDE_DESERIALIZATION_H
#define HALIDE_DESERIALIZATION_H

#include "Pipeline.h"

namespace Halide {

Pipeline deserialize_pipeline(const std::string &filename, const std::unordered_map<std::string, Internal::Parameter> &params);

}  // namespace Halide

#endif
