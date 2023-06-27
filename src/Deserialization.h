#ifndef HALIDE_DESERIALIZATION_H
#define HALIDE_DESERIALIZATION_H

#include "Pipeline.h"

namespace Halide {
Pipeline deserialize_pipeline(const std::string &filename);
}  // namespace Halide

#endif
