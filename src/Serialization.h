#ifndef HALIDE_SERIALIZATION_H
#define HALIDE_SERIALIZATION_H

#include "Pipeline.h"
#include <string>
#include <unordered_map>

namespace Halide {

void serialize_pipeline(const Pipeline &pipeline, const std::string &filename, std::unordered_map<std::string, Internal::Parameter> &params);

}  // namespace Halide

#endif
