#ifndef HALIDE_SERIALIZATION_H
#define HALIDE_SERIALIZATION_H

#include "Pipeline.h"

namespace Halide {

void serialize_pipeline(const Pipeline &pipeline, const std::string &filename, std::map<std::string, Parameter> &params);

}  // namespace Halide

#endif
